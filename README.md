# Lumi-Con 6×6 Matrix Mini v1.1.0

A 36-key open-source stream controller with an integrated ST7735 TFT display, browser-based device setup, Lumia plugin support, and optional virtual pet features.

This project uses:

- a **Raspberry Pi Pico (RP2040)** to scan the 6×6 matrix
- an **ESP8266** to handle the TFT, Wi-Fi, HTTP API, and Lumia communication
- a **Lumia plugin** to receive key events, expose variables, and send display / pet actions back to the device

---

## What it does

### Inputs: device → Lumia

- The Pico scans all 36 keys and sends UART packets to the ESP8266.
- The ESP8266 converts those into Lumia-facing events:
  - **Short press:** `0–35`
  - **Long press:** `36–71` (`key + 36`)
- The ESP8266 posts events to the Lumia plugin listener.

### Display and pet control: Lumia → device

The ESP8266 exposes local HTTP endpoints so Lumia can send text, status updates, clear commands, health checks, and pet actions back to the device.

---

## Recommended current release set

Use this combination as the current baseline:

- **Pico firmware:** `6x6_test_pico.ino` or UF2 built from it
- **ESP firmware:** `lumi_con_esp_integrated_0_4_2.ino`
- **Lumia plugin:** `6x6_matrix_mini_v1_1_3.lumiaplugin`

### Plugin naming note

Older project history refers to the integrated plugin as **v5.2**. That package is effectively the **1.0.0** public baseline for the newer `6x6 matrix mini` plugin line.

If you are using the newer plugin packages, treat these as the current progression:

- `1.0.0` = old integrated `v5.2`
- `1.1.x` = newer `6x6 matrix mini` plugin line

---

## Main features

### Easy browser-based setup

On first boot, the ESP firmware can create a setup access point named **`Lumi-Con-Setup`**.

Connect to it and open:

`http://192.168.4.1`

From there you can join the device to your Wi-Fi network without editing the firmware source.

### Browser-based plugin host pairing

Once the device is on your network, you can set the Lumia/plugin host from a browser instead of recompiling the ESP sketch.

Use:

`http://<device-ip>/plugin?host=<pc-ip>`

This host is stored by the ESP and reused after reboot.

### TFT display actions

The Lumia plugin can send:

- **Display: Add Line**
- **Display: Set Status**
- **Display: Clear**

This is useful for:

- scene/status text
- alerts
- chat-style output
- debugging
- custom stream messages

### Virtual pet support

The platform includes an optional virtual pet system.

The current plugin supports these pet actions:

- `status`
- `sync`
- `feed`
- `play`
- `clean`
- `sleep`
- `med`
- `discipline`
- `reset`

The plugin also exposes pet variables for overlays, alerts, and automations.

### More reliable idle handling

The current plugin line performs a silent health check using `/status?silent=1` before marking the device offline, which helps avoid false disconnects when the device is simply idle.

### Multiple runtime modes

The current ESP firmware supports:

- **Chat mode**
- **noPet Debug mode**
- **Pet mode**
- **LCD Only Mode**

---

## Repository layout

At the time of writing, the public repository includes top-level folders for **3d files**, **Docs**, **Firmware**, and **Plugins/v5.2**. This README is written around the latest firmware and plugin files reviewed for this update, even where the live public repo still shows older packaging and README text. 

Recommended repo structure going forward:

- `Firmware/` for Pico and ESP firmware
- `Plugins/` for Lumia plugin packages and source
- `Docs/` for setup docs, changelogs, and release notes
- `3d files/` for enclosure and print assets

---

## Hardware overview

### Pico ↔ 6×6 matrix

The Pico scans the matrix.

Current Pico test firmware pin mapping:

#### Rows (inputs with pullups)
- `R0 -> GP12`
- `R1 -> GP1`
- `R2 -> GP2`
- `R3 -> GP3`
- `R4 -> GP4`
- `R5 -> GP5`

#### Columns (outputs)
- `C0 -> GP6`
- `C1 -> GP7`
- `C2 -> GP8`
- `C3 -> GP9`
- `C4 -> GP10`
- `C5 -> GP11`

### Pico → ESP8266 UART

- `Pico GP0 (TX) -> ESP RX / GPIO3`
- `Pico GND -> ESP GND`
- Optional: `1k` resistor in series on the TX line

### ESP8266 → ST7735 TFT (SPI)

Current ESP firmware pin mapping:

- `TFT VCC -> 3V3`
- `TFT GND -> GND`
- `TFT SCK -> D5 / GPIO14`
- `TFT MOSI -> D7 / GPIO13`
- `TFT CS -> D2 / GPIO4`
- `TFT DC -> D1 / GPIO5`
- `TFT RST -> ESP RST` recommended

### Important flashing note

Disconnect `Pico GP0 (TX) -> ESP RX (GPIO3)` while uploading firmware to the ESP8266.

---

## Arduino IDE setup

### 1) Install Arduino IDE

Download from:

`https://www.arduino.cc/en/software`

### 2) Add board manager URLs

In Arduino IDE, open:

`File -> Preferences -> Additional Boards Manager URLs`

Add:

#### ESP8266
`http://arduino.esp8266.com/stable/package_esp8266com_index.json`

#### Pico / RP2040 (Earle Philhower)
`https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`

### 3) Install board packages

Open:

`Tools -> Board -> Boards Manager`

Install:

- **ESP8266 by ESP8266 Community**
- **Raspberry Pi Pico/RP2040 by Earle Philhower**

### 4) Install required libraries for the ESP8266 firmware

Install through **Sketch -> Include Library -> Manage Libraries**:

- **WiFiManager**
- **Adafruit GFX Library**
- **Adafruit ST7735 and ST7789 Library**

The Pico test firmware does not require extra third-party Arduino libraries beyond the RP2040 board package.

---

## Flashing the firmware

### 1) Flash the Pico

Flash your current Pico matrix-scanner firmware first.

If you use a UF2 build:

1. Hold **BOOTSEL** while plugging the Pico into USB
2. The Pico appears as a USB drive
3. Drag and drop the UF2 file

If you use source:

- Open `6x6_test_pico.ino`
- Select your Pico board
- Upload normally

### 2) Flash the ESP8266

1. Disconnect `Pico GP0 -> ESP RX`
2. Open `lumi_con_esp_integrated_0_4_2.ino`
3. Select the correct ESP8266 board
4. Upload the firmware
5. Reconnect `Pico GP0 -> ESP RX`

---

## First-time device setup

### Step 1: connect the ESP to Wi-Fi

1. Power the device
2. Connect to the access point:
   - **`Lumi-Con-Setup`**
3. Open:
   - **`http://192.168.4.1`**
4. Select your Wi-Fi network and enter the password
5. Save
6. The ESP reboots and the display shows its IP address

### Step 2: install and configure the Lumia plugin

Install the current Lumia plugin package.

Recommended current plugin:

- `6x6_matrix_mini_v1_1_3.lumiaplugin`

In Lumia plugin settings:

- **Enable Listener**: `ON`
- **Listen Port**: `8787`
- **Shared Secret**: blank unless you intentionally set one in firmware
- **ESP Base URL**: set this to your device address, for example:
  - `http://192.168.1.50`

Recommended defaults:

- **ESP UI Mode**: `legacy_get`
- **Status Path**: `/status`
- **Pet Path**: `/pet`

### Step 3: pair the device with the plugin host

From a browser on the same network, run:

`http://<device-ip>/plugin?host=<pc-ip>`

Example:

`http://192.168.1.50/plugin?host=192.168.1.87`

This tells the ESP where the Lumia plugin listener is running.

### Step 4: confirm the device is responding

Useful test URLs:

- `http://<device-ip>/health`
- `http://<device-ip>/plugin`
- `http://<device-ip>/msg?t=Hello`
- `http://<device-ip>/status?t=OK`
- `http://<device-ip>/clear`
- `http://<device-ip>/pet?action=status`

---

## Current HTTP API

The current ESP firmware supports these user-facing routes:

- `GET /`
- `GET /msg?t=...`
- `GET /status?t=...`
- `GET /status?silent=1`
- `GET /clear`
- `POST /ui`
- `GET /health`
- `GET /plugin`
- `GET /plugin?host=<pc-ip>`
- `GET /plugin?clear=1`
- `GET /pet?action=status|sync|feed|play|clean|sleep|med|toggleSleep|discipline|reset`

---

## Boot behavior and modes

Current boot-key behavior in the ESP firmware:

- Hold **Pico key 0** at boot -> reset Wi-Fi settings and restart
- Hold **Pico key 1** at boot -> Chat mode
- Hold **Pico key 2** at boot -> noPet Debug mode
- Hold **Pico key 35** at boot -> Pet mode

Current runtime modes include:

- **Chat mode**
- **noPet Debug mode**
- **Pet mode**
- **LCD Only Mode**

---

## Lumia plugin: what it adds

### Alerts

The current plugin provides:

- **6x6 short**
- **6x6 long**
- **Pet**

### Actions

The current plugin provides:

- **Display: Add Line**
- **Display: Set Status**
- **Display: Clear**
- **Pet Action**

### Useful variables

The plugin exposes device and pet variables, including:

- `{{event}}`
- `{{kind}}`
- `{{key_label}}`
- `{{held_ms}}`
- `{{device_id}}`
- `{{device_ip}}`
- `{{device_rssi}}`
- `{{device_connected}}`
- `{{device_status_text}}`
- `{{pet_stage_name}}`
- `{{pet_hunger}}`
- `{{pet_happiness}}`
- `{{pet_health}}`
- `{{pet_poop}}`
- `{{pet_change_name}}`

---

## Quick usage ideas

- Use **Display: Set Status** for stream state text such as `LIVE`, `BRB`, or scene names.
- Use **Display: Add Line** for scrolling messages, redeems, or now-playing text.
- Use **Pet Action -> status** before updating a pet overlay.
- Use **Pet Action -> sync** after reconnecting the device.
- Use **Pet Action -> clean** when a pet event indicates poop-related state changed.

---

## Troubleshooting

### ESP upload fails

Disconnect `Pico GP0 (TX) -> ESP RX (GPIO3)` while flashing the ESP8266.

### The setup AP does not appear

- Reboot the device
- Use the factory reset boot key path if Wi-Fi credentials need clearing
- Make sure the ESP firmware actually flashed successfully

### TFT stays white or blank

- Check TFT power and SPI wiring
- Wire `TFT RST -> ESP RST`
- If colours are wrong, check the ST7735 tab/init settings in firmware

### Lumia receives nothing

- Make sure the plugin listener is enabled
- Confirm the plugin listen port is `8787` unless intentionally changed
- Confirm the ESP host pairing was set with:
  - `http://<device-ip>/plugin?host=<pc-ip>`
- Confirm the PC and device are on the same LAN

### Lumia can receive key events but cannot send text or pet commands

- Set **ESP Base URL** in plugin settings to the device address
- Check `/health` and `/status` in a browser
- Confirm the device IP did not change after a reboot

### The plugin looks “offline” after idle time

The current plugin line performs one silent health check first. If you still see offline behavior:

- confirm **ESP Base URL** is correct
- confirm **Status Path** is `/status`
- confirm the device responds to:
  - `http://<device-ip>/status?silent=1`

---

## Suggested public summary

**Lumi-Con 6×6 Matrix Mini is a 36-key open-source stream controller with an integrated TFT display, browser-based device setup, Lumia plugin support, virtual pet features, and flexible display/status workflows for streams and automations.**
