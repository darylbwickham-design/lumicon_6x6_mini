# Build validation — Lumi-Con 6×6 Mini 1.1

## ESP8266 firmware

- Board target: `esp8266com:esp8266:d1_mini_clone`
- ESP8266 board core: `3.1.2`
- Result: clean compile completed with all sketch warnings enabled
- Flash binary: `476,144` bytes
- Global/static RAM: `35,700 / 80,192` bytes (`44%`)
- IRAM: `60,395 / 65,536` bytes (`92%`, including the fixed 32 KiB
  instruction-cache reservation reported by the ESP8266 build)
- Flash code: `437,092 / 1,048,576` bytes (`41%`)

Libraries used:

| Library | Version |
| --- | --- |
| WiFiManager | 2.0.17 |
| Adafruit GFX Library | 1.12.1 |
| Adafruit BusIO | 1.17.2 |
| Adafruit ST7735 and ST7789 Library | 1.11.0 |
| ESP8266WiFi / WebServer / HTTPClient / EEPROM / SPI / Wire | ESP8266 core 3.1.2 bundled versions |

The matching ELF and map files are in `Firmware/ESP8266/debug/` for stack
decoding and support work. They are not required for flashing.

## Lumia plugin

- Plugin manifest version: `1.3.0`
- Lumia target: `^9.0.0`
- SDK dependency: `@lumiastream/plugin 0.9.0`
- Manifest parsed and passed the SDK validator with zero errors.
- `main.js` passed Node syntax checking.
- A mock-controller integration test confirmed the exact routes and bodies for:
  - Device: Screen Mode
  - Display: Celebration, including duration
  - Display: Add Line
  - Display: Set Status
  - Display: Clear
  - Pet Action: Status

## Compatibility/integrity

- All seven supplied STL files are byte-identical to the 1.0 release.
- The complete PCB directory is byte-identical to the 1.0 release.
- The Pico UF2 and source are byte-identical to the 1.0 release.
- Existing short events `0–35`, long events `36–71`, pet events `72–93`,
  UART packet format, port `8787`, EEPROM layout, TFT pins, tab type, and
  rotation are unchanged.

## Firmware hashes

| File | SHA-256 |
| --- | --- |
| `FLASH_THIS_lumicon_6x6_mini_v1.1.0.bin` | `5636342b89f6cd6db258172839e325411e7f1e8c68be52819f5e47d1987ef46d` |
| `rollback/lumi_con_esp_integrated_1_0_4.ino.bin` | `7505e4bc7d8a3d14d46622c88c239665d2ab5864cd03486a642ba85bf84b96c3` |
| `Pico/6x6_matrix_pico_v1_0/6x6_matrix_pico_v1_0.uf2` | `1cf1ededb351b8376ee2fd855a11ce8195c9260ff06f8657dac9c4c5414bdb0c` |
| `Plugin/matrix_6x6_mini-1.3.0.lumiaplugin` | `47922abda2b52680853b6616d934d1afe096ee5b67a9420231fa3332ea45a1be` |

Compilation and software-side validation cannot substitute for the physical
acceptance checklist in `UPGRADE_AND_TEST.md`.
