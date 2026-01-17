# Bionic Flower

ESP32-basierte interaktive Blume mit LEDs, Schrittmotor und Sensoren. Vollständige Home Assistant Integration via MQTT.

## Features

### MQTT Home Assistant Integration

Die Blume wird automatisch in Home Assistant erkannt (MQTT Auto-Discovery).

**Entitäten:**
- `light.bionic_flower` - LED-Steuerung mit Helligkeit, Farbe und Effekten
- `cover.bionic_flower` - Motor-Steuerung (öffnen/schließen)
- `select.bionic_flower_mode` - Modus-Auswahl (Automatic/Manual)
- `sensor.bionic_flower_illuminance` - Lichtsensor (%)
- `sensor.bionic_flower_proximity` - Distanzsensor (%)
- `sensor.bionic_flower_temperature` - Temperatursensor (°C)
- `binary_sensor.bionic_flower_touch_left` - Touch links
- `binary_sensor.bionic_flower_touch_right` - Touch rechts

### LED-Effekte

| Effekt | Beschreibung |
|--------|--------------|
| **None** | Statische Farbe (RGB wählbar) |
| **Rainbow** | Regenbogen-Animation (alle LEDs gleiche Farbe) |
| **Rainbow Multi** | Regenbogen mit individuellen LED-Farben |
| **Circadian** | Tageslicht-Simulation basierend auf Uhrzeit |
| **Weather** | Wetter-Visualisierung mit Motor-Steuerung |

### Circadian-Modus

Passt Farbtemperatur automatisch an die Tageszeit an:

| Uhrzeit | Farbstimmung |
|---------|--------------|
| 06:00 - 09:00 | Warmes Orange (Sonnenaufgang) |
| 09:00 - 12:00 | Warmes Weiß |
| 12:00 - 17:00 | Kühles Tageslicht |
| 17:00 - 20:00 | Goldene Stunde |
| 20:00 - 22:00 | Warmes Amber |
| 22:00 - 06:00 | Gedimmtes Rot (Nacht) |

**Home Assistant Automation:**
```yaml
automation:
  - alias: "Bionic Flower Circadian Update"
    trigger:
      - platform: time_pattern
        minutes: "/30"
    action:
      - service: mqtt.publish
        data:
          topic: "bionic_flower/circadian/hour"
          payload: "{{ now().hour }}"
```

### Weather-Modus

Visualisiert Home Assistant Wetter-Daten mit einzigartigen LED-Animationen und Motor-Positionen:

| Wetter | LEDs | Motor |
|--------|------|-------|
| `sunny` | Warmes Gelb, pulsierend | 100% offen |
| `clear-night` | Dunkelblau mit funkelnden Sternen | 100% offen |
| `partlycloudy` | Gelb/Grau abwechselnd | 75% offen |
| `cloudy` | Graue Wolken wandern | 50% offen |
| `fog` | Blasses Weiß, langsames Atmen | 50% offen |
| `windy` | Cyan, schnelles Hin-und-Her | 50% offen |
| `rainy` | Blaue Regentropfen fallen | Geschlossen |
| `pouring` | Schnelle, intensive Regentropfen | Geschlossen |
| `lightning` | Grau mit weißen Blitzen | Geschlossen |
| `lightning-rainy` | Regen + Blitze kombiniert | Geschlossen |
| `snowy` | Weißes Glitzern | Geschlossen |
| `snowy-rainy` | Weiß/Blau Tropfen | Geschlossen |
| `hail` | Weißes Flackern | Geschlossen |
| `exceptional` | Rainbow Multi Effekt | 100% offen |

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

### Touch-Steuerung (Manual-Modus)

Im Manual-Modus können die Touch-Sensoren zur lokalen Steuerung verwendet werden:

| Aktion | Funktion |
|--------|----------|
| **Links kurz** | Vorheriger Effekt |
| **Links lang** (>500ms) | Helligkeit verringern |
| **Rechts kurz** | Nächster Effekt |
| **Rechts lang** (>500ms) | Helligkeit erhöhen |
| **Beide gleichzeitig** | LEDs ein/ausschalten |

Effekt-Reihenfolge: None → Rainbow → Rainbow Multi → Circadian → Weather → None ...

## Konfiguration

Die MQTT-Einstellungen befinden sich in `src/Settings.h`:

```cpp
#define MQTT_BROKER "192.168.x.x"
#define MQTT_PORT 1883
#define MQTT_USER "mqtt"
#define MQTT_PASSWORD "your_password"
```

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
- **5x WS2812B** RGB-LEDs (GPIO 16)
- **Schrittmotor** mit A4988 Treiber
- **RPR-0521RS** Licht-/Proximity-Sensor (I2C)
- **CAP1203** Touch-Sensor (I2C, Adresse 0x28)

## MQTT Topics

### Subscriptions (eingehend)
- `bionic_flower/light/set` - LED-Befehle (JSON)
- `bionic_flower/cover/set` - OPEN/CLOSE/STOP
- `bionic_flower/cover/set_position` - 0-100%
- `bionic_flower/select/mode/set` - Automatic/Manual
- `bionic_flower/circadian/hour` - Stunde (0-23)
- `bionic_flower/weather/state` - Wetter-Zustand
- `bionic_flower/weather/temperature` - Temperatur (°C)

### Publications (ausgehend)
- `bionic_flower/light/state` - LED-Status (JSON)
- `bionic_flower/cover/state` - open/closed/stopped
- `bionic_flower/cover/position` - 0-100%
- `bionic_flower/select/mode/state` - Automatic/Manual
- `bionic_flower/sensor/illuminance` - Helligkeit (%)
- `bionic_flower/sensor/proximity` - Distanz (%)
- `bionic_flower/sensor/temperature` - Temperatur (°C)
- `bionic_flower/binary_sensor/touch_left` - ON/OFF
- `bionic_flower/binary_sensor/touch_right` - ON/OFF
