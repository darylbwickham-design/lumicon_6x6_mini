# Actions (TFT + Pet)

If you set **ESP Base URL**, Lumia can send commands back to the device.

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
- timer/mode text
- pet state

Input:
- **Message**

Examples:
- `LIVE`
- `Scene: BRB`
- `Pet sleeping`
- `Stage: {{pet_stage_name}}`

### Display: Clear
Use this to clear the device display.

Best for:
- resetting the screen
- clearing old messages
- blanking before a new mode or section

Input:
- none

## Pet Action

Use this to control the virtual pet or request a fresh status update from the device.

### Pet Action options
- **status**
  - asks the device for current pet values without changing anything
- **sync**
  - refreshes Lumia with the current pet state
- **feed**
  - feeds the pet
- **play**
  - plays with the pet
- **clean**
  - cleans the pet / clears poop-related state
- **sleep**
  - toggles or controls sleep depending on firmware behavior
- **med**
  - gives medicine
- **discipline**
  - disciplines the pet
- **reset**
  - resets the pet state

## Silent health checks
The plugin also uses the device status endpoint for connection verification:
- after the idle timeout, Lumia performs a silent health check with `/status?silent=1`
- this check is meant to be silent on the device
- Lumia only shows the device as disconnected if that health check fails

## Recommended ways to use the actions
- Use **Display: Add Line** for scrolling information and short messages.
- Use **Display: Set Status** for one important line of text.
- Use **Display: Clear** before changing modes or screens.
- Use **Pet Action -> status** before showing a pet overlay if you want the latest values.
- Use **Pet Action -> sync** after reconnecting the device.
- Use **Pet Action -> clean** when `poop_up` triggers.
- Use **Pet Action -> feed** or **play** from Lumia command buttons.

## Example Lumia setups

### Example 1 — Key press updates the display
- Trigger: **6x6 short**
- Variation: choose the key
- Action: **Display: Set Status**
- Message: `Pressed {{key_label}}`

### Example 2 — Pet overlay sync button
- Trigger: Lumia command button
- Action: **Pet Action**
- Pet Action value: `status`

### Example 3 — Auto clean after poop event
- Trigger: **Pet**
- Variation: `poop_up`
- Action: **Pet Action**
- Pet Action value: `clean`

### Example 4 — Send pet state to the display
- Action: **Display: Add Line**
- Message: `Mood {{pet_happiness}} | Poop {{pet_poop}}`

Full guide + updates:
https://github.com/darylbwickham-design/lumicon_6x6_mini