# Lumi-Con 6×6 Matrix Mini 1.1

This is the compatibility-first upgrade from the supplied 1.0 release.

## What to install

1. Flash `Firmware/ESP8266/FLASH_THIS_lumicon_6x6_mini_v1.1.0.bin` to the
   D1 mini clone at address `0x000000`.
2. Install `Plugin/matrix_6x6_mini-1.3.0.lumiaplugin` in Lumia.
3. Leave the Pico firmware alone unless it needs restoring. Its source and UF2
   are included and remain protocol-compatible with 1.0.
4. Reboot the controller and follow the on-screen boot selector.

Existing Wi-Fi credentials, plugin-host configuration and pet data use the
same EEPROM layout and should survive an ordinary firmware upgrade.

## First Wi-Fi setup

Use a mobile phone, not the streaming PC:

1. Connect the phone to `Lumi-Con-Setup`.
2. Tap the phone's Wi-Fi sign-in notification.
3. If the sign-in page does not open, browse to `192.168.4.1`.
4. Select the normal home/streaming Wi-Fi network and enter its password.

Holding K0 during boot deliberately erases the saved Wi-Fi settings and opens
this setup flow immediately. It no longer makes you sit through a second full
boot before the phone setup screen appears.

## Visible boot controls

Key numbering is row-major: K0 is top-left and K35 is bottom-right.

| Hold during the boot menu | Result |
| --- | --- |
| K0 — top-left | Erase saved Wi-Fi and open phone setup |
| K1 — second key on the top row | Chat mode (also the default) |
| K2 — third key on the top row | noPet diagnostics |
| K35 — bottom-right | Virtual pet mode |

The progress bar must fill before a selection is accepted. If no key is held,
the controller enters Chat mode.

## New in 1.1

- The boot-options page is now actually shown.
- Phone-first Wi-Fi instructions are explicit on the TFT.
- Polished panels, badges, feed cards and wipe transitions replace the crude
  text-only presentation.
- New non-blocking `Confetti`, `Pulse`, and `Success` celebrations.
- Quick, standard, and big-moment celebration lengths.
- Lumia plugin action: **Display: Celebration**.
- Lumia plugin action: **Device: Screen Mode** switches Chat, Pet, and
  Diagnostics at runtime without a reboot.
- A responsive controller panel is available by opening the IP shown on the
  TFT. It can change screen mode, test every celebration, test messages/status,
  clear the feed, and open diagnostic JSON.
- The pet HUD now rotates through all four information pages; the previous
  health/discipline page was unreachable.
- The existing 0–71 key events, 72–93 pet events, endpoints, port 8787,
  display rotation, wiring and pet storage remain compatible.

Read `Docs/UPGRADE_AND_TEST.md` before treating the build as production-tested
on physical hardware. `Docs/BUILD_VALIDATION.md` records the completed compile,
plugin routing, archive, and hardware-integrity checks.
