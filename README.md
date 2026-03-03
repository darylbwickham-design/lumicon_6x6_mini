# 6×6 Mini with LCD (Lumi-Con) — v1.0.0

A compact **Lumia-first** 6×6 control deck using:
- **Raspberry Pi Pico (RP2040)** for 6×6 matrix scanning (UART packets)
- **ESP8266** for Wi‑Fi + HTTP + **ST7735 TFT** display + event relay to Lumia
- **Lumia plugin (v5.2)** for receiving events and triggering alerts + sending TFT text

This repository is the “mini 6x6” device project (separate from the main Lumi‑Con).

---

## What it does

### Inputs (device → Lumia)
- Pico scans 36 keys and sends UART packets to the ESP8266.
- ESP8266 converts press/release into:
  - **Short press**: events `0–35`
  - **Long press**: events `36–71` (key + 36)
- ESP8266 sends events to the Lumia plugin:
  - `POST http://<PC_IP>:<PORT>/event`

### Display (Lumia → device)
The ESP8266 exposes TFT endpoints:
- `GET /msg?t=...`
- `GET /status?t=...`
- `GET /clear`
- `POST /ui` (JSON)
- `GET /health`

---

## Repo contents

### Firmware
- Pico: `6x6_test_pico.ino` (source) and `6x6_test_pico.ino.uf2` (easy flash)
- ESP8266: `lumi_con_esp_integrated_0_0_4.ino` (ST7735 + partial redraw performance)

### Lumia plugin (v5.2)
- `manifest.json`
- `main.js`
- `package.json` / `package-lock.json`
- `icon.png`

### 3D print files (STL)
- `6X6 Front.stl`
- `6X6 Rear.stl`
- `connector leg.stl`
- `controller front with lcd.stl`
- `electronics rear.stl`

---

## Hardware wiring

IMPORTANT (ESP flashing): disconnect **Pico GP0 (TX) → ESP RX (GPIO3)** while uploading firmware to the ESP8266.

### A) Pico ↔ 6×6 matrix (matches `6x6_test_pico.ino`)
Rows (inputs w/ pullups):
- R0 → GP12
- R1 → GP1
- R2 → GP2
- R3 → GP3
- R4 → GP4
- R5 → GP5

Cols (outputs):
- C0 → GP6
- C1 → GP7
- C2 → GP8
- C3 → GP9
- C4 → GP10
- C5 → GP11

### B) Pico → ESP8266 UART
- Pico GP0 (TX) → ESP RX / GPIO3
- Pico GND → ESP GND
- Optional: 1k resistor in series on TX line

### C) ESP8266 → ST7735 TFT (SPI)
Typical ESP8266 hardware SPI wiring:
- TFT VCC  → 3V3
- TFT GND  → GND
- TFT SCK  → D5 (GPIO14)
- TFT MOSI → D7 (GPIO13)
- TFT CS   → D2 (GPIO4)
- TFT DC   → D1 (GPIO5)

Recommended reset wiring:
- TFT RST → **ESP RST** (hardware reset pin)

Note:
- The ESP firmware uses `TFT_RST = -1` (RST not controlled by a GPIO). Wiring TFT RST to ESP RST is the most reliable option.

---

## Arduino IDE setup (one-time)

1) Install Arduino IDE:
- https://www.arduino.cc/en/software

2) Add board manager URLs:
Arduino IDE → File → Preferences → Additional Boards Manager URLs

Paste:
- ESP8266:
  - http://arduino.esp8266.com/stable/package_esp8266com_index.json
- Pico / RP2040 (Earle Philhower):
  - https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json

3) Boards Manager installs:
Arduino IDE → Tools → Board → Boards Manager
- ESP8266 by ESP8266 Community
- Raspberry Pi Pico/RP2040 (Earle Philhower)

---

## Flash the firmware

### 1) Pico (easy method: UF2)
1) Hold BOOTSEL while plugging Pico into USB
2) Pico appears as a USB drive
3) Drag-drop:
- `6x6_test_pico.ino.uf2`

### 2) ESP8266
1) Disconnect Pico GP0 → ESP RX (GPIO3)
2) Upload `lumi_con_esp_integrated_0_0_4.ino` from Arduino IDE
3) Reconnect Pico GP0 → ESP RX (GPIO3)

ESP first-time Wi‑Fi (WiFiManager):
1) Power ESP → AP appears: `Lumi-Con-Setup`
2) Connect to `Lumi-Con-Setup`
3) Open: http://192.168.4.1
4) Select home Wi‑Fi + password → Save
5) ESP reboots → TFT shows IP

---

## Build + install the Lumia plugin (v5.2)

Open a terminal in the plugin folder (where `package.json` is):

Build (Windows):
```bash
npm install
npx lumia-plugin validate .
npx lumia-plugin build . --out .\lumi_con_bridge_integrated_v5_2.lumiaplugin
```

Install into Lumia:
- Lumia → Configuration → Plugins → Installed → Install Manually
- Select `lumi_con_bridge_integrated_v5_2.lumiaplugin`

---

## Configure the plugin (minimal)

In Lumia plugin settings:
- Enable Listener = ON
- Listen Port = 8787 (or any free port)
- Shared Secret = blank (or set one)

Optional (for TFT actions):
- ESP Base URL = `http://<ESP_IP>`

Optional (debug):
- Enable Debug Toasts = ON

---

## Configure the ESP firmware (PC IP + port)

Edit these constants in `lumi_con_esp_integrated_0_0_4.ino` to match your PC and plugin settings:

```cpp
const char* PLUGIN_HOST = "192.168.1.87"; // your PC IP
constexpr uint16_t PLUGIN_PORT = 8787;    // same as plugin Listen Port
const char* PLUGIN_SECRET = "";           // same as Shared Secret (or blank)
```

---

## Mode selection (on boot)

After Wi‑Fi connects and the IP is shown, the ESP waits for a mode choice using the matrix keys:

- Press **Key 1** → LEGACY mode (HTTP 2xx counts as success)
- Press **Key 2** → CONFIRMED mode (requires `{ ok:true, seq:<same> }` ACK)

Recommended:
- Use CONFIRMED mode with plugin v5.2 (best reliability).

---

## Quick tests

### Check device endpoints
- `http://<ESP_IP>/health`
- `http://<ESP_IP>/msg?t=Hello`
- `http://<ESP_IP>/status?t=OK`
- `http://<ESP_IP>/clear`

### Check plugin listener health
- `http://<PC_IP>:8787/health`

---

## Troubleshooting

ESP upload fails:
- Disconnect Pico GP0 (TX) → ESP RX (GPIO3) during ESP flashing.

TFT is white / blank:
- Ensure TFT RST is wired to ESP RST.
- If colors are wrong, try a different ST7735 tab in the ESP firmware:
  - INITR_GREENTAB / INITR_REDTAB / INITR_BLACKTAB

Lumia alerts don’t fire:
- Confirm ESP `PLUGIN_HOST` is your PC IP
- Confirm ESP `PLUGIN_PORT` matches plugin Listen Port
- If using Shared Secret, both ends must match exactly
- PC and ESP must be on the same LAN

---

## License / disclaimer

This is an experimental community project and is **not affiliated with Lumia**.
