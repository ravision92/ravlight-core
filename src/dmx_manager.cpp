// Arduino/config headers must come before <lwip/sockets.h> to avoid the
// INADDR_NONE conflict: lwIP defines it as a macro, Arduino's IPAddress.h
// declares it as an extern variable — if lwIP wins the race the declaration fails.
#include "config.h"
#include <dmx_manager.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "AsyncUDP.h"
#ifdef RAVLIGHT_MODULE_RECORDER
#include "dmx_recorder.h"
#endif

#include <lwip/sockets.h>
#include <fcntl.h>
#include <string.h>

#if defined(RAVLIGHT_MODULE_DMX_PHYSICAL) || defined(RAVLIGHT_MODULE_DMX_PHYSICAL_2)
#include <esp_dmx.h>
#endif

#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
dmx_port_t dmxPort = DMX_NUM_1;
bool dmxIsConnected = false;
#endif

#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL_2
dmx_port_t dmxPort2 = DMX_NUM_0;
bool dmxIsConnected2 = false;
#endif

#define TAG "DMX"

SemaphoreHandle_t dmxBufferMutex = NULL;
uint8_t dmxBuffer[DMX_BUFFER_SIZE];

// ── Universe pool ─────────────────────────────────────────────────────────────
//
// Double-buffered to support Art-Net 4 ArtSync. Each universe carries two
// buffers; writeToPool() always writes the "pending" one (1 - active_idx).
// ArtSync flips active_idx atomically for every universe that was touched —
// the render task then sees a coherent snapshot across all universes instead
// of mid-frame torn data.
//
// When no ArtSync packet has been seen recently (ARTSYNC_TIMEOUT_MS), the
// receiver falls back to "immediate apply" — writeToPool swaps the buffer
// inline, matching the original behaviour for controllers that don't speak
// Art-Net 4 (xLights, ETC consoles, etc.).
//
// Reads (getUniverseData) are lock-free: a single byte load of active_idx
// is atomic on ESP32, and the active buffer never gets written by anyone
// other than the next swap. Render therefore never blocks on the mutex.

struct universe_slot_t {
    uint16_t id;
    uint32_t last_seen_ms;     // millis() of the most recent frame for this universe (0 = never)
    uint8_t  active_idx;       // 0 or 1 — which of data[] the render currently sees
    uint8_t  pending_dirty;    // 1 if data[1-active_idx] has new bytes awaiting an ArtSync swap
    uint8_t  data[2][513];     // double buffer; [0] unused channel slot per buffer
};
static universe_slot_t universePool[DMX_MAX_UNIVERSES];
static uint8_t         universeCount = 0;

// Sync packet tracking — covers both Art-Net 4 ArtSync (opcode 0x5200) and
// E1.31 Extended Synchronization (VECTOR_E131_EXTENDED_SYNCHRONIZATION). When
// either has been seen recently we run in "buffered" mode (writeToPool defers
// the active_idx flip); after SYNC_TIMEOUT_MS of silence we fall back to
// inline apply so legacy controllers without sync still work.
static volatile uint32_t s_sync_last_ms      = 0;
static volatile uint32_t s_artsync_packets   = 0;   // Art-Net 0x5200
static volatile uint32_t s_sacnsync_packets  = 0;   // sACN ext sync
static volatile bool     s_swap_pending      = false;
static const uint32_t    SYNC_TIMEOUT_MS     = 1000;

static inline bool sync_active(uint32_t now_ms) {
    return s_sync_last_ms != 0 && (now_ms - s_sync_last_ms) < SYNC_TIMEOUT_MS;
}

void registerDmxUniverse(uint16_t universe) {
    for (int i = 0; i < universeCount; i++)
        if (universePool[i].id == universe) return;
    if (universeCount >= DMX_MAX_UNIVERSES) {
        ESP_LOGW(TAG, "Universe pool full, ignoring u%d", universe);
        return;
    }
    memset(&universePool[universeCount], 0, sizeof(universe_slot_t));
    universePool[universeCount].id = universe;
    universeCount++;
    ESP_LOGI(TAG, "Registered universe %d (total %d)", universe, universeCount);
}

const uint8_t* getUniverseData(uint16_t universe) {
    // Lock-free read: the render task sees the active buffer; ArtSync swaps
    // active_idx atomically (single-byte write). Writes to the pending buffer
    // by the network task never touch the active buffer that we return here.
    for (int i = 0; i < universeCount; i++)
        if (universePool[i].id == universe)
            return universePool[i].data[universePool[i].active_idx];
    return nullptr;
}

// ArtNet data is 0-indexed (ch1 at src[0]); pool is 1-indexed (ch1 at data[1]).
// Always writes to the "pending" buffer. If we're not currently seeing
// ArtSync packets (or have never seen one), we also flip active_idx in the
// same call so legacy controllers behave as before.
static void writeToPool(uint16_t universe, const uint8_t* src, uint16_t size) {
    uint16_t n = (size > 512) ? 512 : size;
    uint32_t now = millis();
    bool immediate = !sync_active(now);

    for (int i = 0; i < universeCount; i++) {
        if (universePool[i].id != universe) continue;
        uint8_t pending = 1 - universePool[i].active_idx;
        memcpy(universePool[i].data[pending] + 1, src, n);
        universePool[i].last_seen_ms = now;
        if (immediate) {
            // No ArtSync mode: swap inline so this frame is visible right away.
            universePool[i].active_idx   = pending;
            universePool[i].pending_dirty = 0;
        } else {
            universePool[i].pending_dirty = 1;
        }
        if (universe == dmxConfig.startUniverse)
            memcpy(dmxBuffer + 1, src, n);
        return;
    }
}

// Atomically swap every dirty universe's active buffer in one pass. Called
// from onArtnetPacket() on ArtSync receipt under dmxBufferMutex, and also
// deferred-applied at the start of each render frame (see dmxApplyPendingSwap).
static void artsyncSwapDirty() {
    for (int i = 0; i < universeCount; i++) {
        if (universePool[i].pending_dirty) {
            universePool[i].active_idx ^= 1;
            universePool[i].pending_dirty = 0;
        }
    }
}

// Render task calls this at the start of every frame: if an ArtSync arrived
// while a frame was in flight we deferred the actual swap to avoid mid-frame
// tearing. Apply it now, under the mutex, before reading any universe data.
void dmxApplyPendingSwap() {
    if (!s_swap_pending || !dmxBufferMutex) return;
    xSemaphoreTake(dmxBufferMutex, portMAX_DELAY);
    if (s_swap_pending) {
        artsyncSwapDirty();
        s_swap_pending = false;
    }
    xSemaphoreGive(dmxBufferMutex);
}

uint32_t artsyncPacketCount()  { return s_artsync_packets;  }
uint32_t sacnsyncPacketCount() { return s_sacnsync_packets; }

uint32_t getUniverseLastSeen(uint16_t universe) {
    for (int i = 0; i < universeCount; i++)
        if (universePool[i].id == universe) return universePool[i].last_seen_ms;
    return 0;
}

uint8_t  dmxUniverseCount()           { return universeCount; }
uint16_t dmxUniverseAt(uint8_t idx)   { return (idx < universeCount) ? universePool[idx].id : 0; }

// Runtime gates for the ArtNet + sACN receive paths — declared here
// (before writeToPoolSacn / onArtnetPacket reference them) so both the
// polled and task-driven inputs can bail out early on a residual packet
// after the input source has been switched. reinitDMXInput() flips these
// low before tearing down and high after the new path is up.
static volatile bool s_artnet_gate = false;
static volatile bool s_sacn_gate   = false;

// ── Source frame-rate estimator ───────────────────────────────────────────────
// Cumulative-counter delta over a 250 ms sliding window, divided by the
// number of universes that received traffic in the last 1.5 s. Gives the
// real per-universe frame rate of the controller upstream, whether it's
// pushing a single universe or sixteen, and whether it's Art-Net or sACN.
// Updated by tickDmxFps() (called from main loop); dmxSourceFps() just
// returns the cached value.
static uint16_t s_dmx_fps          = 0;
static uint32_t s_dmx_fps_last_ms  = 0;
static uint32_t s_dmx_fps_last_pkt = 0;

uint16_t dmxSourceFps(void) { return s_dmx_fps; }

void tickDmxFps(void) {
    uint32_t now = millis();
    if (now - s_dmx_fps_last_ms < 250) return;
    s_dmx_fps_last_ms = now;

    // Sum every source that updates the universe pool — network ArtNet,
    // network sACN, the wired RS-485 RX path AND internal injectors
    // (effects engine, recorder playback, test pattern). Without this
    // a device running on built-in effects or wired DMX RX would always
    // report 0 fps on the Info popup even though traffic is flowing.
    uint32_t pkts = artnetPacketCount() + sacnPacketCount()
                  + wiredPacketCount()  + injectPacketCount();
    uint32_t da   = (pkts >= s_dmx_fps_last_pkt) ? (pkts - s_dmx_fps_last_pkt) : 0;
    s_dmx_fps_last_pkt = pkts;

    if (!dmxIsActive() && da == 0) { s_dmx_fps = 0; return; }

    uint8_t n_active = 0;
    for (int i = 0; i < universeCount; i++) {
        uint32_t last = universePool[i].last_seen_ms;
        if (last != 0 && (now - last) < 1500) n_active++;
    }
    if (n_active == 0) n_active = 1;

    uint16_t inst = (uint16_t)((da * 4) / n_active);   // tick=250ms → ×4 → /n_active
    s_dmx_fps = (uint16_t)((s_dmx_fps + inst) / 2);    // 1-frame IIR smoothing
}

// Symmetric to s_artnetPackets: counts every E1.31 ArtDMX-equivalent data
// packet that lands in the pool. Both sACN receive paths (the dedicated
// task and the legacy polled get131DMX) funnel through writeToPoolSacn,
// so a single increment here covers both.
static volatile uint32_t s_sacnPackets   = 0;
// Frames received from the local wired RS-485 RX path.
static volatile uint32_t s_wiredPackets  = 0;
// Frames injected by internal sources — effects engine, recorder
// playback, test pattern. Anything that drives the pool without coming
// over the network.
static volatile uint32_t s_injectPackets = 0;

uint32_t sacnPacketCount(void)   { return s_sacnPackets;   }
uint32_t wiredPacketCount(void)  { return s_wiredPackets;  }
uint32_t injectPacketCount(void) { return s_injectPackets; }

static void writeToPoolSacn(uint16_t universe, const uint8_t* src, uint16_t size) {
    if (!s_sacn_gate) return;   // gated during input-source switch
    s_sacnPackets++;
    writeToPool(universe, src, size);
}

void injectDmxUniverse(uint16_t universe, const uint8_t* src, uint16_t length) {
    if (!src || !dmxBufferMutex) return;
    xSemaphoreTake(dmxBufferMutex, portMAX_DELAY);
    writeToPool(universe, src, length);
    xSemaphoreGive(dmxBufferMutex);
    s_injectPackets++;
    DMXLedRun();
}

// ── ArtNet (AsyncUDP, port 6454) ─────────────────────────────────────────────
// AsyncUDP registers a raw lwIP udp_recv callback: each datagram is handled the
// moment the network stack delivers it, with no fixed-size BSD-socket receive
// mailbox in the path. That mailbox (~6 datagrams, a compile-time limit in the
// Arduino framework) was the wall — Resolume bursts all 16 universes at once and
// the tail was dropped before the firmware could read it. Same approach WLED
// uses to ingest many universes on this hardware.

#define ARTNET_PORT 6454
static AsyncUDP artnetUdp;
static volatile uint32_t s_artnetPackets = 0;  // received ArtDMX count (diagnostic)
static void sendArtPollReply(const IPAddress& requester);  // defined below

static void onArtnetPacket(AsyncUDPPacket& packet) {
    // Runtime gate: drop the packet immediately when the current input
    // source isn't ArtNet. ArtPoll replies stay on (opcode 0x2000 below)
    // so the fixture keeps announcing itself to the network even in
    // Effects/AutoScene modes.
    if (!s_artnet_gate) {
        // Still answer discovery even when gated — hidden devices are a
        // worse UX than briefly leaking a poll reply.
        if (packet.length() >= 10 &&
            memcmp(packet.data(), "Art-Net\0", 8) == 0 &&
            (packet.data()[8] | ((uint16_t)packet.data()[9] << 8)) == 0x2000) {
            sendArtPollReply(packet.remoteIP());
        }
        return;
    }
    const uint8_t* buf = packet.data();
    int n = (int)packet.length();
    if (n < 10 || memcmp(buf, "Art-Net\0", 8) != 0) return;
    uint16_t opcode = buf[8] | ((uint16_t)buf[9] << 8);   // LE
    if (opcode == 0x5000) {          // ArtDMX
        if (n < 18) return;
        uint16_t universe = buf[14] | ((uint16_t)(buf[15] & 0x7F) << 8);
        uint16_t length   = ((uint16_t)buf[16] << 8) | buf[17];   // BE
        if (length > 512 || n < 18 + (int)length) return;
        s_artnetPackets++;
        if (dmxBufferMutex) {
            xSemaphoreTake(dmxBufferMutex, portMAX_DELAY);
            writeToPool(universe, buf + 18, length);
            xSemaphoreGive(dmxBufferMutex);
        }
        DMXLedRun();
    } else if (opcode == 0x5200) {  // ArtSync (Art-Net 4)
        // Mark every dirty universe ready for the next render frame. We defer
        // the actual active_idx flip to dmxApplyPendingSwap() so a render in
        // flight doesn't see a half-swapped pool. Set the timestamp so writeToPool
        // knows the controller speaks sync and stays in buffered mode.
        s_artsync_packets++;
        s_sync_last_ms = millis();
        s_swap_pending = true;
        // Intentionally NOT calling DMXLedRun() here — ArtSync packets are
        // frame-boundary markers, not actual DMX data. Some controllers
        // (Resolume in muted state, lighting consoles in standby) keep
        // emitting Sync even when their data output is dark, and that
        // would make dmxIsActive()/the UI dot stay green with no real
        // traffic. Only ArtDMX (the 0x5000 branch above) counts.
    } else if (opcode == 0x2000) {   // ArtPoll
        sendArtPollReply(packet.remoteIP());
    }
}

void initArtnet() {
    artnetUdp.close();
    if (!artnetUdp.listen(ARTNET_PORT)) {
        ESP_LOGE(TAG, "ArtNet listen(%d) failed", ARTNET_PORT);
        return;
    }
    artnetUdp.onPacket(onArtnetPacket);
    ESP_LOGI(TAG, "ArtNet UDP ready (port %d, %d universes)", ARTNET_PORT, universeCount);
}

uint32_t artnetPacketCount(void) { return s_artnetPackets; }

// Minimal ArtPollReply — lets Resolume/GrandMA/etc. discover the fixture.
// Parse "MAJOR.MINOR.PATCH" (e.g. "2.20.5") from FW_VERSION into two
// bytes for the ArtPollReply VersInfo field. Missing tokens → 0.
static void parseFwVersion(uint8_t& major, uint8_t& minor) {
    major = 0; minor = 0;
    const char* p = FW_VERSION;
    while (*p && *p >= '0' && *p <= '9') { major = major * 10 + (*p - '0'); p++; }
    if (*p == '.') {
        p++;
        while (*p && *p >= '0' && *p <= '9') { minor = minor * 10 + (*p - '0'); p++; }
    }
}

static void sendArtPollReply(const IPAddress& requester) {
    uint8_t reply[239] = {};
    memcpy(reply, "Art-Net\0", 8);
    reply[8] = 0x00; reply[9] = 0x21;   // OpPollReply = 0x2100 (LE)

    struct in_addr localIp = {};
    inet_aton(netConfig.currentip.c_str(), &localIp);
    memcpy(reply + 10, &localIp.s_addr, 4);    // IP (network order)
    reply[14] = 0x36; reply[15] = 0x19;         // port 6454 (LE)

    // VersInfo (offsets 16-17, big-endian: hi=major, lo=minor). Was
    // omitted entirely before — clients displayed "0.0" for firmware
    // version. Now parses FW_VERSION so the reply always matches the
    // build actually running.
    uint8_t fwMajor, fwMinor;
    parseFwVersion(fwMajor, fwMinor);
    reply[16] = fwMajor;
    reply[17] = fwMinor;

    // NetSwitch (18) + SubSwitch (19) — top nibbles of the 15-bit
    // universe number, so external controllers can filter to only the
    // net/subnet we're on.
    reply[18] = (uint8_t)((dmxConfig.startUniverse >> 8) & 0x7F);
    reply[19] = (uint8_t)((dmxConfig.startUniverse >> 4) & 0x0F);

    // OEM (20-21, big-endian) — Art-Net OEM code. Ravision doesn't hold
    // a registered OEM ID yet; leaving the "Unknown/custom" 0xFFFF is
    // preferable to spoofing another vendor's number.
    reply[20] = 0xFF; reply[21] = 0xFF;

    // Status1 (23) — bit 5 set = programmable via web UI.
    reply[23] = 0x20;

    // ESTA Manufacturer code (24-25, little-endian). Ravision is not
    // yet an ESTA member so we leave 0x0000 here — most clients render
    // it as "PLASA" (the standards body itself) as the fallback. When
    // Ravision picks up a real ESTA code, drop it in these two bytes
    // (LSB first).
    reply[24] = 0x00;
    reply[25] = 0x00;

    // ShortName (26-43, 18 bytes) — the device ID/serial, useful for
    // operators to spot fixtures in a controller's device list.
    strncpy((char*)reply + 26, setConfig.ID_fixture.c_str(), 17);
    // LongName (44-107, 64 bytes) — human name. "RavLight <fixture>"
    // gives the operator both the platform and the fixture flavour in
    // one line, without repeating the version (that's what VersInfo
    // is for).
    snprintf((char*)reply + 44, 64, "RavLight %s", PROJECT_NAME);
    // NodeReport (108-171) — "#xxxx [count] status" per ArtNet spec.
    snprintf((char*)reply + 108, 64, "#0001 [0000] RavLight %s ready", PROJECT_NAME);

    reply[172] = 0x00; reply[173] = 0x01;   // NumPorts = 1 (BE)
    reply[174] = 0x80;                       // PortTypes[0]: DMX output
    reply[182] = 0x80;                       // GoodOutput[0]
    reply[190] = (uint8_t)(dmxConfig.startUniverse & 0x0F);   // SwOut[0] — universe LSB

    uint8_t mac[6] = {};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    memcpy(reply + 201, mac, 6);
    memcpy(reply + 207, &localIp.s_addr, 4);   // BindIp
    reply[211] = 1;                             // BindIndex
    reply[212] = 0x08;                          // Status2: sACN capable

    artnetUdp.writeTo(reply, sizeof(reply), requester, ARTNET_PORT);
}

// ── sACN / E1.31 (raw lwip UDP socket, multicast per universe) ───────────────
// Supports non-consecutive universe ranges; each universe joins its own multicast group.
// Multicast address: 239.255.(universe>>8).(universe&0xFF) per ANSI E1.31-2018 §9.3.2.

#define SACN_PORT     5568
#define E131_HDR_SIZE 126   // header ends before DMX data (start code at 125, data at 126)

static int          sacnSock       = -1;
static TaskHandle_t sacnTaskHandle = NULL;

static const uint8_t E131_MAGIC[12] = {'A','S','C','-','E','1','.','1','7',0,0,0};

// universe parameter is E1.31 (1-indexed); internal pool IDs are 0-indexed.
// Callers must pass (pool_id + 1) so universe 0 in the UI → joins 239.255.0.1.
static void sacnJoinUniverse(uint16_t universe) {
    struct ip_mreq mreq = {};
    mreq.imr_multiaddr.s_addr = htonl(0xEFFF0000 | universe);
    mreq.imr_interface.s_addr = inet_addr(netConfig.currentip.c_str());
    if (setsockopt(sacnSock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
        ESP_LOGW(TAG, "sACN join u%d failed: %d", universe, errno);
}

// Dedicated FreeRTOS task on Core 0 — drains the sACN socket with a blocking
// recv() so packets are processed immediately, independent of the render loop.
// Mirrors artnetTaskFn; see that function for the rationale.
static void sacnTaskFn(void* pvParams) {
    static uint8_t buf[638];   // 126-byte header + up to 512 DMX channels
    // E1.31 root-layer Vectors (RFC E1.31 §5.2):
    //   0x00000004 = VECTOR_ROOT_E131_DATA       (regular DMX frame)
    //   0x00000008 = VECTOR_ROOT_E131_EXTENDED   (sync / discovery)
    // E1.31 framing-layer Vectors for EXTENDED:
    //   0x00000001 = VECTOR_E131_EXTENDED_SYNCHRONIZATION
    //   0x00000002 = VECTOR_E131_EXTENDED_DISCOVERY (ignored here)
    while (true) {
        int n = recv(sacnSock, buf, sizeof(buf), 0);
        if (n < 22) {
            vTaskDelay(1);  // timeout / socket closed / short packet — yield, retry
            continue;
        }
        if (memcmp(buf + 4, E131_MAGIC, 12) != 0) continue;

        uint32_t root_vec = ((uint32_t)buf[18] << 24) | ((uint32_t)buf[19] << 16) |
                            ((uint32_t)buf[20] << 8 ) |  buf[21];
        if (root_vec == 0x00000008) {
            // Extended packet — sync or discovery. Sync is 49 bytes; discovery
            // varies but we don't act on it here.
            if (n < 44) continue;
            uint32_t frame_vec = ((uint32_t)buf[40] << 24) | ((uint32_t)buf[41] << 16) |
                                 ((uint32_t)buf[42] << 8 ) |  buf[43];
            if (frame_vec == 0x00000001) {
                // sACN sync — same semantic as Art-Net 4 ArtSync: defer the
                // universe pool swap to dmxApplyPendingSwap() called from the
                // render task at frame boundary, so consumers see a coherent
                // multi-universe snapshot.
                s_sacnsync_packets++;
                s_sync_last_ms = millis();
                s_swap_pending = true;
                DMXLedRun();
            }
            continue;
        }
        if (root_vec != 0x00000004) continue;   // unknown root vector, drop

        if (n < E131_HDR_SIZE) { vTaskDelay(1); continue; }
        uint16_t e131_univ = (buf[113] << 8) | buf[114];
        uint16_t propCount = (buf[123] << 8) | buf[124];   // includes start code
        uint16_t size      = (propCount > 1) ? (uint16_t)(propCount - 1) : 0;
        if (size > 512 || n < E131_HDR_SIZE + (int)size) continue;
        // E1.31 universes are 1-indexed; normalize to 0-indexed to match ArtNet pool
        uint16_t universe = (e131_univ > 0) ? (uint16_t)(e131_univ - 1) : 0;
        if (dmxBufferMutex) {
            xSemaphoreTake(dmxBufferMutex, portMAX_DELAY);
            writeToPoolSacn(universe, buf + E131_HDR_SIZE, size);
            xSemaphoreGive(dmxBufferMutex);
        }
        DMXLedRun();
    }
}

void initE131() {
    // Kill previous task and socket if reinitializing
    if (sacnTaskHandle) { vTaskDelete(sacnTaskHandle); sacnTaskHandle = NULL; }
    if (sacnSock >= 0)  { close(sacnSock); sacnSock = -1; }

    sacnSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sacnSock < 0) { ESP_LOGE(TAG, "sACN socket() failed"); return; }

    int yes = 1;
    setsockopt(sacnSock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(SACN_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sacnSock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "sACN bind() failed: %d", errno);
        close(sacnSock); sacnSock = -1; return;
    }

    // Blocking socket with receive timeout: the task blocks at recv() until a
    // packet arrives. The 200 ms timeout lets the task notice socket closure.
    struct timeval tv = {0, 200000};
    setsockopt(sacnSock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    for (int i = 0; i < universeCount; i++)
        sacnJoinUniverse((uint16_t)(universePool[i].id + 1));  // pool is 0-indexed, E1.31 is 1-indexed

    xTaskCreatePinnedToCore(sacnTaskFn, "sacn_rx", 4096, NULL, 5,
                            &sacnTaskHandle, 0);  // Core 0: network / lwIP side

    ESP_LOGI(TAG, "sACN UDP ready (port %d, %d universes)", SACN_PORT, universeCount);
}

// E1.31 packet layout (ANSI E1.31-2018):
//   [4..15]   ACN Packet Identifier
//   [113..114] Universe (big-endian)
//   [123..124] Property Count including start code (big-endian)
//   [125]      Start Code (0x00 = null)
//   [126..]    DMX channel data
void get131DMX() {
    if (sacnSock < 0) return;
    static uint8_t buf[638];   // 126-byte header + up to 512 DMX channels
    int n;
    while ((n = recv(sacnSock, buf, sizeof(buf), 0)) >= E131_HDR_SIZE) {
        if (memcmp(buf + 4, E131_MAGIC, 12) != 0) continue;
        uint16_t e131_univ = (buf[113] << 8) | buf[114];
        uint16_t propCount = (buf[123] << 8) | buf[124];   // includes start code
        uint16_t size      = (propCount > 1) ? (uint16_t)(propCount - 1) : 0;
        if (size > 512 || n < E131_HDR_SIZE + (int)size) continue;
        // E1.31 universes are 1-indexed; normalize to 0-indexed to match ArtNet pool
        uint16_t universe = (e131_univ > 0) ? (uint16_t)(e131_univ - 1) : 0;
        if (dmxBufferMutex && xSemaphoreTake(dmxBufferMutex, portMAX_DELAY) == pdTRUE) {
            writeToPoolSacn(universe, buf + E131_HDR_SIZE, size);
            xSemaphoreGive(dmxBufferMutex);
        }
        DMXLedRun();
    }
}

// ── Status LED ────────────────────────────────────────────────────────────────

static bool ledState = false;
static unsigned long lastDMXReceivedTime = 0;
#define LED_TIMEOUT 1000
#define DMX_ACTIVE_WINDOW_MS 1500

bool dmxIsActive() {
    return lastDMXReceivedTime != 0 && (millis() - lastDMXReceivedTime) < DMX_ACTIVE_WINDOW_MS;
}

void DMXLedRun() {
    static unsigned long lastToggleTime = 0;
    const unsigned long toggleInterval = 100;
    if (millis() - lastToggleTime >= toggleInterval) {
        ledState = !ledState;
#ifdef RAVLIGHT_HAS_STATUS_LED
        digitalWrite(HW_PIN_LED_STATUS, ledState ? HIGH : LOW);
#endif
        lastToggleTime = millis();
    }
    lastDMXReceivedTime = millis();
}

// ── Core init / dispatch ─────────────────────────────────────────────────────

void initDmxInputs() {
    if (!dmxBufferMutex) dmxBufferMutex = xSemaphoreCreateMutex();
#ifdef RAVLIGHT_HAS_STATUS_LED
    pinMode(HW_PIN_LED_STATUS, OUTPUT);
    digitalWrite(HW_PIN_LED_STATUS, LOW);
#endif
    // Ensure startUniverse is always in the pool (single-universe fixture compat).
    registerDmxUniverse(dmxConfig.startUniverse);

    switch (dmxConfig.dmxInput) {
        case DMX_PHYSICAL:
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
            // Mutual exclusion safety: if the device is reading wired
            // DMX, it must never simultaneously try to transmit on the
            // same bus. An auto-direction transceiver briefly tipped
            // into TX during init has been observed to corrupt the
            // upstream master's signal (artifacts, frozen pixels on
            // every slave on the cable). Force the output flag off
            // here regardless of what NVS persisted.
            dmxConfig.dmxOutputEnabled = false;
            initWiredDmx();
#endif
            break;
        case ARTNET:
            initArtnet();
            s_artnet_gate = true;                // accept incoming packets
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
            if (dmxConfig.dmxOutputEnabled) initWiredDmx();
#endif
            break;
        case SACN:
            initE131();
            s_sacn_gate = true;                  // accept incoming packets
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
            if (dmxConfig.dmxOutputEnabled) initWiredDmx();
#endif
            break;
        case AUTO_SCENE:
#ifdef RAVLIGHT_MODULE_RECORDER
            startAutoScene(dmxConfig.autoSceneSlot);
#endif
            break;
        default:
            ESP_LOGW(TAG, "Invalid DMX input type");
            break;
    }
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL_2
    registerDmxUniverse(dmxConfig.startUniverse + 1);
    initWiredDmx2();
#endif
}

void receiveDmxData() {
    if (handleDMXenable) {
        switch (dmxConfig.dmxInput) {
            case DMX_PHYSICAL:
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
                getWiredDMX();
#endif
                break;
            case ARTNET:
                // ArtNet is drained by the dedicated artnetTaskFn task (Core 0).
                // Only forward to DMX physical output if enabled.
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
                sendDmxData();
#endif
                break;
            case SACN:
                // sACN is drained by the dedicated sacnTaskFn task (Core 0).
                // Only forward to DMX physical output if enabled.
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
                sendDmxData();
#endif
                break;
            default:
                break;
        }
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL_2
        getWiredDMX2();
#endif
    }
    if (millis() - lastDMXReceivedTime >= LED_TIMEOUT) {
        if (ledState) {
            ledState = false;
#ifdef RAVLIGHT_HAS_STATUS_LED
            digitalWrite(HW_PIN_LED_STATUS, LOW);
#endif
        }
    }
}

// ── Physical RS-485 DMX module ────────────────────────────────────────────────
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL

void initWiredDmx() {
    dmx_config_t dmx_config = DMX_CONFIG_DEFAULT;
    dmx_personality_t personalities[] = { {1, "Default Personality"} };
    int personality_count = 1;
    dmx_driver_install(dmxPort, &dmx_config, personalities, personality_count);
    dmx_set_pin(dmxPort, HW_PIN_DMX_TX, HW_PIN_DMX_RX, HW_PIN_DMX_EN);
    ESP_LOGI(TAG, "Wired DMX initialized");
}

void getWiredDMX() {
    dmx_packet_t packet;
    if (dmx_receive(dmxPort, &packet, DMX_TIMEOUT_TICK)) {
        if (!packet.err) {
            if (!dmxIsConnected) dmxIsConnected = true;
            if (dmxBufferMutex) xSemaphoreTake(dmxBufferMutex, portMAX_DELAY);
            dmx_read(dmxPort, dmxBuffer, packet.size);
            if (dmxBufferMutex) {
                writeToPool(dmxConfig.startUniverse, dmxBuffer + 1, packet.size - 1);
                xSemaphoreGive(dmxBufferMutex);
            }
            s_wiredPackets++;
            DMXLedRun();
        } else {
            ESP_LOGW(TAG, "DMX packet error");
        }
    } else if (dmxIsConnected) {
        ESP_LOGW(TAG, "DMX signal lost");
    }
}

void sendDmxData() {
    if (dmxConfig.dmxOutputEnabled && dmxConfig.dmxInput != DMX_PHYSICAL) {
        // Apply output channel offset — input ch (outOffset+1) becomes
        // wire ch 1, and we zero-pad the tail so wire channels past the
        // shifted window stay at 0 instead of leaking stale bytes from
        // the previous full-buffer push.
        const uint16_t off = dmxConfig.outOffset;
        if (off == 0) {
            dmx_write(dmxPort, dmxBuffer, DMX_BUFFER_SIZE);
        } else {
            static uint8_t wireBuf[DMX_BUFFER_SIZE];
            wireBuf[0] = 0;                                // null start code
            uint16_t copy = (off >= 512) ? 0 : (uint16_t)(512 - off);
            if (copy) memcpy(wireBuf + 1, dmxBuffer + 1 + off, copy);
            if (copy < 512) memset(wireBuf + 1 + copy, 0, 512 - copy);
            dmx_write(dmxPort, wireBuf, DMX_BUFFER_SIZE);
        }
        dmx_send_num(dmxPort, DMX_PACKET_SIZE);
        dmx_wait_sent(dmxPort, DMX_TIMEOUT_TICK);
    }
}

#endif // RAVLIGHT_MODULE_DMX_PHYSICAL

// ── Live reinit helpers ───────────────────────────────────────────────────────

// Live input-mode switch. The save handler invokes this whenever
// dmxConfig.dmxInput changes so the user doesn't have to wait for a
// 10 s restart on every DMX-source toggle. Tears down whatever was
// running (ArtNet socket / sACN socket+task / wired esp_dmx driver /
// auto-scene player) and brings the new path up.
//
// Wired-DMX safety: same mutual-exclusion rule as initDmxInputs — when
// the new input is DMX_PHYSICAL we force dmxOutputEnabled off (a slave
// must never transmit on the bus it's listening to). Otherwise the
// output is left as the user configured.
void reinitDMXInput() {
    if (!dmxBufferMutex) return;   // called before initDmxInputs() — skip

    // ── Tear down EVERYTHING first ────────────────────────────────────
    // Earlier this function only stopped the path it thought was about
    // to change; in practice that left e.g. the ArtNet listener bound
    // while we switched to Built-in Effects, and both wrote to the same
    // universe pool simultaneously. Now we kill all input sources
    // unconditionally and reopen only the one we need.
    //
    // Note: artnetUdp.close() on this AsyncUDP build doesn't
    // synchronously flush the packet queue — packets already dispatched
    // by lwIP still reach onArtnetPacket for a few ms afterwards. The
    // callback checks s_artnet_gate first and drops them, so setting
    // the gate low BEFORE close() eliminates the residual write path.
    s_artnet_gate = false;
    s_sacn_gate   = false;
    artnetUdp.close();
    if (sacnTaskHandle) { vTaskDelete(sacnTaskHandle); sacnTaskHandle = NULL; }
    // Zero every universe's active buffer + the legacy dmxBuffer while
    // the old source is torn down and before the new one has produced
    // its first frame. Otherwise renderers keep painting whatever the
    // previous source left in the pool for the ~50–100 ms transition
    // window — visible on strips as a brief garbage-coloured flash
    // ("glitch tra built-in e artnet").
    if (xSemaphoreTake(dmxBufferMutex, portMAX_DELAY) == pdTRUE) {
        for (int i = 0; i < universeCount; i++) {
            memset(universePool[i].data[0], 0, sizeof(universePool[i].data[0]));
            memset(universePool[i].data[1], 0, sizeof(universePool[i].data[1]));
            universePool[i].pending_dirty = 0;
        }
        memset(dmxBuffer, 0, DMX_BUFFER_SIZE);
        xSemaphoreGive(dmxBufferMutex);
    }
    if (sacnSock >= 0)  { close(sacnSock); sacnSock = -1; }
#ifdef RAVLIGHT_MODULE_RECORDER
    stopAutoScene();
#endif
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
    if (dmx_driver_is_installed(dmxPort)) {
        dmx_driver_delete(dmxPort);
    }
    dmxIsConnected = false;
#endif

    // ── Bring up new path ─────────────────────────────────────────────
    switch (dmxConfig.dmxInput) {
        case DMX_PHYSICAL:
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
            // Mutual exclusion: slave mode must not transmit.
            dmxConfig.dmxOutputEnabled = false;
            initWiredDmx();
#endif
            break;
        case ARTNET:
            initArtnet();
            s_artnet_gate = true;                // accept incoming packets
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
            if (dmxConfig.dmxOutputEnabled) initWiredDmx();
#endif
            break;
        case SACN:
            initE131();
            s_sacn_gate = true;                  // accept incoming packets
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
            if (dmxConfig.dmxOutputEnabled) initWiredDmx();
#endif
            break;
        case AUTO_SCENE:
#ifdef RAVLIGHT_MODULE_RECORDER
            startAutoScene(dmxConfig.autoSceneSlot);
#endif
            break;
        case EFFECTS:
            // The effects engine is always ticking from the main loop,
            // gated on dmxConfig.dmxInput == EFFECTS — no init needed
            // here, just leaving the network paths torn down so we don't
            // see external traffic during the test.
            break;
        default: break;
    }
    ESP_LOGI(TAG, "Input reinitialized: mode %d", dmxConfig.dmxInput);
}

void reinitUniverse(uint16_t universe) {
    registerDmxUniverse(universe);
    // For sACN, just join the new multicast group if socket is already open.
    // For ArtNet, INADDR_ANY receives all universes — no extra action needed.
    if (dmxConfig.dmxInput == SACN) {
        if (sacnSock >= 0)
            sacnJoinUniverse((uint16_t)(universe + 1));  // pool is 0-indexed, E1.31 is 1-indexed
        else
            initE131();
    }
    ESP_LOGI(TAG, "Universe reinitialized: %d", universe);
}

void reinitDMXOutput(bool enable) {
    ESP_LOGI(TAG, "Output %s", enable ? "enabled" : "disabled");
}

// ── Physical RS-485 DMX port 2 (UART0 / GPIO1/GPIO3) ─────────────────────────
// Requires RAVLIGHT_DISABLE_SERIAL — UART0 is shared with Serial debug output.
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL_2

void initWiredDmx2() {
    dmx_config_t dmx_config = DMX_CONFIG_DEFAULT;
    dmx_personality_t personalities[] = { {1, "Default Personality"} };
    int personality_count = 1;
    dmx_driver_install(dmxPort2, &dmx_config, personalities, personality_count);
    dmx_set_pin(dmxPort2, HW_PIN_DMX2_TX, HW_PIN_DMX2_RX, HW_PIN_DMX2_EN);
    ESP_LOGI(TAG, "Wired DMX port 2 initialized (UART0 GPIO%d/GPIO%d)",
             HW_PIN_DMX2_TX, HW_PIN_DMX2_RX);
}

void getWiredDMX2() {
    dmx_packet_t packet;
    if (dmx_receive(dmxPort2, &packet, DMX_TIMEOUT_TICK)) {
        if (!packet.err) {
            if (!dmxIsConnected2) dmxIsConnected2 = true;
            static uint8_t buf2[513];
            if (dmxBufferMutex) xSemaphoreTake(dmxBufferMutex, portMAX_DELAY);
            dmx_read(dmxPort2, buf2, packet.size);
            if (dmxBufferMutex) {
                writeToPool(dmxConfig.startUniverse + 1, buf2 + 1, packet.size - 1);
                xSemaphoreGive(dmxBufferMutex);
            }
            DMXLedRun();
        } else {
            ESP_LOGW(TAG, "DMX port 2 packet error");
        }
    } else if (dmxIsConnected2) {
        ESP_LOGW(TAG, "DMX port 2 signal lost");
    }
}

#endif // RAVLIGHT_MODULE_DMX_PHYSICAL_2
