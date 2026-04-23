<img src="images/RavLightLogo.png" alt="RavLight Logo" width="300">

# RavLight Core

> Modular open-source firmware for networked stage lighting nodes — think WLED but for professional DMX fixtures.

Built on ESP32 with native ArtNet, sACN/E1.31 and physical DMX512 support. Every feature is a compile-time flag, so you ship only what the hardware actually needs.

---

## Hardware

| Board | MCU | Notes |
|-------|-----|-------|
| **XDMX rev2.2** | ESP32 (WT32-ETH01) | LAN8720 Ethernet onboard, HW-519 RS-485 DMX, 74HCT245D 5V level shifter |

**Veyron fixture** — reference pixel bar:
- 40× WS2811 RGB pixels (pin 5)
- 2× P9813 accent pixels / 6× cool-white (data pin 2, clock pin 4)
- LM35 temperature sensor (pin 32)

---

## Architecture — three tiers

```
Core          always compiled       networking, ArtNet/sACN, webserver, NVS, discovery slave
MODULE_*      opt-in features       DMX physical, recorder, temperature, reset button
FIXTURE_*     hardware personality  Veyron 40+2px, DMX personalities, strobe, highlight
```

Board capabilities and pin assignments live in `boards/<name>.h` and are force-included at compile time — porting to a new board requires only a new header file and a new PlatformIO env.

---

## Quick start

**Requirements:** PlatformIO Core or IDE extension, ESP32 toolchain.

```bash
git clone https://github.com/Ravision92/ravlight-core.git
cd ravlight-core

# build
pio run -e xdmx_v2_veyron

# flash firmware + SPIFFS (web UI)
pio run -e xdmx_v2_veyron --target upload
pio run -e xdmx_v2_veyron --target uploadfs

# serial monitor
pio device monitor
```

First boot → device starts in **SoftAP mode**. Connect to the AP and open `192.168.4.1` to configure network, DMX universe, and start address.

---

## Web UI

- Network, DMX, Fixture, Settings — accordion layout
- Live parameter updates (no restart for DMX/fixture changes)
- Config export / import via JSON file
- OTA firmware update via `/update`

---

## Libraries

`FastLED` · `esp_dmx` · `ArtNet (hideakitai)` · `ESPAsyncE131` · `ESPAsyncWebServer` · `ElegantOTA` · `ArduinoJson` · `SPIFFS`

---

## Status

| Feature | State |
|---------|-------|
| Modular Core + `env:xdmx_v2_veyron` build | ✅ |
| ArtNet + sACN + physical DMX IN/OUT | ✅ |
| UDP + ESP-NOW discovery (slave) | ✅ |
| Board file pattern (`boards/`) | ✅ |
| Config JSON v2 with auto-migration | ✅ |
| DMX Recorder (record + playback) | 🔄 in progress |
| NFC provisioning | 📋 planned |
| React Native app | 📋 planned |
| XDMX rev3 (integrated PCB) | 📋 planned |

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

One module = one `.h` + `.cpp` pair under `include/` and `src/`.
Every feature behind an `#ifdef`. No hardcoded cross-module dependencies.

---

## License

RavLight Core is dual-licensed:

- **AGPLv3** — free for open-source and personal use (see [LICENSE](LICENSE))
- **Commercial license** — required for closed-source or commercial products (see [DUAL LICENSE.md](DUAL%20LICENSE.md))
