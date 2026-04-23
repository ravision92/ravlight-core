# Changelog — RavLight Hardware

Hardware findings, PCB decisions, component validations and wiring fixes.
Firmware changes are tracked in [`CHANGELOG`](CHANGELOG).

---

## [Unreleased]

### Planned
- XDMX rev3: ESP32 + LAN8720 on single integrated PCB (no separate WT32-ETH01 module)
- Modular expansion boards: motor drivers, additional LED outputs, relay modules
- Display touch module (optional, plug-in)

---

## [XDMX rev2.2] — 2025-01-12

### Specification
- MCU: ESP32 via WT32-ETH01 module (LAN8720 Ethernet onboard)
- Physical DMX: HW-519 RS-485 module (auto-direction, no DE/RE pin required)
- Power: MP1584EN regulator (black version)
- Level shifter: 74HCT245D — 8× GPIO converted to 5 V output
- Reset: physical push button on GPIO 36
- GPIO: 4× sockets, 10× usable pins (2 input-only / 7 output-only / 1 bidirectional)
- Flash/debug socket on board

### Pin assignments (compile-time, defined in `settings.h`)
## Pin map XDMX (WT32-ETH01)
| Funzione         | Pin |
|------------------|-----|
| DMX TX           | 33  |
| DMX RX           | 35  |
| LED status       | 17  |
| Reset button     | 36  |
| ETH PHY POWER    | 16  |
| ETH PHY MDC      | 23  |
| ETH PHY MDIO     | 18  |
| WS2811 strip     | 5   |
| P9813 data       | 2   |
| P9813 clock      | 4   |
| LM35 temp sensor | 32  |

## Pin map Veyron 
| Funzione         | Pin |
|------------------|-----|
| WS2811 strip     | 5   |
| P9813 data       | 2   |
| P9813 clock      | 4   |
| LM35 temp sensor | 32  |
---

## [Veyron fixture] — 2025-04-05

### Specification
- Pixel bar with XDMX rev2.2
- Main strip: WS2811 RGB, 20 px/m COB, 40 pixels on pin 5
- Accent: 2× P9813 pixels on pin 2 (data) / pin 4 (clock) — drives 6×3 cool-white strip LEDs
- Temperature sensor: LM35 on pin 32

---

## [2.2.2] — 2025-05-26

### Known Issue — Fixed
- **Ethernet link LED logic inverted** on WT32-ETH01
  - LAN8720 LINK pin is active-low (pull-up, goes to GND when link is active)
  - Fix: wired LED cathode to LINK pin with 330 Ω series resistor (common-cathode configuration)

---

## [2.2.1] — 2025-11-19

### Research
- ESP32 I2S peripheral evaluated as LED signal backend vs RMT / bit-banging
- Enables up to 16 independent parallel LED outputs
- Decision: keep RMT for current hardware; revisit I2S for future multi-output expansion board

---

## [2.2.1] — 2025-02-19
- Wrong 5v power wiring of HW-519 require cut traces and cable joint to feed 3.3v  

---

## [2.2.0] — 2025-01-12

### Validated
- **HW-519 RS-485 module** confirmed compatible with ESP32 for physical DMX
  - Auto-direction hardware: no DE/RE pin management needed in firmware
  - Wiring: VCC, GND, TXD, RXD + A/B differential bus

---

## [1.1.0] — 2024-10-23

### Added
- Physical device label designed for silkscreen on Ravision XBar PCB
- Hardware specification document: operating voltage, connectivity, power consumption

---

## [1.0.0] — 2024-03-04

### Initial Prototype
- LAN8720 PHY confirmed on WT32-ETH01 module
- Simultaneous Ethernet + Wi-Fi STA + SoftAP validated