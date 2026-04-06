# 6×6 Matrix Mini — Setup

36 key open source stream controller with integrated LCD, chat, alerts, and virtual pet support.

## Build files
GitHub repository:
https://github.com/darylbwickham-design/lumicon_6x6_mini

## Tutorial video
https://www.youtube.com/watch?v=UeyQVr_ehtY&list=PLqOF0QEQ86SRTQ2jzz_a06AIdHnLY1Pae

## Quick checklist
1. Install the plugin in Lumia.
2. Download and flash the amended firmware.
3. Power the device and complete Wi-Fi setup.
4. In plugin settings, turn **Enable Listener** on.
5. Leave **Listen Port** as `8787` unless your firmware uses a different port.
6. If you set a secret in firmware, enter the same value in **Shared Secret**.
7. If you want Lumia to send text or pet commands back to the device, set **ESP Base URL** to your device address, for example:
   - `http://192.168.1.50`
8. Leave **ESP UI Mode** as `legacy_get` unless your firmware is using `/ui` POST mode.
9. Optional:
   - fill in **Key labels (Short 0–35)**
   - fill in **Key labels (Long 0–35)**
   These labels become `{{key_label}}` inside Lumia.
10. Optional:
   - turn on **Show advanced settings**
   - confirm **Status Path** is `/status`
   - confirm **Offline Timeout (seconds)** is set how you want

## Silent health-check behavior
- The plugin does **not** immediately mark the device offline just because the idle timeout elapsed.
- After **Offline Timeout**, the plugin performs one silent health check to:
  - `/status?silent=1`
- If the device replies successfully, Lumia keeps the device connected.
- Lumia only marks the device offline if that health check fails.

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
These update automatically when the device talks to Lumia.

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

Use these for:
- support/debug overlays
- online/offline indicators
- signal strength displays
- status text panels

## Pet variables
These can be used in overlays, alert text, and commands.

### Pet change variables
- `{{pet_change_name}}`
- `{{pet_change_code}}`
- `{{pet_variation}}`
- `{{pet_field_id}}`
- `{{pet_field_name}}`
- `{{pet_from}}`
- `{{pet_to}}`
- `{{pet_delta}}`

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

### Good uses for variables
- Overlay text:
  - `Hunger: {{pet_hunger}}`
  - `Stage: {{pet_stage_name}}`
  - `Poops: {{pet_poop}}`
  - `Status: {{device_status_text}}`
- Alert messages:
  - `Pet update: {{pet_change_name}}`
  - `{{pet_field_name}} changed from {{pet_from}} to {{pet_to}}`
- Display text:
  - `Pet Stage: {{pet_stage_name}}`
  - `RSSI {{device_rssi}}`