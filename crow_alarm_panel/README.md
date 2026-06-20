# Crow Alarm Panel - ESPHome Custom Component

ESPHome custom component for integrating Crow/Arrowhead alarm panels with Home Assistant.

## Features

- **Auto-created diagnostics**: Panel hardware, firmware, ready, mains, battery state, alarm bus connected
- **Optional YAML**: zones, `panel_lcd` raw sniffer, armed state, ACP, buttons
- **Full Alarm Control Panel Entity**: Expose a Home Assistant alarm panel while keeping button/switch entities
- **Keypad Bus Logging**: `on_message` trigger exposes raw bus frames
- **Output State**: Switch entities reflect output states (read-only today)
- **Keypress/Arm/Disarm Buttons**: Sends keypad keypress sequences to arm away/stay or disarm
- **Interrupt-Driven**: ISR-based clocked serial decoding

## Credits

This repo builds on the work done by [jesserockz](https://github.com/jesserockz/esphome-components/tree/main/components/crow_alarm_panel). 
I have added the transmission code, but the bulk of the work is thanks to them.

## Installation

### Method 1: External Components (Recommended)

See [here](https://esphome.io/components/external_components/) for the full reference on external components

Add to your ESPHome YAML configuration:

```yaml
external_components:
  - source: github://yourusername/esphome-crow-alarm@main
    components: [ crow_alarm_panel ]
```

### Method 2: Local Components

1. Clone this repository:
   ```bash
   cd /config  # Your ESPHome config directory
   git clone https://github.com/yourusername/esphome-crow-alarm.git
   ```

2. Reference in your YAML:
   ```yaml
   external_components:
     - source:
         type: local
         path: esphome-crow-alarm
       components: [ crow_alarm_panel ]
   ```

## Configuration

### Basic Setup

```yaml
crow_alarm_panel:
  name: "Crow Alarm"
  id: alarm
  clock_pin: 18
  data_pin: 19
  address: 0

# Auto-created: Panel hardware, firmware, ready, mains, battery, bus connected

text_sensor:
  - platform: crow_alarm_panel
    crow_alarm_panel_id: alarm
    type: armed_state
    name: "Alarm Status"

binary_sensor:
  - platform: crow_alarm_panel
    crow_alarm_panel_id: alarm
    type: zone
    zone: 2
    name: "Hall PIR"
    device_class: motion
    include_bypass_sensor: true   # also creates "Hall PIR Bypassed" (read-only)
    filters:
      - delayed_off: 2000ms

  # Or separate bypass block (still supported):
  # - platform: crow_alarm_panel
  #   crow_alarm_panel_id: alarm
  #   type: bypass
  #   zone: 2
  #   name: "Hall PIR Bypassed"

# Full alarm control panel entity (optional)
alarm_control_panel:
  - platform: crow_alarm_panel
    name: "Crow Alarm"
    code: !secret alarm_code        # Optional: default disarm code
    requires_code_to_arm: false     # Optional: set true if your panel needs code to arm
# Buttons, switches, and sensors from above can still be used alongside the full panel entity.

# Output state (read-only; write not implemented)
switch:
  - platform: crow_alarm_panel
    type: output
    output: 1
    name: "Siren (read only)"

# Buttons for keypad-driven actions
button:
  - platform: crow_alarm_panel
    type: arm_away
    name: "Arm Away"

  - platform: crow_alarm_panel
    type: arm_stay
    name: "Arm Stay"

  - platform: crow_alarm_panel
    type: disarm
    name: "Disarm"
    code: !secret alarm_code
```

## Hardware Setup

### Wiring

Connect your ESP32 to the alarm panel keypad bus:

- **Clock Pin (GPIO18)**: Connect to alarm panel clock line through a 5v <-> 3.3v level shifter
- **Data Pin (GPIO19)**: Connect to alarm panel data line through the level shifter
- **Ground**: Common ground between ESP32 and alarm panel
- **Power**: The arrowhead esl panel uses 13v for its power line. You can use a buck converter to step this down to 5v to make it useable by the esp32.

### Recommended Hardware

- ESP32 development board
- Bidirectional Logic level converter
- Buck converter

## Known Limitations

- Output switches are read-only; `set_output` is not implemented yet.
- Setting the address as a different value to the keypad causes the keypad to reset. Unsure why at this point.
- Crow keypad key bytes (`0x14` CHIME, `0x15` MEM, etc.) are outside the built-in `KEYS[]` map — sniffer logs "Unknown key index" for these.
- Memory browse opcodes (`0xA9`, `0xA0`) and memory event text decode are not implemented — `0x20` logs event # only.
- Transmission emulates keypad button presses (`0xA1`) only; no direct panel commands.

## Protocol

This component decodes the proprietary Crow alarm panel serial protocol:

- **Communication Type**: Clock-synchronous serial
- **Bit Order**: LSB-first transmission
- **Frame Markers**: 0x7E boundary bytes
- **Interrupt-Driven**: GPIO interrupt on clock falling edge
- **Message Types**: Zone state, armed state, keypresses, outputs, time, memory events

### Supported Message Types

| Type | Description |
|------|-------------|
| 0x10 | Panel status — see field notes below |
| 0x11 | Armed state (armed_away, arming, disarmed, pending) |
| 0x12 | Zone state (triggered, alarmed, bypassed) |
| 0x14 | Keypad beep command (panel → keypad) |
| 0x15 | Keypad state (normal, installer, programming) |
| 0x20 | Memory event broadcast (event # logged; partial decode — see field notes) |
| 0x50 | Output state |
| 0x23 | Panel info — hardware / firmware |
| 0x54 | `LCD_CONTENT` — raw hex via `panel_lcd`; byte layout notes in field docs |
| 0xA1 | Keypress (keypad → panel; also used for ESP transmit) |
| 0xD2 | Memory cleared |

### Opcodes seen on bus, not yet handled

| Type | Description |
|------|-------------|
| 0x1E | Chime status reply after CHIME key (`byte 3`: `0x01`=on, `0x00`=off) |
| 0xA9 | Memory LCD preview before `0x20` during MEM scroll |
| 0xA0 | Exit memory mode (`a0.00.01`) |
| 0xFE | End burst after memory timeout |

### Field notes — Arrowhead Elite-S (PCB **V908.3**, bus **v908** / fw **v2.1**)

Validated on live bus captures (`keypad-bus-notes.md` in project repo).

#### `0x10` panel status (`00.B1.B2.00.00`)

| Byte 1 | Meaning (live) |
|--------|----------------|
| `0x00` | AC mains OK |
| `0x02` | AC just lost (~first seconds after power cut) |
| `0x03` | AC fail + battery low (sustained on battery; same byte after AC cut) |
| `0x01` | Transition when AC restoring |

| Byte 2 | Meaning |
|--------|---------|
| `0xC1` | Panel ready |
| `0xC0` | Not ready (zone open) |

**Power cycle vs MEM log order:** live `02→03` happens first; MEM logs "AC fail" later while still on `03`; "Low battery" MEM entry comes after that (same live `03`); restore is live `01→00` then MEM restore chain.

**Component mapping:** `mains_power` ON when byte1 is `0x02` or `0x03`. `battery_state` when byte1 is `0x03` (battery low). Only field-tested with AC fail + batt low together; AC OK + batt low not tested (rare).

#### `0x23` panel info

Stable payload: `00.03.8C.02.01.00.23.03`

| Bytes | Decode |
|-------|--------|
| 1–2 | Hardware **v908** (`0x038C` BE16) |
| 3–4 | Firmware **v2.1** (best guess) |
| 6–7 | Frame tail `23.03` (required for valid frame) |

PCB sticker reads **V908.3**. Bus reports **908** only; trailing `0x03` may be revision or protocol marker — unconfirmed.

#### `0x20` memory events (9-byte payload)

| Byte | Partial decode |
|------|----------------|
| 1 | Event slot (`0xC8 + event# − 200`) |
| 2 | Event type: `6E` AC fail, `1A` low batt, `D7`/`19` restore, `97` batt OK, `70` live fault screen, `2B` armed, `5C` open by user |
| 3–5 | Timestamp chunk (same LCD time → same bytes; not converted to clock yet) |
| 6–8 | Era flags: `34.84…` on battery, `68.08…` after AC restore |

Historical `0x20` traffic only during MEM browse — not on idle bus.

#### Crow keypad `0xA1` key bytes (Elite-S)

| Button | Bus byte |
|--------|----------|
| 0–9 | `0x00`–`0x09` |
| PROGRAM / ENTER | `0x10` / `0x11` |
| CHIME | `0x14` |
| MEM | `0x15` |
| CONTROL | `0x16` |
| ARM / STAY | `0x0D` / `0x0E` *(constants in code, field-unconfirmed)* |
| Unknown | `0x23` / `0x24` *(A/B?)* |

## Development

### Local Development Setup

Follow the esphome development guide to set up a local environment. You can use this to test that your code compiles.
I then set up the custom component on my regular esphome install via git and pulled it in that to test on a live device.

## Troubleshooting

### Component Not Found

If ESPHome can't find the component:
- Check the `external_components` path is correct
- Verify the component name is `crow_alarm_panel`
- Try adding `refresh: 0s` to always fetch latest version

### No Data Received

If no messages are received from the alarm panel:
- Verify wiring (clock and data pins)
- Check ground connection
- Ensure keypad address is correct
