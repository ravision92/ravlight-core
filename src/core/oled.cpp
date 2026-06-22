#ifdef RAVLIGHT_MODULE_OLED
// SSD1309 128×64 I²C status display.
//
// Layout (4 rows on 128×64, 6×10 + 6×8 fonts from u8g2):
//   y= 0..15  fixture + ID  — 9px font, bold-ish — "Axon RVEFA0"
//   y=16..30  IP address    — 8px font           — "192.168.2.63"
//   y=32..46  DMX bar + U   — 8px font + bar     — "DMX ▮▮▮▮▯▯  U0"
//   y=48..63  heap + temp   — small 6px font     — "heap 145k  24°C  127s"
//
// The bar is a live activity indicator: every received ArtNet packet bumps
// a hit counter; the bar fills proportionally to packets seen in the last
// refresh window and decays over a few frames so brief silences are
// visible.

#include "core/oled.h"
#include "core/oled_logo.h"
#include "config.h"
#include "dmx_manager.h"
#include "network_manager.h"
#include <U8g2lib.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

#ifdef RAVLIGHT_MODULE_TEMP
#include "temp_sensor.h"
#endif

#ifndef HW_PIN_OLED_SDA
#  error "RAVLIGHT_MODULE_OLED enabled but board does not define HW_PIN_OLED_SDA"
#endif

// SSD1306 init sequence — strict subset of SSD1309 commands. Tested
// working on the user's SSD1309 module (the 5 V boards with onboard LDO).
// If a future SSD1309 panel stays dark with this driver, switch to
// U8G2_SSD1309_128X64_NONAME0_F_HW_I2C, which sends the 1309-specific
// charge-pump enable sequence.
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C s_u8g2(U8G2_R0, /*reset=*/U8X8_PIN_NONE);

static bool     s_ok             = false;   // false after a failed init; subsequent ticks no-op
static uint32_t s_last_draw_ms   = 0;
static uint32_t s_splash_until_ms = 0;       // tickOled() suppressed until then so the
                                             // boot screen stays readable instead of being
                                             // overwritten on the first loop iteration
static uint32_t s_last_pkts      = 0;       // ArtNet packet counter snapshot at previous tick
static uint8_t  s_bar_level      = 0;       // 0..6 filled bar segments
static char     s_diag[96]       = "(not-initialised)";   // /api/i2c diagnostic line

const char* oledDiag() { return s_diag; }

// Minimal XLR-3 male connector icon ~10×10 px — outer ring + 3 pin dots in
// the standard pin-1-on-top arrangement. Drawn programmatically so it
// scales cleanly across u8g2 rotations.
static void drawXlrIcon(uint8_t cx, uint8_t cy) {
    s_u8g2.drawCircle(cx, cy, 5);
    s_u8g2.drawDisc(cx,     cy - 2, 1);   // pin 1 (top)
    s_u8g2.drawDisc(cx - 2, cy + 1, 1);   // pin 2 (bottom-left)
    s_u8g2.drawDisc(cx + 2, cy + 1, 1);   // pin 3 (bottom-right)
}

// Right-pointing arrow ~10×6 px centred at (cx, cy). The default u8g2
// fonts ship without an ASCII arrow glyph, so we draw one — a shaft plus
// two angled head segments meeting at the tip.
static void drawArrowRight(uint8_t cx, uint8_t cy) {
    s_u8g2.drawHLine(cx - 5, cy, 9);            // shaft
    s_u8g2.drawLine (cx + 4, cy, cx + 1, cy - 3);
    s_u8g2.drawLine (cx + 4, cy, cx + 1, cy + 3);
}

// Map dmxInput enum → 6-char-or-less source label for the mode row.
static const char* sourceLabel(uint8_t input) {
    switch (input) {
        case 1: return "DMX";      // DMX_PHYSICAL
        case 2: return "ArtNet";
        case 3: return "sACN";
        case 4: return "Scene";    // AUTO_SCENE (recorder playback)
        case 5: return "FX";       // EFFECTS
        default: return "?";
    }
}

void initOled() {
    Wire.begin(HW_PIN_OLED_SDA, HW_PIN_OLED_SCL, 400000);

    // Full bus scan — record every responding address into the diagnostic
    // string so we can introspect from the web UI when the display stays
    // blank. ESP_LOGI mirror so it's also in the serial log.
    char list[64] = {0};
    int  found    = 0;
    uint8_t firstAddr = 0;
    for (uint8_t a = 0x08; a <= 0x77; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            char tmp[8];
            snprintf(tmp, sizeof(tmp), "%s0x%02X", found ? "," : "", a);
            strncat(list, tmp, sizeof(list) - strlen(list) - 1);
            if (!found) firstAddr = a;
            found++;
        }
    }
    if (found == 0) {
        snprintf(s_diag, sizeof(s_diag), "no I2C devices found on SDA=%d SCL=%d",
                 HW_PIN_OLED_SDA, HW_PIN_OLED_SCL);
        return;
    }

    // Pick 0x3C if present (most common), else 0x3D, else the first hit
    // we saw — covers oddball address-strapped modules.
    uint8_t addr = 0;
    if (strstr(list, "0x3C")) addr = 0x3C;
    else if (strstr(list, "0x3D")) addr = 0x3D;
    else addr = firstAddr;
    s_u8g2.setI2CAddress(addr << 1);

    if (!s_u8g2.begin()) {
        snprintf(s_diag, sizeof(s_diag), "u8g2.begin() failed; bus had: %s", list);
        return;
    }
    s_u8g2.setBusClock(400000);
    s_u8g2.setContrast(200);
    s_u8g2.clearBuffer();
    // RavLight wordmark centred on the upper third. The 128×19 bitmap is
    // exactly display-width, so x=0 is its natural left edge; vertically
    // we leave 12 px above for breathing room and 33 px below for the
    // fixture identity + status line.
    const uint8_t logo_y = 12;
    s_u8g2.drawXBM(0, logo_y, RAVLIGHT_LOGO_W, RAVLIGHT_LOGO_H, RAVLIGHT_LOGO_BITS);

    // Below the logo: fixture name centred, then version + "booting…"
    // small font under it.
    s_u8g2.setFont(u8g2_font_helvB10_tr);
    int nameW = s_u8g2.getStrWidth(PROJECT_NAME);
    s_u8g2.drawStr((128 - nameW) / 2, logo_y + RAVLIGHT_LOGO_H + 13, PROJECT_NAME);

    s_u8g2.setFont(u8g2_font_5x7_tr);
    char vbuf[24];
    snprintf(vbuf, sizeof(vbuf), "%s · booting…", FW_VERSION);
    int vW = s_u8g2.getStrWidth(vbuf);
    s_u8g2.drawStr((128 - vW) / 2, 63, vbuf);

    s_u8g2.sendBuffer();
    s_ok = true;
    // Hold the splash for ~3 s so it's actually readable — without this,
    // the first tickOled() call from the main loop overwrites it within
    // a single iteration.
    s_splash_until_ms = millis() + 3000;
    snprintf(s_diag, sizeof(s_diag), "ok @ 0x%02X (bus: %s)", addr, list);
}

void tickOled() {
    if (!s_ok) return;
    uint32_t now = millis();
    if (s_splash_until_ms && now < s_splash_until_ms) return;
    if (now - s_last_draw_ms < 250) return;
    s_last_draw_ms = now;

    // ── Build all rows ─────────────────────────────────────────────────────
    char l_id[24], l_ip[24], l_mode[20], l_univ[12], l_stats[40];

    snprintf(l_id, sizeof(l_id), "%s %s", PROJECT_NAME, setConfig.ID_fixture.c_str());

    const char* ip   = netConfig.currentip.length() ? netConfig.currentip.c_str() : "no link";
    const char* mode = getConnectionMode();      // "ETH" / "WiFi" / "AP-WiFi" / "OFF"
    snprintf(l_ip, sizeof(l_ip), "%s %s", mode, ip);

    // Mode row text — source label only ("ArtNet"); the destination side
    // is the XLR icon (DMX wire) when dmxOutputEnabled, drawn below.
    snprintf(l_mode, sizeof(l_mode), "%s", sourceLabel((uint8_t)dmxConfig.dmxInput));
    snprintf(l_univ, sizeof(l_univ), "UNVRS %u", (unsigned)dmxConfig.startUniverse);

    // DMX activity tracking — still drives the live blinking dot in the
    // stats row. We compare cumulative ArtNet packet count against the
    // previous tick: any non-zero delta means the bridge saw fresh traffic
    // in the last 250 ms.
    uint32_t pkts  = artnetPacketCount();
    uint32_t delta = (pkts >= s_last_pkts) ? (pkts - s_last_pkts) : 0;
    s_last_pkts    = pkts;
    bool dmxLive   = (delta > 0) || dmxIsActive();
    s_bar_level    = dmxLive ? (uint8_t)(s_bar_level ^ 1u) : 0;   // 0/1 blink toggle

    // Stats row — heap + uptime (+ temp if compiled in)
    uint32_t heap_k = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
    uint32_t up_s   = (uint32_t)(esp_timer_get_time() / 1000000ULL);
#ifdef RAVLIGHT_MODULE_TEMP
    snprintf(l_stats, sizeof(l_stats), "%uk %.0fC %us",
             (unsigned)heap_k, getTemperature(), (unsigned)up_s);
#else
    snprintf(l_stats, sizeof(l_stats), "%uk %us", (unsigned)heap_k, (unsigned)up_s);
#endif

    // ── Render ─────────────────────────────────────────────────────────────
    s_u8g2.clearBuffer();

    // Row 1 (y0..12): fixture + ID — bold
    s_u8g2.setFont(u8g2_font_helvB10_tr);
    s_u8g2.drawStr(0, 11, l_id);

    // Row 2 (y14..23): IP
    s_u8g2.setFont(u8g2_font_6x10_tr);
    s_u8g2.drawStr(0, 23, l_ip);

    // Separator line
    s_u8g2.drawHLine(0, 26, 128);

    // Row 3 (y29..44): mode → XLR icon + universe — the visual focus.
    // Layout left-to-right: source label, arrow glyph, XLR (or "LED"
    // when bridge is disabled), UNVRS N right-aligned.
    s_u8g2.setFont(u8g2_font_helvB10_tr);
    s_u8g2.drawStr(0, 42, l_mode);
    int modeW = s_u8g2.getStrWidth(l_mode);
    drawArrowRight((uint8_t)(modeW + 8), 37);
    if (dmxConfig.dmxOutputEnabled) {
        drawXlrIcon((uint8_t)(modeW + 23), 37);
    } else {
        s_u8g2.setFont(u8g2_font_6x10_tr);
        s_u8g2.drawStr(modeW + 16, 42, "LED");
        s_u8g2.setFont(u8g2_font_helvB10_tr);
    }
    s_u8g2.setFont(u8g2_font_6x10_tr);
    int uW = s_u8g2.getStrWidth(l_univ);
    s_u8g2.drawStr(128 - uW, 42, l_univ);

    // Row 4 (y50..63): stats on the left, DMX status pill on the right —
    // mirrors the web UI's `.hdr-chip`:
    //   DMX traffic     → pill shows "DMX",    inverse-flashes each tick
    //   online, no DMX  → pill shows "NO DMX", static outline
    // "Real DMX" means ArtDMX/sACN data packets in the last ~1.5 s —
    // ArtSync-only traffic from a muted controller doesn't count.
    s_u8g2.setFont(u8g2_font_5x7_tr);
    s_u8g2.drawStr(0, 62, l_stats);

    const char* dmxLbl = dmxLive ? "DMX" : "NO DMX";
    const int card_w = 34;            // sized to fit "NO DMX" + padding
    const int card_h = 13;
    const int card_x = 128 - card_w;
    const int card_y = 64 - card_h;
    const int card_r = 2;
    bool flash_on = dmxLive && (s_bar_level & 1);
    if (flash_on) {
        s_u8g2.drawRBox(card_x, card_y, card_w, card_h, card_r);
        s_u8g2.setDrawColor(0);                              // inverted text
    } else {
        s_u8g2.drawRFrame(card_x, card_y, card_w, card_h, card_r);
    }
    int textW = s_u8g2.getStrWidth(dmxLbl);
    s_u8g2.drawStr(card_x + (card_w - textW) / 2, card_y + 9, dmxLbl);
    s_u8g2.setDrawColor(1);                                  // restore

    s_u8g2.sendBuffer();
}

#endif // RAVLIGHT_MODULE_OLED
