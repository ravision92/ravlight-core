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

struct universe_slot_t {
    uint16_t id;
    uint8_t  data[513];   // data[0] unused; data[1..512] = DMX channels 1-512
};
static universe_slot_t universePool[DMX_MAX_UNIVERSES];
static uint8_t         universeCount = 0;

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
    for (int i = 0; i < universeCount; i++)
        if (universePool[i].id == universe) return universePool[i].data;
    return nullptr;
}

// ArtNet data is 0-indexed (ch1 at src[0]); pool is 1-indexed (ch1 at data[1]).
static void writeToPool(uint16_t universe, const uint8_t* src, uint16_t size) {
    uint16_t n = (size > 512) ? 512 : size;
    for (int i = 0; i < universeCount; i++) {
        if (universePool[i].id != universe) continue;
        memcpy(universePool[i].data + 1, src, n);
        if (universe == dmxConfig.startUniverse)
            memcpy(dmxBuffer + 1, src, n);
        return;
    }
}

static void writeToPoolSacn(uint16_t universe, const uint8_t* src, uint16_t size) {
    writeToPool(universe, src, size);
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
static void sendArtPollReply(const IPAddress& requester) {
    uint8_t reply[239] = {};
    memcpy(reply, "Art-Net\0", 8);
    reply[8] = 0x00; reply[9] = 0x21;   // OpPollReply = 0x2100 (LE)

    struct in_addr localIp = {};
    inet_aton(netConfig.currentip.c_str(), &localIp);
    memcpy(reply + 10, &localIp.s_addr, 4);    // IP (network order)
    reply[14] = 0x36; reply[15] = 0x19;         // port 6454 (LE)
    reply[20] = 0xFF; reply[21] = 0x00;         // OEM 0x00FF

    strncpy((char*)reply + 26, setConfig.ID_fixture.c_str(), 17);   // ShortName
    snprintf((char*)reply + 44, 64, "Ravision %s %s", PROJECT_NAME, FW_VERSION);
    snprintf((char*)reply + 108, 64, "#0001 [0000] OK");

    reply[172] = 0x00; reply[173] = 0x01;   // NumPorts = 1 (BE)
    reply[174] = 0x80;                       // PortTypes[0]: DMX output
    reply[182] = 0x80;                       // GoodOutput[0]
    reply[190] = (uint8_t)(dmxConfig.startUniverse & 0xFF);   // SwOut[0]

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
    while (true) {
        int n = recv(sacnSock, buf, sizeof(buf), 0);
        if (n < E131_HDR_SIZE) {
            vTaskDelay(1);  // timeout / socket closed / short packet — yield, retry
            continue;
        }
        if (memcmp(buf + 4, E131_MAGIC, 12) != 0) continue;
        uint16_t universe  = (buf[113] << 8) | buf[114];
        uint16_t propCount = (buf[123] << 8) | buf[124];   // includes start code
        uint16_t size      = (propCount > 1) ? (uint16_t)(propCount - 1) : 0;
        if (size > 512 || n < E131_HDR_SIZE + (int)size) continue;
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
        sacnJoinUniverse(universePool[i].id);

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
        uint16_t universe  = (buf[113] << 8) | buf[114];
        uint16_t propCount = (buf[123] << 8) | buf[124];   // includes start code
        uint16_t size      = (propCount > 1) ? (uint16_t)(propCount - 1) : 0;
        if (size > 512 || n < E131_HDR_SIZE + (int)size) continue;
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
            initWiredDmx();
#endif
            break;
        case ARTNET:
            initArtnet();
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
            if (dmxConfig.dmxOutputEnabled) initWiredDmx();
#endif
            break;
        case SACN:
            initE131();
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
        dmx_write(dmxPort, dmxBuffer, DMX_BUFFER_SIZE);
        dmx_send_num(dmxPort, DMX_PACKET_SIZE);
        dmx_wait_sent(dmxPort, DMX_TIMEOUT_TICK);
    }
}

#endif // RAVLIGHT_MODULE_DMX_PHYSICAL

// ── Live reinit helpers ───────────────────────────────────────────────────────

void reinitDMXInput() {
    if (!dmxBufferMutex) return;   // called before initDmxInputs() — skip
    // ArtNet uses AsyncUDP — initArtnet() closes and re-listens internally.
    // sACN still uses a BSD socket + task: kill the task before closing the fd.
    if (sacnTaskHandle) { vTaskDelete(sacnTaskHandle); sacnTaskHandle = NULL; }
    if (sacnSock   >= 0) { close(sacnSock); sacnSock = -1; }
#ifdef RAVLIGHT_MODULE_RECORDER
    stopAutoScene();
#endif
    switch (dmxConfig.dmxInput) {
        case ARTNET:      initArtnet(); break;
        case SACN:        initE131();   break;
        case AUTO_SCENE:
#ifdef RAVLIGHT_MODULE_RECORDER
            startAutoScene(dmxConfig.autoSceneSlot);
#endif
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
            sacnJoinUniverse(universe);
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
