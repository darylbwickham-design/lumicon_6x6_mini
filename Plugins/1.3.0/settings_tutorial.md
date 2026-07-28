# Lumi-Con 6×6 Mini — Setup v1.3.0 / Firmware 1.1

36 key open source stream controller with integrated LCD, chat, alerts, and virtual pet support.

## Quick checklist
1. Install the plugin in Lumia.
2. Download and flash the amended firmware.
3. Power the device and complete Wi-Fi setup with a mobile phone, not the streaming PC:
   - connect the phone to `Lumi-Con-Setup`
   - tap the phone's Wi-Fi sign-in notification, or open the phone's browser at `192.168.4.1`
   - enter your normal Wi-Fi details on the phone setup page
4. In plugin settings, enter **ESP Base URL** first, for example:
   - `http://192.168.1.50`
5. Click **Save**.
   - The plugin detects the local PC IP that reaches the device.
   - The plugin then calls:
     - `http://<device-ip>/plugin?host=<pc-ip>`
6. Turn **Enable Listener** on.
7. Leave **Listen Port** as `8787`. The supplied firmware has this port compiled into it.
8. Leave **Shared Secret** empty unless you have compiled the identical `PLUGIN_SECRET` into the firmware.
9. If needed later, run the **Device: Connect** action to send the host registration again.
10. Leave **ESP UI Mode** as `ui_post` with firmware 1.1. Select `legacy_get` only when using 1.0 firmware.
11. Optional:
   - fill in **Key labels (Short 0–35)**
   - fill in **Key labels (Long 0–35)**
   These labels become `{{key_label}}` inside Lumia.

## Display status color support
- **Display: Set Status** now includes a **Status Color** dropdown.
- Available colors:
  - `Default (Teal)`
  - `Red`
  - `Yellow`
  - `Green`
- If no color is chosen, the device keeps its default teal status color.

## Display celebrations
- **Display: Celebration** plays a non-blocking full-screen animation.
- Styles: `Confetti`, `Pulse`, and `Success`.
- Accent colours: teal, green, gold, red, and pink.
- Lengths: Quick (1 second), Standard (1.8 seconds), and Big Moment (3 seconds).
- The key listener and HTTP server continue operating while the animation plays.

## Runtime screen switching
- Use **Device: Screen Mode** to select Chat, Pet, or Diagnostics.
- The controller does not reboot.
- The selected mode lasts until the next reboot; the boot default remains Chat
  unless a boot key is held.

## Built-in test panel
Open the ESP Base URL itself in a browser. Firmware 1.1 provides controls for
screen modes, all three celebrations, display/message tests, and JSON
diagnostics.

## Reliability repairs in v1.2.2
- Repeated firmware delivery attempts can no longer execute one physical press twice.
- A pet status request updates state without inventing a false `hunger_up` change.
- The listener recovers automatically after a port conflict is cleared.
- Offline devices are rechecked with a quiet exponential backoff.
- Firmware reboots reset event sequencing safely.
- Device event processing is parallelised so acknowledgements return more quickly.
- Timed-out HTTP requests are aborted instead of remaining open.

## What “Device: Connect” does
- Resolves the correct local PC IP for the selected device address
- Sends `GET /plugin?host=<pc-ip>` to the ESP Base URL
- Updates these variables:
  - `{{host_pc_ip}}`
  - `{{host_register_status}}`
  - `{{host_register_message}}`
  - `{{host_register_at}}`

## Health checks and automatic recovery
- The plugin does **not** immediately mark the device offline just because the idle timeout elapsed.
- After **Offline Timeout**, the plugin performs one silent health check to:
  - `/status?silent=1`
- If the device replies successfully, Lumia keeps the device connected.
- Lumia only marks the device offline if that health check fails.
- When the device is offline, the plugin automatically retries registration and health checks.
- Retry intervals gradually increase to avoid network spam and reset after recovery.
- If the listener port is occupied, the plugin reports the error and retries after the conflict is cleared.

## What the plugin listens for
- **6×6 short press**
  - event `0` to `35`
- **6×6 long press**
  - event `36` to `71`
- **Pet events**
  - event base `72`
  - event count `22`

## Alerts added by the plugin
- **6x6 short**
  - Use this for normal short presses on the matrix.
  - Add a variation and choose the key number you want.
- **6x6 long**
  - Use this for long presses on the matrix.
  - Add a variation and choose the key number you want.
- **Pet**
  - Use this for virtual pet changes sent by the ESP firmware.
  - Add a variation and choose the pet update you want.

### Pet variations
- `hunger_up`
- `hunger_down`
- `happiness_up`
- `happiness_down`
- `energy_up`
- `energy_down`
- `hygiene_up`
- `hygiene_down`
- `health_up`
- `health_down`
- `discipline_up`
- `discipline_down`
- `poop_up`
- `poop_down`
- `stage_up`
- `stage_down`
- `sleep_on`
- `sleep_off`
- `sick_on`
- `sick_off`
- `alive_on`
- `alive_off`

## Device variables

### Matrix / key variables
- `{{event}}` — key number
- `{{kind}}` — `short` or `long`
- `{{key_label}}` — label from plugin settings
- `{{held_ms}}` — press duration in milliseconds
- `{{received_at}}` — when Lumia received the event

### Device status variables
- `{{device_id}}`
- `{{device_ip}}`
- `{{device_rssi}}`
- `{{device_connected}}`
- `{{device_last_seen}}`
- `{{device_status_text}}`
- `{{seq}}`
- `{{listener_status}}`
- `{{listener_error}}`
- `{{listener_port}}`
- `{{firmware_plugin_port}}`
- `{{firmware_version}}`
- `{{device_free_heap}}`

### Host registration variables
- `{{host_pc_ip}}`
- `{{host_register_status}}`
- `{{host_register_message}}`
- `{{host_register_at}}`

Use these for:
- support or debug overlays
- online or offline indicators
- signal strength displays
- showing the registered PC IP
- showing whether the connect step succeeded

## Pet variables

### Pet change variables
- `{{pet_change_name}}`
- `{{pet_change_code}}`
- `{{pet_variation}}`
- `{{pet_field_id}}`
- `{{pet_field_name}}`
- `{{pet_from}}`
- `{{pet_to}}`
- `{{pet_delta}}`

Pet state requests do not overwrite these change variables. They retain the last real pet transition.

### Pet state variables
- `{{pet_ui_mode}}`
- `{{pet_mode_enabled}}`
- `{{pet_alive}}`
- `{{pet_stage}}`
- `{{pet_stage_name}}`
- `{{pet_age_minutes}}`
- `{{pet_hunger}}`
- `{{pet_happiness}}`
- `{{pet_energy}}`
- `{{pet_hygiene}}`
- `{{pet_health}}`
- `{{pet_discipline}}`
- `{{pet_poop}}`
- `{{pet_sick}}`
- `{{pet_sleeping}}`
- `{{pet_event_base}}`
- `{{pet_event_count}}`
- `{{pet_queue_depth}}`
- `{{pet_last_change_code}}`

## Good uses for variables
- Overlay text:
  - `Hunger: {{pet_hunger}}`
  - `Stage: {{pet_stage_name}}`
  - `Poops: {{pet_poop}}`
  - `Status: {{device_status_text}}`
  - `Host: {{host_pc_ip}}`
- Alert messages:
  - `Pet update: {{pet_change_name}}`
  - `{{pet_field_name}} changed from {{pet_from}} to {{pet_to}}`
- Display text:
  - `Pet Stage: {{pet_stage_name}}`
  - `RSSI {{device_rssi}}`

## Suggested test for the new status color feature
Create four **Display: Set Status** actions in Lumia and test:
1. Message: `REC STARTED` with **Status Color** = `Red`
2. Message: `BRB` with **Status Color** = `Yellow`
3. Message: `LIVE` with **Status Color** = `Green`
4. Message: `READY` with **Status Color** = `Default (Teal)`
