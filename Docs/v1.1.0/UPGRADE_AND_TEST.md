# Upgrade and test procedure — 1.1

## Back up / rollback

The supplied 1.0 binary is included at:

`Firmware/ESP8266/rollback/lumi_con_esp_integrated_1_0_4.ino.bin`

Flashing that binary at `0x000000` restores the previous ESP firmware. An
ordinary firmware flash does not intentionally erase EEPROM. Do not select an
erase-all option unless you also intend to remove saved Wi-Fi and pet state.

## ESP flash

Target: **LOLIN(WEMOS) D1 mini (clone)**, ESP8266 core 3.1.2.

With esptool:

```powershell
py -m esptool --chip esp8266 --port COMx --baud 460800 write_flash 0x000000 FLASH_THIS_lumicon_6x6_mini_v1.1.0.bin
```

Replace `COMx` with the controller's serial port. The binary can also be used
by the existing Lumi-Con web flasher if that flasher accepts a complete
ESP8266 sketch binary at offset zero.

## Lumia

1. Install `matrix_6x6_mini-1.3.0.lumiaplugin`.
2. Set **ESP Base URL** to the IP shown on the controller, including `http://`.
3. Leave the listener enabled and port at `8787`.
4. Leave **ESP UI Mode** on `UI POST`.
5. Save, then run **Device: Connect** if registration does not complete
   automatically.
6. Open the IP shown on the TFT in a browser to use the built-in device test
   panel.

## Required physical test

- [ ] Screen remains landscape with the existing enclosure/bezel.
- [ ] Logo, welcome and boot selector appear in sequence.
- [ ] K0/K1/K2/K35 highlight their full row and show hold progress.
- [ ] No key held enters Chat mode.
- [ ] With Wi-Fi erased, the display says to use a mobile phone.
- [ ] Choosing K0 reaches phone setup without repeating the logo sequence.
- [ ] Phone captive portal completes and the assigned IP appears.
- [ ] Short presses deliver events 0–35 exactly once.
- [ ] Long presses deliver events 36–71 exactly once.
- [ ] ACK OK/FAIL changes correctly without freezing the display.
- [ ] Display Message, Status, Clear and Celebration actions work.
- [ ] Quick, Standard and Big Moment celebration lengths differ visibly.
- [ ] Confetti, Pulse and Success animations do not stop key delivery.
- [ ] Device: Screen Mode switches Chat/Pet/Diagnostics without rebooting.
- [ ] Browser panel loads from the displayed IP and its test controls work.
- [ ] Pet mode loads existing state and pet actions still work.
- [ ] Pet HUD eventually shows health and discipline values.
- [ ] Reboot preserves Wi-Fi, plugin host and pet state.

## Acceptance rule

Do not call a unit passed after a screen-only check. Test all 36 keys once,
then long-press at least K0, K17 and K35, and confirm Lumia received the
expected event numbers without duplicates.
