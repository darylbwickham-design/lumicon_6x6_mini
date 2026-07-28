# Lumi-Con 6×6 Mini v1.1.1 — display repair

This is a focused repair for the 1.1 display build. It preserves the existing
hardware wiring, display rotation, Pico packet protocol, key numbers, web
endpoints, EEPROM layout and Lumia listener port.

## What changed

- Celebrations now draw their card once, then update only their moving details.
  The old version cleared and repainted the whole 160×128 TFT every 45 ms,
  which produced the visible black sweep/flash and choppy movement.
- Confetti and Success use animated particles in the unused top and bottom
  bands. Pulse uses a small centred pulse bar. None of those frames redraw the
  message card or the whole TFT.
- The opening frame is drawn immediately when `/celebrate` is called, so the
  browser test button and Lumia action give visible feedback as soon as the
  request arrives.
- The complete normal screen is restored afterwards. This fixes the Pet-mode
  header being blank after a full-screen celebration.
- The matched Lumia plugin verifies the device's JSON confirmation before it
  reports a successful celebration.

## Files

- `Firmware/lumicon_6x6_mini_v1_1_1.ino` — ESP8266 firmware source.
- `Plugin/matrix_6x6_mini_v11-1.3.1.lumiaplugin` — update to the separate
  `matrix_6x6_mini_v11` plugin created for the 1.1 firmware. It does not
  overwrite the original `matrix_6x6_mini` plugin.

## Test after flashing

1. Open the controller address in a browser and run Confetti, Pulse and
   Success from **Test Celebrations**.
2. Each effect should appear immediately, animate without whole-screen black
   flashes, and return to a complete Chat or Pet screen.
3. Run the Lumia **Display: Celebration** action. A bad address, old firmware
   or missing endpoint now reports an error in Lumia instead of a false success.
4. Press a physical key while an effect is playing and confirm its normal Lumia
   key event still arrives.

## Build target

Compile for **LOLIN(WEMOS) D1 mini (clone)** using ESP8266 core **3.1.2** with
WiFiManager, Adafruit GFX and Adafruit ST7735 installed. Flash at offset
`0x000000` as with the previous firmware.
