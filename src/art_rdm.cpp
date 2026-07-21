#include "art_rdm.h"

#if defined(RAVLIGHT_MODULE_ARTRDM) && defined(RAVLIGHT_MODULE_DMX_PHYSICAL)

#include <esp_dmx.h>
#include <rdm/controller.h>
#include <esp_log.h>
#include "dmx_manager.h"

static const char* TAG = "ARTRDM";

extern dmx_port_t dmxPort;                       // owned by dmx_manager.cpp
void artnetSendReply(const uint8_t* data, size_t len, const IPAddress& ip);

// Art-Net RDM opcodes (little-endian on the wire)
#define OP_TOD_REQUEST  0x8000
#define OP_TOD_DATA     0x8100
#define OP_TOD_CONTROL  0x8200
#define OP_RDM          0x8300

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

// ArtRdm: transparent RDM transaction proxy. NOT YET IMPLEMENTED — a correct
// proxy forwards the raw RDM PDU (prepend 0xCC start code) onto the wire and
// wraps the raw response back into ArtRdm. esp_dmx's high-level rdm_send_request
// re-encodes parameters, so this needs the low-level raw send/receive path plus
// spec-exact ArtRdm framing. Discovery (above) already lets a console SEE the
// devices; per-device GET/SET is the next step.
static void handleArtRdm(const art_rdm_msg_t& m) {
    ESP_LOGW(TAG, "ArtRdm from %s (%u bytes) — proxy not yet implemented",
             m.ip.toString().c_str(), m.len);
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
