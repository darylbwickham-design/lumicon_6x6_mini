# Wiring and compatibility — unchanged from 1.0

No STL, PCB copper, mounting-hole position or wiring change is required.

## Pico matrix

| Function | Pico GPIO |
| --- | --- |
| Row 0 | GP12 |
| Rows 1–5 | GP1–GP5 |
| Columns 0–5 | GP6–GP11 |
| UART TX to ESP RX | GP0 |

The PCB J1 order remains:

`C0, R0, C1, R1, C2, R2, C3, R3, C4, R4, C5, R5`

Key numbering is physical row-major: top row K0–K5, second row K6–K11,
through bottom row K30–K35.

## Pico to ESP8266

| Pico | ESP8266 D1 mini |
| --- | --- |
| GP0 UART TX | RX / GPIO3 |
| GND | GND |
| Power rail used by the existing build | Existing matching power input |

The UART signal is 3.3 V logic. Do not inject 5 V logic into ESP RX.

## TFT to ESP8266

| TFT function | D1 mini pin |
| --- | --- |
| CS | D2 / GPIO4 |
| DC | D1 / GPIO5 |
| SCLK | D5 / GPIO14 |
| MOSI | D7 / GPIO13 |
| RST | Existing fixed/shared reset arrangement (`-1` in firmware) |

The firmware remains `INITR_GREENTAB` with rotation `3`, matching the supplied
1.0 enclosure and bezel.

## Software compatibility

- Pico packet: `A5, type, key, xor` at 115200 baud.
- Short key events: 0–35.
- Long key events: 36–71.
- Pet events: 72–93.
- Lumia listener port: 8787.
- Existing `/msg`, `/status`, `/clear`, `/ui`, `/health`, `/plugin`, and
  `/pet` endpoints remain available.
- New endpoints: `/celebrate` and `/mode`.
- Opening `/` shows the firmware 1.1 device test and diagnostics panel.
