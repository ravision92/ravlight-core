#include "art_rdm.h"

#if defined(RAVLIGHT_MODULE_ARTRDM) && defined(RAVLIGHT_MODULE_DMX_PHYSICAL)

#include <esp_dmx.h>
#include <rdm/controller.h>
#include <esp_log.h>
#include <string.h>
#include "dmx_manager.h"

static const char* TAG = "ARTRDM";

extern dmx_port_t dmxPort;                       // owned by dmx_manager.cpp
void artnetSendReply(const uint8_t* data, size_t len, const IPAddress& ip);

// Art-Net RDM opcodes (little-endian on the wire)
#define OP_TOD_REQUEST  0x8000
#define OP_TOD_DATA     0x8100
#define OP_TOD_CONTROL  0x8200
#define OP_RDM          0x8300

// ArtRdm header length before the raw RDM packet begins: ID[8] OpCode[2]
// ProtVerHi/Lo[2] RdmVer[1] Filler1/2[2] Spare1-7[7] Net[1] Command[1]
// Address[1] = 25. Not independently packet-captured against a real
// Onyx/DMX Workshop session — matches the Art-Net 4 spec layout and is
// consistent with this file's already-verified ArtTodRequest offsets, but
// may need adjustment after real-world testing.
#define ARTRDM_HDR_LEN  25

// RDM wire-format start codes (not exposed as named macros by esp_dmx).
#define RDM_SC      0xCC
#define RDM_SUB_SC  0x01

#define ARTRDM_MAX_UIDS 32
#define ARTRDM_MSG_MAX  300      // largest ArtRdm datagram we buffer

typedef struct {
    IPAddress ip;
    uint16_t  len;
    uint8_t   buf[ARTRDM_MSG_MAX];
} art_rdm_msg_t;

static QueueHandle_t s_queue = nullptr;

// Set while an RDM transaction is driving the bus, so the DMX-output path can
// yield the port. (Integration with the bridge send path is a follow-up; for
// now it is a diagnostic flag other code may read.)
volatile bool g_artRdmBusy = false;

// ── ArtTodData: report the discovered RDM device table ───────────────────────
static void sendTodData(const IPAddress& ip, uint8_t net, uint8_t addr,
                        const rdm_uid_t* uids, int count) {
    if (count < 0) count = 0;
    if (count > ARTRDM_MAX_UIDS) count = ARTRDM_MAX_UIDS;

    uint8_t p[28 + ARTRDM_MAX_UIDS * 6] = {};
    memcpy(p, "Art-Net\0", 8);
    p[8]  = OP_TOD_DATA & 0xFF; p[9] = (OP_TOD_DATA >> 8) & 0xFF;
    p[10] = 0;   p[11] = 14;           // ProtVer 14
    p[12] = 0x01;                      // RdmVer (RDM STANDARD_V1_0)
    p[13] = 1;                         // Port (physical, deprecated -> 1)
    // 14-19 Spare
    p[20] = 1;                         // BindIndex
    p[21] = net;                       // Net (high 7 bits of the port address)
    p[22] = 0x00;                      // CommandResponse: 0x00 = TodFull
    p[23] = addr;                      // low 8 bits of the port address
    p[24] = (count >> 8) & 0xFF;       // UidTotal (BE)
    p[25] = count & 0xFF;
    p[26] = 1;                         // BlockCount
    p[27] = (uint8_t)count;            // UidCount in this packet

    for (int i = 0; i < count; i++) {
        uint8_t* u = &p[28 + i * 6];
        u[0] = (uids[i].man_id >> 8) & 0xFF;   // UID is MSB-first on the wire
        u[1] =  uids[i].man_id       & 0xFF;
        u[2] = (uids[i].dev_id >> 24) & 0xFF;
        u[3] = (uids[i].dev_id >> 16) & 0xFF;
        u[4] = (uids[i].dev_id >>  8) & 0xFF;
        u[5] =  uids[i].dev_id        & 0xFF;
    }
    artnetSendReply(p, 28 + count * 6, ip);
    ESP_LOGI(TAG, "ArtTodData -> %s : %d device(s), net=%u addr=%u",
             ip.toString().c_str(), count, net, addr);
}

// ArtTodRequest / ArtTodControl: run a full discovery on the wire, reply ToD.
static void handleTodRequest(const art_rdm_msg_t& m) {
    // Echo the requested port address back so the controller accepts the reply.
    uint8_t net  = (m.len > 21) ? m.buf[21] : 0;
    uint8_t addr = 0;
    if (m.buf[8] == (OP_TOD_REQUEST & 0xFF) && (m.buf[9] == (OP_TOD_REQUEST >> 8))) {
        uint8_t addCount = (m.len > 23) ? m.buf[23] : 0;   // ArtTodRequest AddCount
        if (addCount > 0 && m.len > 24) addr = m.buf[24];
    }

    rdm_uid_t uids[ARTRDM_MAX_UIDS];
    g_artRdmBusy = true;
    int found = rdm_discover_devices_simple(dmxPort, uids, ARTRDM_MAX_UIDS);
    g_artRdmBusy = false;
    ESP_LOGI(TAG, "discovery found %d device(s)", found);
    sendTodData(m.ip, net, addr, uids, found);
}

static uint16_t rdmChecksum(const uint8_t* p, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++) sum += p[i];
    return (uint16_t)sum;
}

// Wrap a raw RDM packet (as it appears on the wire, including its own 2-byte
// checksum) into an ArtRdm datagram and send it to `ip`.
static void sendArtRdmReply(const uint8_t* rdmPacket, size_t rdmLen, const IPAddress& ip) {
    uint8_t p[ARTRDM_HDR_LEN + 40] = {};   // header + up to a ~32-byte PD response
    if (rdmLen > sizeof(p) - ARTRDM_HDR_LEN) {
        ESP_LOGW(TAG, "ArtRdm reply truncated: %u bytes", (unsigned)rdmLen);
        rdmLen = sizeof(p) - ARTRDM_HDR_LEN;
    }
    memcpy(p, "Art-Net\0", 8);
    p[8] = OP_RDM & 0xFF; p[9] = (OP_RDM >> 8) & 0xFF;
    p[10] = 0; p[11] = 14;      // ProtVer 14
    p[12] = 0x01;               // RdmVer (RDM STANDARD_V1_0)
    // 13-24: Filler1/2, Spare1-7, Net, Command, Address — 0 is fine for a reply
    memcpy(p + ARTRDM_HDR_LEN, rdmPacket, rdmLen);
    artnetSendReply(p, ARTRDM_HDR_LEN + rdmLen, ip);
}

// Proxy a single GET request for a PID esp_dmx's controller module actually
// exposes. esp_dmx's controller API (rdm/controller/*) is typed per-PID —
// there is no generic "send raw RDM PDU, get raw response" call in this
// library version, despite some header comments implying broader PID
// coverage — so only PIDs with a real rdm_send_get_*() wrapper can be
// proxied this way. DEVICE_INFO covers the fields consoles ask for right
// after discovery (personality current/count, footprint, start address,
// sub-device/sensor count) — the ones showing blank/wrong before this.
static void proxyGet(const art_rdm_msg_t& m, const rdm_uid_t& destUid,
                     const rdm_uid_t& srcUid, uint8_t tn, uint16_t subDevice,
                     uint16_t pid) {
    uint8_t pd[32] = {};
    size_t  pdl = 0;
    rdm_ack_t ack = {};

    if (pid == RDM_PID_DEVICE_INFO) {
        rdm_device_info_t info = {};
        if (!rdm_send_get_device_info(dmxPort, &destUid, subDevice, &info, &ack) || ack.err) {
            ESP_LOGW(TAG, "ArtRdm proxy: GET DEVICE_INFO to %04x:%08x failed",
                     destUid.man_id, (unsigned)destUid.dev_id);
            return;
        }
        pd[0] = 1; pd[1] = 0;                                    // RDM proto ver 1.0
        pd[2] = (info.model_id >> 8) & 0xFF;         pd[3]  = info.model_id & 0xFF;
        pd[4] = (info.product_category >> 8) & 0xFF; pd[5]  = info.product_category & 0xFF;
        pd[6]  = (uint8_t)(info.software_version_id >> 24);
        pd[7]  = (uint8_t)(info.software_version_id >> 16);
        pd[8]  = (uint8_t)(info.software_version_id >>  8);
        pd[9]  = (uint8_t)(info.software_version_id);
        pd[10] = (info.footprint >> 8) & 0xFF;         pd[11] = info.footprint & 0xFF;
        pd[12] = info.personality.current;
        pd[13] = info.personality.count;
        pd[14] = (info.dmx_start_address >> 8) & 0xFF; pd[15] = info.dmx_start_address & 0xFF;
        pd[16] = (info.sub_device_count >> 8) & 0xFF;  pd[17] = info.sub_device_count & 0xFF;
        pd[18] = info.sensor_count;
        pdl = 19;
    } else if (pid == RDM_PID_SOFTWARE_VERSION_LABEL) {
        char label[33] = {};
        if (!rdm_send_get_software_version_label(dmxPort, &destUid, subDevice,
                                                 label, sizeof(label), &ack) || ack.err) {
            ESP_LOGW(TAG, "ArtRdm proxy: GET SOFTWARE_VERSION_LABEL to %04x:%08x failed",
                     destUid.man_id, (unsigned)destUid.dev_id);
            return;
        }
        pdl = strnlen(label, sizeof(pd));
        memcpy(pd, label, pdl);
    } else {
        ESP_LOGW(TAG, "ArtRdm proxy: PID 0x%04x not supported yet", pid);
        return;
    }

    // Build the raw RDM GET_COMMAND_RESPONSE ourselves — esp_dmx's typed
    // controller call already parsed the response for us, but ArtRdm needs
    // an actual RDM wire-format packet relayed back to the console.
    uint8_t r[24 + 32 + 2] = {};
    r[0] = RDM_SC;
    r[1] = RDM_SUB_SC;
    r[2] = (uint8_t)(24 + pdl);                     // message length (excl. checksum)
    // Destination = original requester (srcUid from the incoming request)
    r[3]  = (srcUid.man_id >> 8) & 0xFF;   r[4]  = srcUid.man_id & 0xFF;
    r[5]  = (uint8_t)(srcUid.dev_id >> 24); r[6]  = (uint8_t)(srcUid.dev_id >> 16);
    r[7]  = (uint8_t)(srcUid.dev_id >>  8); r[8]  = (uint8_t)(srcUid.dev_id);
    // Source = the device we actually queried (destUid from the incoming request)
    r[9]  = (destUid.man_id >> 8) & 0xFF;  r[10] = destUid.man_id & 0xFF;
    r[11] = (uint8_t)(destUid.dev_id >> 24); r[12] = (uint8_t)(destUid.dev_id >> 16);
    r[13] = (uint8_t)(destUid.dev_id >>  8); r[14] = (uint8_t)(destUid.dev_id);
    r[15] = tn;
    r[16] = (uint8_t)RDM_RESPONSE_TYPE_ACK;
    r[17] = 0;                                       // message count
    r[18] = (subDevice >> 8) & 0xFF; r[19] = subDevice & 0xFF;
    r[20] = (uint8_t)RDM_CC_GET_COMMAND_RESPONSE;
    r[21] = (pid >> 8) & 0xFF; r[22] = pid & 0xFF;
    r[23] = (uint8_t)pdl;
    memcpy(r + 24, pd, pdl);
    uint16_t sum = rdmChecksum(r, 24 + pdl);
    r[24 + pdl]     = (sum >> 8) & 0xFF;
    r[24 + pdl + 1] =  sum       & 0xFF;

    sendArtRdmReply(r, 24 + pdl + 2, m.ip);
    ESP_LOGI(TAG, "ArtRdm proxy: GET 0x%04x %04x:%08x -> %s ok (pdl=%u)",
             pid, destUid.man_id, (unsigned)destUid.dev_id,
             m.ip.toString().c_str(), (unsigned)pdl);
}

// ArtRdm: RDM transaction proxy. GET-only for now, and only for PIDs
// esp_dmx's controller module actually exposes (DEVICE_INFO,
// SOFTWARE_VERSION_LABEL) — see proxyGet(). Header offsets are best-effort
// (see ARTRDM_HDR_LEN above); discovery (ToD, above) is independently
// verified against real Onyx/DMX Workshop traffic, this path is not yet.
static void handleArtRdm(const art_rdm_msg_t& m) {
    constexpr size_t RDM_MIN_LEN = 24 + 2;   // header (no PD) + checksum
    if (m.len < ARTRDM_HDR_LEN + RDM_MIN_LEN) {
        ESP_LOGW(TAG, "ArtRdm packet too short (%u bytes)", m.len);
        return;
    }
    const uint8_t* r = m.buf + ARTRDM_HDR_LEN;
    if (r[0] != RDM_SC || r[1] != RDM_SUB_SC) {
        ESP_LOGW(TAG, "ArtRdm: not an RDM packet (SC=0x%02x SSC=0x%02x)", r[0], r[1]);
        return;
    }

    rdm_uid_t destUid, srcUid;
    destUid.man_id = ((uint16_t)r[3] << 8) | r[4];
    destUid.dev_id = ((uint32_t)r[5] << 24) | ((uint32_t)r[6] << 16) |
                     ((uint32_t)r[7] << 8) | r[8];
    srcUid.man_id  = ((uint16_t)r[9] << 8) | r[10];
    srcUid.dev_id  = ((uint32_t)r[11] << 24) | ((uint32_t)r[12] << 16) |
                     ((uint32_t)r[13] << 8) | r[14];
    uint8_t  tn        = r[15];
    uint16_t subDevice = ((uint16_t)r[18] << 8) | r[19];
    uint8_t  cc        = r[20];
    uint16_t pid       = ((uint16_t)r[21] << 8) | r[22];

    if (cc != RDM_CC_GET_COMMAND) {
        ESP_LOGW(TAG, "ArtRdm proxy: only GET implemented (cc=0x%02x pid=0x%04x)", cc, pid);
        return;
    }

    g_artRdmBusy = true;
    proxyGet(m, destUid, srcUid, tn, subDevice, pid);
    g_artRdmBusy = false;
}

static void artRdmTask(void*) {
    art_rdm_msg_t m;
    for (;;) {
        if (xQueueReceive(s_queue, &m, portMAX_DELAY) != pdTRUE) continue;
        uint16_t op = m.buf[8] | ((uint16_t)m.buf[9] << 8);
        switch (op) {
            case OP_TOD_REQUEST:
            case OP_TOD_CONTROL: handleTodRequest(m); break;
            case OP_RDM:         handleArtRdm(m);     break;
            default: break;
        }
    }
}

void artRdmInit() {
    if (s_queue) return;
    s_queue = xQueueCreate(4, sizeof(art_rdm_msg_t));
    if (!s_queue) { ESP_LOGE(TAG, "queue alloc failed"); return; }
    // Low priority, pinned to core 0 (with lwIP) so it never preempts render.
    xTaskCreatePinnedToCore(artRdmTask, "artrdm", 4096, nullptr, 3, nullptr, 0);
    ESP_LOGI(TAG, "Art-RDM proxy ready (man 0x0642, port %d)", dmxPort);
}

void artRdmHandlePacket(const uint8_t* buf, int len, const IPAddress& from) {
    if (!s_queue || len < 24 || len > ARTRDM_MSG_MAX) return;
    art_rdm_msg_t m;
    m.ip  = from;
    m.len = (uint16_t)len;
    memcpy(m.buf, buf, len);
    xQueueSend(s_queue, &m, 0);        // drop if full — controller will retry
}

#else   // module off
void artRdmInit() {}
void artRdmHandlePacket(const uint8_t*, int, const IPAddress&) {}
#endif
