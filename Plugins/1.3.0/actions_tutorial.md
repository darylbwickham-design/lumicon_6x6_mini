# Actions (TFT + Pet + Connect) — Plugin 1.3.0 / Firmware 1.1

If you set **ESP Base URL**, Lumia can send commands back to the device.

## Device: Connect
Use this to register the current PC with the device again.

The plugin now retries automatically after ordinary outages. This manual action remains useful when diagnosing a changed network or forcing an immediate retry.

What it sends:
- `http://<device-ip>/plugin?host=<pc-ip>`

Best for:
- first-time setup retry
- reconnecting after network changes
- forcing the device to refresh which PC it should talk to

Input:
- none

## Device: Screen Mode
Switch the live TFT between:

- **Chat** — normal Lumia message/feed view
- **Pet** — animated persistent virtual pet
- **Diagnostics** — key delivery and ACK-focused feed

The switch is immediate and does not reboot the controller or alter event
numbers. Firmware 1.1 briefly confirms the selected mode on screen.

## Display actions

### Display: Add Line
Use this to send a new line into the device message/chat area.

Best for:
- event logs
- chat relay
- now playing text
- custom Lumia messages

Input:
- **Message**

Examples:
- `Now Playing: {{song_title}}`
- `{{username}} redeemed hydrate`
- `Pet update: {{pet_change_name}}`

### Display: Set Status
Use this to set the main status line on the device.

Best for:
- stream state
- scene name
- timer or mode text
- pet state

Input:
- **Message**
- **Status Color**
  - `Default (Teal)` — used when unset
  - `Red`
  - `Yellow`
  - `Green`

Accepted status inputs:
- `GET /status?t=REC%20STARTED&color=red`
- `GET /status?t=REC%20STARTED&color=green`
- `GET /status?t=REC%20STARTED&color=yellow`
- `POST /ui` with `{"channel":"status","text":"REC STARTED","color":"green"}`
- If **Status Color** is left on **Default (Teal)**, the plugin sends no color override and the device uses its default teal status color.

Examples:
- Message: `LIVE` | Status Color: `Red`
- Message: `Scene: BRB` | Status Color: `Yellow`
- Message: `Pet sleeping` | Status Color: `Green`
- Message: `Stage: {{pet_stage_name}}` | Status Color: `Default (Teal)`

### Display: Clear
Use this to clear the device display.

Best for:
- resetting the screen
- clearing old messages
- blanking before a new mode or section

Input:
- none

### Display: Celebration
Use this for follows, raids, milestones, wins, successful actions, or any Lumia
event that deserves stronger on-device feedback.

Input:
- **Celebration Text**
- **Style** — Confetti, Pulse, or Success
- **Accent Colour** — Teal, Green, Gold, Red, or Pink
- **Length** — Quick, Standard, or Big Moment

The animation is non-blocking: key events and the Lumia listener continue to
run while it is visible.

## Pet Action
Use this to control the virtual pet or request a fresh status update from the device.

### Pet Action options
- `status`
- `sync`
- `feed`
- `play`
- `clean`
- `sleep`
- `med`
- `discipline`
- `reset`

## Silent health checks
The plugin also uses the device status endpoint for connection verification:
- after the idle timeout, Lumia performs a silent health check with `/status?silent=1`
- this check is meant to be silent on the device
- Lumia only shows the device as disconnected if that health check fails
- while offline, registration and health checks retry automatically with a quiet backoff
- listener port errors are retried after the conflicting application releases the port

## Recommended ways to use the actions
- Use **Device: Connect** after changing Wi-Fi, router, or the PC network adapter.
- Use **Device: Screen Mode** when an action should move the device between its
  Chat, Pet, and Diagnostics views.
- Use **Display: Add Line** for scrolling information and short messages.
- Use **Display: Set Status** for one important line of text, with an optional status color.
- Use **Display: Clear** before changing modes or screens.
- Use **Pet Action -> status** before showing a pet overlay if you want the latest values.
- A status request updates pet state variables without changing the last real `pet_change_*` values.
- Use **Pet Action -> sync** after reconnecting the device.
- Use **Pet Action -> clean** when `poop_up` triggers.
- Use **Pet Action -> feed** or **play** from Lumia command buttons.

## Example Lumia setups

### Example 1 — Manual reconnect button
- Trigger: Lumia command button
- Action: **Device: Connect**

### Example 2 — Key press updates the status
- Trigger: **6x6 short**
- Variation: choose the key
- Action: **Display: Set Status**
- Message: `Pressed {{key_label}}`
- Status Color: `Default (Teal)`

### Example 3 — Recording warning
- Trigger: Lumia command button
- Action: **Display: Set Status**
- Message: `REC STARTED`
- Status Color: `Red`

### Example 4 — Pet overlay sync button
- Trigger: Lumia command button
- Action: **Pet Action**
- Pet Action value: `status`

### Example 5 — Auto clean after poop event
- Trigger: **Pet**
- Variation: `poop_up`
- Action: **Pet Action**
- Pet Action value: `clean`
