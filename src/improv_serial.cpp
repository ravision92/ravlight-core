#include "improv_serial.h"

#if defined(RAVLIGHT_MODULE_IMPROV) && !defined(RAVLIGHT_DISABLE_SERIAL)

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "network_manager.h"
#include "serial_console.h"
#include "version.h"

// ── Improv-Serial protocol constants ─────────────────────────────────────────
static const uint8_t IMPROV_HEADER[6] = { 'I', 'M', 'P', 'R', 'O', 'V' };

// Packet types (byte after the version byte)
enum {
    TYPE_CURRENT_STATE = 0x01,   // device -> host
    TYPE_ERROR_STATE   = 0x02,   // device -> host
    TYPE_RPC           = 0x03,   // host   -> device
    TYPE_RPC_RESULT    = 0x04,   // device -> host
};

// Device states
enum {
    STATE_READY        = 0x02,   // authorized, awaiting credentials
    STATE_PROVISIONING = 0x03,
    STATE_PROVISIONED  = 0x04,
};

// Error codes
enum {
    ERR_NONE            = 0x00,
    ERR_INVALID_RPC     = 0x01,
    ERR_UNKNOWN_CMD     = 0x02,
    ERR_UNABLE_CONNECT  = 0x03,
    ERR_UNKNOWN         = 0xFF,
};

// RPC command ids (first data byte of a TYPE_RPC packet)
enum {
    CMD_SEND_WIFI  = 0x01,
    CMD_REQ_STATE  = 0x02,
    CMD_REQ_INFO   = 0x03,
    CMD_REQ_SCAN   = 0x04,
};

// ── Outgoing packet helpers ──────────────────────────────────────────────────
static void sendPacket(uint8_t type, const uint8_t* data, uint8_t len) {
    uint8_t p[300];
    uint16_t n = 0;
    for (int i = 0; i < 6; i++) p[n++] = IMPROV_HEADER[i];
    p[n++] = 0x01;   // protocol version
    p[n++] = type;
    p[n++] = len;
    for (uint8_t i = 0; i < len; i++) p[n++] = data[i];
    uint8_t chk = 0;
    for (uint16_t i = 0; i < n; i++) chk += p[i];
    p[n++] = chk;
    Serial.write(p, n);
    Serial.flush();
}

static void sendState(uint8_t st)  { sendPacket(TYPE_CURRENT_STATE, &st, 1); }
static void sendError(uint8_t err) { sendPacket(TYPE_ERROR_STATE,   &err, 1); }

// TYPE_RPC_RESULT = [command][result_len][ (strlen, bytes)* ]
static void sendRpcResult(uint8_t cmd, const String* strings, uint8_t count) {
    uint8_t data[256];
    uint16_t n = 0;
    data[n++] = cmd;
    uint16_t lenPos = n++;            // placeholder for the result-data length
    uint16_t start  = n;
    for (uint8_t s = 0; s < count; s++) {
        uint8_t sl = (uint8_t)strings[s].length();
        if (n + 1 + sl > sizeof(data)) break;
        data[n++] = sl;
        memcpy(&data[n], strings[s].c_str(), sl);
        n += sl;
    }
    data[lenPos] = (uint8_t)(n - start);
    sendPacket(TYPE_RPC_RESULT, data, (uint8_t)n);
}

static String deviceUrl() {
    if (WiFi.status() == WL_CONNECTED) return "http://" + WiFi.localIP().toString() + "/";
    return "http://rav" + setConfig.ID_fixture + ".local/";
}

// ── RPC handlers ─────────────────────────────────────────────────────────────
static void handleReqInfo() {
    String info[4] = { String(PROJECT_NAME), String(FW_VERSION),
                       String("ESP32"), "rav" + setConfig.ID_fixture };
    sendRpcResult(CMD_REQ_INFO, info, 4);
}

static void handleReqState() {
    bool provisioned = (WiFi.status() == WL_CONNECTED);
    sendState(provisioned ? STATE_PROVISIONED : STATE_READY);
    if (provisioned) {
        String url = deviceUrl();
        sendRpcResult(CMD_REQ_STATE, &url, 1);
    }
}

static void handleReqScan() {
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n && i < 30; i++) {
        String net[3] = {
            WiFi.SSID(i),
            String(WiFi.RSSI(i)),
            (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? String("NO") : String("YES"),
        };
        sendRpcResult(CMD_REQ_SCAN, net, 3);
    }
    WiFi.scanDelete();
    sendRpcResult(CMD_REQ_SCAN, nullptr, 0);   // empty result = end of list
}

static void handleSendWifi(const uint8_t* d, uint8_t dlen) {
    if (dlen < 2) { sendError(ERR_INVALID_RPC); return; }
    uint8_t ssidLen = d[0];
    if (1 + ssidLen + 1 > dlen) { sendError(ERR_INVALID_RPC); return; }
    uint8_t passLen = d[1 + ssidLen];
    if (2 + ssidLen + passLen > dlen) { sendError(ERR_INVALID_RPC); return; }

    String ssid, pass;
    for (uint8_t i = 0; i < ssidLen; i++) ssid += (char)d[1 + i];
    for (uint8_t i = 0; i < passLen; i++) pass += (char)d[2 + ssidLen + i];

    sendState(STATE_PROVISIONING);

    // Live test-connect so we only report success on real credentials. Keep AP
    // up alongside so a device provisioning from AP fallback stays reachable.
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(200);

    if (WiFi.status() == WL_CONNECTED) {
        netConfig.wifiSSID     = ssid;
        netConfig.wifiPassword = pass;
        netConfig.dhcp         = true;
        netConfig.currentip    = WiFi.localIP().toString();
        saveConfig();
        setMDNSHost(setConfig.ID_fixture);
        sendState(STATE_PROVISIONED);
        String url = deviceUrl();
        sendRpcResult(CMD_SEND_WIFI, &url, 1);   // ESP Web Tools shows "Visit device"
    } else {
        WiFi.disconnect();
        sendError(ERR_UNABLE_CONNECT);
    }
}

// ── Frame processing ─────────────────────────────────────────────────────────
static uint8_t  s_buf[300];
static uint16_t s_pos = 0;

static void processFrame(uint16_t total) {
    uint8_t sum = 0;
    for (uint16_t i = 0; i < total - 1; i++) sum += s_buf[i];
    if (sum != s_buf[total - 1]) return;          // bad checksum → ignore
    if (s_buf[7] != TYPE_RPC) return;             // only host RPC commands

    uint8_t  cmd     = s_buf[9];
    uint8_t  cmdLen  = s_buf[10];
    const uint8_t* cmdData = &s_buf[11];

    switch (cmd) {
        case CMD_SEND_WIFI: handleSendWifi(cmdData, cmdLen); break;
        case CMD_REQ_STATE: handleReqState();                break;
        case CMD_REQ_INFO:  handleReqInfo();                 break;
        case CMD_REQ_SCAN:  handleReqScan();                 break;
        default:            sendError(ERR_UNKNOWN_CMD);      break;
    }
}

// Front-door byte pump: matches the Improv header, then buffers a full frame.
// Any byte that isn't part of an Improv frame is forwarded to the text console.
static void parseByte(uint8_t b) {
    if (s_pos < 6) {
        if (b == IMPROV_HEADER[s_pos]) {
            s_buf[s_pos++] = b;
        } else {
            // Header broke: the tentatively-held bytes weren't Improv after all.
            for (uint16_t i = 0; i < s_pos; i++) serialConsoleFeedChar((char)s_buf[i]);
            s_pos = 0;
            if (b == IMPROV_HEADER[0]) s_buf[s_pos++] = b;   // could start a new header
            else serialConsoleFeedChar((char)b);
        }
        return;
    }
    if (s_pos >= sizeof(s_buf)) { s_pos = 0; return; }        // overflow guard
    s_buf[s_pos++] = b;
    if (s_pos >= 9) {
        uint16_t total = 10 + s_buf[8];                       // hdr6+ver+type+len + data + chk
        if (s_pos >= total) { processFrame(total); s_pos = 0; }
    }
}

void checkImprovSerial() {
    while (Serial.available()) parseByte((uint8_t)Serial.read());
}

#else   // module off or serial disabled
void checkImprovSerial() {}
#endif
