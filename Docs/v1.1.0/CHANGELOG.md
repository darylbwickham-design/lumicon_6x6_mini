# Changelog

## Firmware 1.1.0

- Fixed the missing boot-options presentation.
- Added explicit K1 Chat selection, physical key-position hints, selected-row
  highlighting, and live hold progress.
- Extended the visible selection window to 4.2 seconds.
- Replaced generic Wi-Fi browser instructions with phone-first setup steps.
- Removed the unnecessary second full boot after choosing K0 Wi-Fi setup.
- Added connecting, connected, IP-address, and setup-timeout result screens.
- Added dark captive-portal styling.
- Added polished welcome, status, message-feed and wait screens.
- Added wipe transitions between boot stages.
- Added non-blocking Confetti, Pulse and Success celebrations.
- Added 0.6–5 second configurable celebration duration.
- Added `GET /celebrate` and `POST /ui` celebration support.
- Added `GET /mode` for safe runtime Chat, Pet, and Diagnostics switching.
- Replaced the old Pet API link list with a responsive device control,
  celebration-test, display-test, and diagnostics panel.
- Added READY and plugin-linked success feedback.
- Fixed the pet HUD rotation so health/discipline is no longer skipped.
- Preserved the 1.0 hardware, protocol, event numbers, endpoints and EEPROM.

## Lumia plugin 1.3.0

- Added **Display: Celebration**.
- Added style and accent-colour controls.
- Added Quick, Standard, and Big Moment duration choices.
- Added **Device: Screen Mode** for runtime Chat/Pet/Diagnostics switching.
- Made `UI POST` the default for new firmware 1.1 installations.
- Retained Legacy GET support for firmware 1.0.
