# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Projektübersicht

Bionic Flower ist ein ESP32-basiertes IoT-Projekt für eine interaktive, mechanische Blume mit LEDs, Schrittmotor, Touch-Sensor und Lichtsensor. Das Projekt enthält sowohl die Hauptfirmware mit Web-Interface als auch didaktische DIY-Beispiele für Bildungszwecke.

## Build & Upload

Das Projekt verwendet die Arduino IDE / PlatformIO für ESP32:

```bash
# Arduino IDE: Öffne flower.ino und wähle ESP32 als Board
# Baud-Rate für Serial Monitor: 115200
```

**Benötigte Bibliotheken:**
- FastLED (LED-Steuerung)
- ESPAsyncWebServer (Web-Interface)
- Wire (I2C-Kommunikation)

## Architektur

### Haupt-Firmware (flower.ino)

Die Firmware folgt einem Service-basierten Architekturmuster:

```
flower.ino (Entry Point)
    └── WebService (Koordiniert alle Services)
            ├── WiFiService (Access Point: 192.168.4.1:80)
            ├── DNSService (Captive Portal)
            └── HardwareService (Singleton, steuert alle Hardware)
                    ├── MotorLogic (Schrittmotor-Steuerung)
                    ├── RPR0521RS (Licht-/Proximity-Sensor via I2C)
                    ├── CAP1203 (Touch-Sensor via I2C)
                    └── FastLED (5 RGB-LEDs)
```

### Hardware-Konfiguration (Settings.h)

| Komponente | GPIO / Pin |
|------------|------------|
| LEDs (5x Neopixel) | GPIO 16 |
| I2C SDA | GPIO 4 |
| I2C SCL | GPIO 5 |
| Touch-Sensor I2C | 0x28 |
| Motor DIR | GPIO 33 |
| Motor STEP | GPIO 25 |
| Motor SLEEP | GPIO 13 |

### Datenmodelle (Models.h)

- `Color`: RGB-Farbwerte mit Hex-Konvertierung
- `Configuration`: Motor-Position, Schwellenwerte, Autonomie-Modus, Farbe
- `SensorData`: Helligkeits-, Distanz- und Touch-Werte

### DIY-Beispiele (für Bildungszwecke)

Das Verzeichnis `DIY_projects/` enthält schrittweise Anleitungen mit steigender Komplexität:
- **Start_Coding_yourself/**: Einfache Beispiele für einzelne Sensoren/Aktoren
- **Bee, Photosynthesis, etc.**: Thematische Projekte mit Markdown-Anleitungen (DE/EN) und Arduino-Sketches

DIY-Beispiele verwenden die `BasicStepperDriver`-Bibliothek für einfachere Motor-Steuerung, während die Haupt-Firmware die eigene `MotorLogic`-Klasse nutzt.

## Wichtige Konstanten

```cpp
MOTOR_FULL_STEP_COUNT = 14000  // Vollständiges Öffnen/Schließen
MOTOR_POSITION_OPEN = 1.0f
MOTOR_POSITION_CLOSED = 0.0f
LED_COUNT = 5
```
