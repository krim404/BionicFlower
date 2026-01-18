# Bionic Flower

ESP32-based interactive flower with LEDs, stepper motor and sensors. Full Home Assistant integration via MQTT.

> **Note:** The original code is from [Festo Bionics4Education](https://github.com/Festo-se/Bionics4Education). This repository contains modifications for Home Assistant MQTT integration, WiFi station mode, and additional LED effects.

## Features

### MQTT Home Assistant Integration

The flower is automatically discovered in Home Assistant (MQTT Auto-Discovery).

**Entities:**
- `light.bionic_flower` - LED control with brightness, color and effects
- `cover.bionic_flower` - Motor control (open/close)
- `select.bionic_flower_mode` - Mode selection (Automatic/Manual)
- `switch.bionic_flower_adaptive_brightness` - Adaptive brightness based on ambient light
- `sensor.bionic_flower_illuminance` - Light sensor (%)
- `sensor.bionic_flower_proximity` - Distance sensor (%)
- `sensor.bionic_flower_temperature` - Temperature sensor (°C)
- `binary_sensor.bionic_flower_touch_left` - Touch left
- `binary_sensor.bionic_flower_touch_right` - Touch right

### LED Effects

| Effect | Description |
|--------|-------------|
| **None** | Static color (RGB selectable) |
| **Rainbow** | Rainbow animation (all LEDs same color) |
| **Rainbow Multi** | Rainbow with individual LED colors |
| **Circadian** | Daylight simulation based on time of day |
| **Weather** | Weather visualization with motor control |
| **Sensor** | Motor reacts to ambient light (opens in light, closes in dark) |

### Adaptive Brightness

When enabled (default: ON), LED brightness automatically adjusts based on ambient light:
- 1% ambient light → 5% LED brightness
- 9%+ ambient light → 100% LED brightness
- Linear scaling in between

Updates every 15 seconds. Can be toggled via `switch.bionic_flower_adaptive_brightness`.

### Circadian Mode

Automatically adjusts color temperature to the time of day using NTP:

| Time | Color Mood |
|------|------------|
| 06:00 - 09:00 | Warm orange (sunrise) |
| 09:00 - 12:00 | Warm white |
| 12:00 - 17:00 | Cool daylight |
| 17:00 - 20:00 | Golden hour |
| 20:00 - 22:00 | Warm amber |
| 22:00 - 06:00 | Dimmed red (night) |

Time is automatically synchronized via NTP (with automatic DST handling). Configure your timezone in `Credentials.h`.

### Weather Mode

Visualizes Home Assistant weather data with unique LED animations and motor positions:

| Weather | LEDs | Motor |
|---------|------|-------|
| `sunny` | Warm yellow, pulsing | 100% open |
| `clear-night` | Dark blue with twinkling stars | 100% open |
| `partlycloudy` | Yellow/gray alternating | 75% open |
| `cloudy` | Gray clouds drifting | 50% open |
| `fog` | Pale white, slow breathing | 50% open |
| `windy` | Cyan, fast back-and-forth | 50% open |
| `rainy` | Blue raindrops falling | Closed |
| `pouring` | Fast, intense raindrops | Closed |
| `lightning` | Gray with white flashes | Closed |
| `lightning-rainy` | Rain + lightning combined | Closed |
| `snowy` | White glittering | Closed |
| `snowy-rainy` | White/blue drops | Closed |
| `hail` | White flickering | Closed |
| `exceptional` | Rainbow Multi effect | 100% open |

**Home Assistant Automation:**
```yaml
automation:
  - alias: "Bionic Flower Weather Update"
    trigger:
      - platform: state
        entity_id: weather.home
    action:
      - service: mqtt.publish
        data:
          topic: "bionic_flower/weather/state"
          payload: "{{ states('weather.home') }}"
      - service: mqtt.publish
        data:
          topic: "bionic_flower/weather/temperature"
          payload: "{{ state_attr('weather.home', 'temperature') }}"
```

### Touch Control

The touch sensors can be used for local control:

| Action | Function |
|--------|----------|
| **Left short** | Previous effect |
| **Left long** (>500ms) | Decrease brightness |
| **Right short** | Next effect |
| **Right long** (>500ms) | Increase brightness |
| **Both simultaneously** | Toggle LEDs on/off |

Effect order: None → Rainbow → Rainbow Multi → Circadian → Weather → Sensor → None ...

## Setup

1. **Create credentials file:**
   ```bash
   cp src/Credentials.h.example src/Credentials.h
   ```

2. **Enter your values** in `src/Credentials.h`:
   ```cpp
   // WiFi - Your network
   #define WIFI_SSID "your_wifi_name"
   #define WIFI_PASSWORD "your_wifi_password"

   // MQTT - Your Home Assistant broker
   #define MQTT_BROKER "192.168.x.x"
   #define MQTT_PORT 1883
   #define MQTT_USER "mqtt"
   #define MQTT_PASSWORD "your_mqtt_password"

   // Timezone (for Circadian effect)
   // See: https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
   #define NTP_TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"  // Europe/Berlin
   ```

3. **Home Assistant MQTT broker** must be installed and configured
   - Mosquitto add-on or external MQTT broker
   - MQTT integration enabled in Home Assistant

## Build

```bash
# PlatformIO
pio run

# Upload
pio run -t upload

# Serial Monitor
pio device monitor
```

## Hardware

- **ESP32** DevKit
- **5x WS2812B** RGB LEDs (GPIO 16)
- **Stepper motor** with A4988 driver
- **RPR-0521RS** Light/proximity sensor (I2C)
- **CAP1203** Touch sensor (I2C, address 0x28)

## MQTT Topics

### Subscriptions (incoming)
- `bionic_flower/light/set` - LED commands (JSON)
- `bionic_flower/cover/set` - OPEN/CLOSE/STOP
- `bionic_flower/cover/set_position` - 0-100%
- `bionic_flower/select/mode/set` - Automatic/Manual
- `bionic_flower/switch/adaptive_brightness/set` - ON/OFF
- `bionic_flower/weather/state` - Weather state
- `bionic_flower/weather/temperature` - Temperature (°C)

### Publications (outgoing)
- `bionic_flower/light/state` - LED status (JSON)
- `bionic_flower/cover/state` - open/closed/stopped
- `bionic_flower/cover/position` - 0-100%
- `bionic_flower/select/mode/state` - Automatic/Manual
- `bionic_flower/switch/adaptive_brightness/state` - ON/OFF
- `bionic_flower/sensor/illuminance` - Brightness (%)
- `bionic_flower/sensor/proximity` - Distance (%)
- `bionic_flower/sensor/temperature` - Temperature (°C)
- `bionic_flower/binary_sensor/touch_left` - ON/OFF
- `bionic_flower/binary_sensor/touch_right` - ON/OFF

## Credits

Original code by [Festo Bionics4Education](https://github.com/Festo-se/Bionics4Education)
