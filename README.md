# Lumi-Con 6×6 Matrix Mini with LCD — v1.0.2

A compact **Lumia-first** control deck built around a **6×6 matrix**, a **Raspberry Pi Pico**, an **ESP8266**, and a small **ST7735 TFT** display.

This repository represents the **v1.x.x generation of the Lumi-Con 6×6 Matrix Mini project as a whole**.

The project is split into three practical parts:

- **Pico firmware** for matrix scanning
- **ESP8266 firmware** for Wi-Fi, HTTP, TFT display, and Lumia relay
- **Lumia plugin** for event intake, alert triggering, and TFT text actions

This mini device project is separate from the larger main Lumi-Con project.

---

## Recommended version combination

For most users, the recommended and most stable setup is:

- **Project generation:** **Lumi-Con 6×6 Matrix Mini v1.x.x**
- **Pico firmware:** `6x6_test_pico.ino` / `6x6_test_pico.ino.uf2`
- **ESP8266 firmware:** `lumi_con_esp_integrated_0_3_6.ino`
- **Lumia plugin:** **1_x_x**

### Lumia Marketplace naming note
The recommended plugin version is **v5.2**, but on the **Lumia Marketplace** it may appear as **1.0.x**.



---

## What the device does

### Inputs: device → Lumia
- The **Pico** scans all 36 keys and sends UART packets to the ESP8266
- The **ESP8266** converts those into Lumia-facing event numbers:
  - **Short press**: events `0–35`
  - **Long press**: events `36–71` (`key + 36`)
- The ESP8266 then sends events to the Lumia plugin:
  - `POST http://<PC_IP>:<PORT>/event`

### Display: Lumia → device
The ESP8266 exposes TFT endpoints so Lumia can push text and status updates to the screen:

- `GET /msg?t=...`
- `GET /status?t=...`
- `GET /clear`
- `POST /ui` (JSON)
- `GET /health`

---

## Repository contents

### Firmware

#### Pico
- `6x6_test_pico.ino`  
  Source version of the current test Pico firmware
- `6x6_test_pico.ino.uf2`  
  Easy drag-and-drop flash file

#### ESP8266
- `lumi_con_esp_integrated_0_3_6_fixed.ino`  
  **Recommended stable ESP firmware**

### Additional ESP development builds
Later ESP builds may also be present in the repository for development and testing, for example:

- `lumi_con_esp_integrated_0_1_1.ino`
- `lumi_con_esp_integrated_0_1_2.ino`
- `lumi_con_esp_integrated_0_1_3.ino`
- `lumi_con_esp_integrated_0_2_0.ino`
- `lumi_con_esp_integrated_0_2_1.ino`
- `lumi_con_esp_integrated_0_2_2.ino`
- `lumi_con_esp_integrated_0_2_3.ino`

These later ESP builds are **development branch firmware** and are not the recommended starting point for most users.

### Lumia plugin files
- `6x6_matrix_mini_v1_0_0.lumiaplugin`
- `6x6_matrix_mini_v1_0_1.lumiaplugin`
- `6x6_matrix_mini.lumiaplugin`

Recommended plugin release:
- **Plugin v5.2**
- May appear as **1.0.x** on the Lumia Marketplace

### 3D print files
- `6X6 Front.stl`
- `6X6 Rear.stl`
- `connector leg.stl`
- `controller front with lcd.stl`
- `electronics rear.stl`

---

## Hardware overview

### Pico ↔ 6×6 matrix
The Pico scans the key matrix.

### Pico → ESP8266
The Pico sends UART key data to the ESP.

### ESP8266 → TFT + Lumia
The ESP handles:

- Wi-Fi connection
- HTTP endpoints
- TFT screen drawing
- Sending events to the Lumia plugin
- Receiving message/status/UI actions for the TFT

---

## Hardware wiring

### Important for ESP flashing
Disconnect **Pico GP0 (TX) → ESP RX (GPIO3)** while uploading firmware to the ESP8266.

### A) Pico ↔ 6×6 matrix
Matches `6x6_test_pico.ino`

#### Rows (inputs with pullups)
- R0 → GP12
- R1 → GP1
- R2 → GP2
- R3 → GP3
- R4 → GP4
- R5 → GP5

#### Columns (outputs)
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

#### Recommended reset wiring
- TFT RST → **ESP RST**

#### Note
The recommended stable ESP firmware uses `TFT_RST = -1`, meaning TFT reset is not driven from a GPIO in the sketch. Wiring TFT RST directly to ESP RST is the simplest and most reliable setup.

---

## Arduino IDE setup

### 1) Install Arduino IDE
Install Arduino IDE from:

`https://www.arduino.cc/en/software`

### 2) Add board manager URLs
In Arduino IDE, open:

**File → Preferences → Additional Boards Manager URLs**

Add:

#### ESP8266
`http://arduino.esp8266.com/stable/package_esp8266com_index.json`

#### Pico / RP2040 (Earle Philhower)
`https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`

### 3) Install board packages
Open:

**Tools → Board → Boards Manager**

Install:

- **ESP8266 by ESP8266 Community**
- **Raspberry Pi Pico/RP2040 by Earle Philhower**

### 4) Install required libraries

#### Required for the ESP8266 firmware
Install these through **Sketch → Include Library → Manage Libraries**:

- **WiFiManager** by tzapu / tablatronix
- **Adafruit GFX Library** by Adafruit
- **Adafruit ST7735 and ST7789 Library** by Adafruit

These cover the external libraries used by the ESP firmware.

#### Included with the ESP8266 board package
These are used by the ESP sketch but normally come with the ESP8266 core, so you do **not** usually install them separately:

- `ESP8266WiFi`
- `ESP8266WebServer`
- `ESP8266HTTPClient`
- `WiFiClient`
- `EEPROM`
- `SPI`

#### Pico library note
The current `6x6_test_pico.ino` uses the RP2040 core and standard Arduino functionality only.
It does **not** require any extra third-party Arduino libraries beyond the installed **Raspberry Pi Pico/RP2040** board package.

---

## Flashing the firmware

### 1) Flash the Pico
The easiest method is UF2 drag-and-drop flashing.

1. Hold **BOOTSEL** while plugging the Pico into USB
2. The Pico appears as a USB drive
3. Drag and drop:
   - `6x6_test_pico.ino.uf2`

### 2) Flash the ESP8266
1. Disconnect **Pico GP0 → ESP RX (GPIO3)**
2. Upload:
   - `lumi_con_esp_integrated_0_3_6_fixed.ino`
3. Reconnect **Pico GP0 → ESP RX (GPIO3)**

---

## First-time ESP Wi-Fi setup

The ESP firmware uses **WiFiManager** for first-time network setup.

1. Power the ESP
2. A setup access point appears:
   - `Lumi-Con-Setup`
3. Connect to that AP
4. Open:
   - `http://192.168.4.1`
5. Select your Wi-Fi network and enter the password
6. Save
7. The ESP reboots and the TFT shows its IP address

---

## Lumia plugin install

Open a terminal in the plugin folder, where `package.json` is located.

### Build on Windows
```bash
npm install
npx lumia-plugin validate .
npx lumia-plugin build . --out .\lumi_con_bridge_integrated_v5_2.lumiaplugin
```

### Install into Lumia
- Open **Lumia**
- Go to **Configuration → Plugins → Installed → Install Manually**
- Select:
  - `lumi_con_bridge_integrated_v5_2.lumiaplugin`

### Marketplace note
The recommended plugin is **v5.2**, but depending on where you view it, it may be labelled as **1.0.x** on the Lumia Marketplace.

---

## Plugin configuration

In Lumia plugin settings:

- **Enable Listener** = ON
- **Listen Port** = `8787`
- **Shared Secret** = blank, or set one if you want one

### Optional for TFT actions
- **ESP Base URL** = `http://<ESP_IP>`

### Optional for debugging
- **Enable Debug Toasts** = ON

---

## ESP firmware configuration

Edit these constants in `lumi_con_esp_integrated_0_3_6.ino` so they match your PC and Lumia plugin settings:

```cpp
const char* PLUGIN_HOST = "192.168.1.87"; // your PC IP
constexpr uint16_t PLUGIN_PORT = 8787;    // same as plugin Listen Port
const char* PLUGIN_SECRET = "";           // same as Shared Secret (or blank)
```

---

## Mode selection on boot

After Wi-Fi connects and the IP is shown, the ESP waits for a mode choice using the matrix keys:

- Press **Key 0** → **factory reset**
- Press **Key 2** → **Debug mode**
- Press **Key 35** → **E-PET mode**
 

---

## Quick test URLs

### Check ESP endpoints
- `http://<ESP_IP>/health`
- `http://<ESP_IP>/msg?t=Hello`
- `http://<ESP_IP>/status?t=OK`
- `http://<ESP_IP>/clear`

### Check plugin listener
- `http://<PC_IP>:8787/health`

---

## Troubleshooting

### Missing library errors in Arduino IDE
If the ESP sketch fails with missing library errors, make sure these are installed:

- **WiFiManager**
- **Adafruit GFX Library**
- **Adafruit ST7735 and ST7789 Library**

If the Pico sketch fails, double-check that the **Raspberry Pi Pico/RP2040** board package is installed correctly.

### ESP upload fails
Disconnect **Pico GP0 (TX) → ESP RX (GPIO3)** while flashing the ESP8266.

### TFT stays white or blank
- Check that **TFT RST** is wired to **ESP RST**
- If colours are wrong, try a different ST7735 init tab in the ESP firmware:
  - `INITR_GREENTAB`
  - `INITR_REDTAB`
  - `INITR_BLACKTAB`

### Lumia alerts do not fire
- Check that ESP `PLUGIN_HOST` matches your PC IP
- Check that ESP `PLUGIN_PORT` matches the plugin listen port
- If you use a shared secret, it must match on both sides
- Make sure the PC and ESP are on the same LAN

### Plugin version naming looks strange
- The recommended plugin release is **v5.2**
- On the Lumia Marketplace it may be shown as **1.0.x**
- This README uses the internal project version history for clarity

---

## Development branch note

The later ESP firmware builds in this repository belong to the ongoing development branch.


If you are building the device for normal use, start with:

- **Pico:** `6x6_test_pico.ino.uf2`
- **ESP:** `lumi_con_esp_integrated_0_3_6.ino`
- **Plugin:** **v1.1.2**


---

## License / disclaimer

This is an experimental community project and is **not affiliated with Lumia**.
