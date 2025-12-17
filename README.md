# Crow Alarm Panel - ESPHome Custom Component

ESPHome custom component for integrating Crow/Arrowhead alarm panels with Home Assistant.

## Features

- **Zone Monitoring**: Binary sensors for motion detection and bypass status
- **Armed State Detection**: Text sensor showing alarm state (armed_away, armed_stay, disarmed, etc.)
- **Output Control**: Switches for controlling alarm panel outputs
- **Keypress Simulation**: Send commands via the keypad bus
- **Interrupt-Driven**: Efficient ISR-based serial communication

## Installation

### Method 1: External Components (Recommended)

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
  id: alarm
  clock_pin: GPIO18
  data_pin: GPIO19
  address: 8  # Your keypad address

  keypads:
    - name: "Front Keypad"
      address: 4

# Binary sensors for zones
binary_sensor:
  - platform: crow_alarm_panel
    type: motion
    zone: 1
    name: "Front Door"

  - platform: crow_alarm_panel
    type: bypass
    zone: 1
    name: "Front Door Bypassed"

# Text sensor for armed state
text_sensor:
  - platform: crow_alarm_panel
    type: armed_state
    name: "Alarm Status"

# Switch for outputs
switch:
  - platform: crow_alarm_panel
    type: output
    output: 1
    name: "Siren"
```

### Automation Actions (Coming Soon)

Once arm/disarm commands are implemented, you'll be able to use:

```yaml
button:
  - platform: template
    name: "Arm Away"
    on_press:
      - crow_alarm_panel.arm_away: alarm

  - platform: template
    name: "Disarm"
    on_press:
      - crow_alarm_panel.disarm:
          id: alarm
          code: !secret alarm_code
```

## Hardware Setup

### Wiring

Connect your ESP32 to the alarm panel keypad bus:

- **Clock Pin (GPIO18)**: Connect to alarm panel clock line
- **Data Pin (GPIO19)**: Connect to alarm panel data line
- **Ground**: Common ground between ESP32 and alarm panel
- **Power**: Power ESP32 separately (do not power from alarm panel)

### Recommended Hardware

- ESP32 development board
- Logic level converter (if alarm panel uses 12V logic)
- Optocouplers for electrical isolation (recommended)

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
| 0x11 | Armed state (armed_away, arming, disarmed, pending) |
| 0x12 | Zone state (triggered, alarmed, bypassed) |
| 0x14 | Keypad command (beep commands) |
| 0x15 | Keypad state (normal, installer, programming) |
| 0x50 | Output state |
| 0x54 | Current time/date |
| 0xD1 | Keypress from physical keypad |
| 0xD2 | Memory cleared |

## Development

### Local Development Setup

```bash
git clone https://github.com/yourusername/esphome-crow-alarm.git
cd esphome-crow-alarm
```

### Making Changes

```bash
# Make your changes
git add .
git commit -m "feat: Add new feature"
git push
```

### Testing

Use ESPHome's local component loading during development:

```yaml
external_components:
  - source:
      type: local
      path: /path/to/esphome-crow-alarm
    components: [ crow_alarm_panel ]
```

## Roadmap

- [x] Zone monitoring
- [x] Armed state detection
- [x] Output control (read-only)
- [x] Keypress event monitoring
- [ ] Arm/disarm commands
- [ ] Output control (write)
- [ ] Zone bypass control
- [ ] Time synchronization
- [ ] Memory event log reading

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
- Monitor with logic analyzer to confirm bus activity

### Bus Collisions

If commands interfere with panel operation:
- Increase `MIN_TX_INTERVAL_MS` in component
- Check for proper bus idle detection
- Verify only one device is transmitting at a time

## License

MIT License

Copyright (c) 2024

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Credits

Based on reverse engineering of Crow/Arrowhead alarm panel communication protocol.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'feat: Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request
