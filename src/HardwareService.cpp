
// MARK: Includes

#include "HardwareService.h"
#include "MQTTService.h"
#include <time.h>

// MARK: Constants

const String PRINT_PREFIX = "[HW]: ";
const uint32_t MAX_MEASUREMENT_COUNT = 20;
const float MAX_BRIGHTNESS = 4095;
const float MAX_DISTANCE = 4095;
const float MAX_REOPENCYCLES_LIGHT = 5;
const float MAX_REOPENCYCLES_TOUCH = 5;
const float MAX_REOPENCYCLES_DISTANCE = 5;

// MARK: Variables

HardwareService* shared_instance;

// MARK: Initialization

HardwareService::HardwareService() {
  motor_calibration_finished = false;
  ambient_brightness = DEFAULT_AMBIENT_BRIGHTNESS;
  light_measurement_count = MAX_MEASUREMENT_COUNT;

  for (int i = 0; i < LED_COUNT; i++) {
    leds[i] = CRGB::Black;
  }

  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, LED_COUNT);
  delay(500);

  Wire.begin(I2C_SDA, I2C_SCL, 100000);
  delay(500);

  pinMode(32, OUTPUT);
  digitalWrite(32, LOW);
  pinMode(27, OUTPUT);
  digitalWrite(27, LOW);
  pinMode(14, OUTPUT);
  digitalWrite(14, LOW);
  pinMode(12, OUTPUT);
  digitalWrite(12, LOW);
  pinMode(15, OUTPUT);
  digitalWrite(15, LOW);
  pinMode(2, OUTPUT);
  digitalWrite(2, LOW);

  // GPIO Pins

  pinMode(21, OUTPUT);
  digitalWrite(21, LOW);
  pinMode(22, OUTPUT);
  digitalWrite(22, LOW);
  pinMode(23, OUTPUT);
  digitalWrite(23, LOW);

  // Input-only Pins

  digitalWrite(34, LOW);
  digitalWrite(35, LOW);

  configuration.motor_position = MOTOR_POSITION_CLOSED; // Flower State
  configuration.lower_brightness_threshold = DEFAULT_LOWER_BRIGHTNESS_THRESHOLD; // Brightness Threshold
  configuration.upper_brightness_threshold = DEFAULT_UPPER_BRIGHTNESS_THRESHOLD; // Brightness Threshold
  configuration.distance_threshold = DEFAULT_DISTANCE_THRESHOLD; // Distance Threshold
  configuration.is_autonomous = DEFAULT_AUTONOMY_VALUE == 1; // Is Autonomy on?
  configuration.color = { 0, 145, 220 };

  sensor_data.has_light_sensor = light_sensor.init() == 0;
  sensor_data.has_touch_sensor = touch_sensor.begin();
  reopen_cycle_count = 0;
  rgb_hue = 0;

  // Touch handling init
  touch_left_start = 0;
  touch_right_start = 0;
  touch_left_was_pressed = false;
  touch_right_was_pressed = false;
  touch_left_long_triggered = false;
  touch_right_long_triggered = false;

  // Adaptive brightness init
  last_adaptive_brightness_update = 0;
  adaptive_brightness_factor = 255;  // Start at full brightness

  // NVS debouncing init
  nvs_save_requested_at = 0;
  nvs_save_pending = false;

  motor.setupPins();
}

// MARK: Static Functions

HardwareService* HardwareService::getSharedInstance() {
  if (shared_instance == nullptr) {
    shared_instance = new HardwareService();
  }
  return shared_instance;
}

// MARK: Methods

boolean HardwareService::start() {
  sensor_timer.detach();

  // Load saved state from NVS before showing LED indicator
  loadStateFromNVS();

  writeLED({ 0, 255, 0 });
  delay(500);
  writeLED({ 255, 0, 0 });
  delay(500);
  writeLED({ 0, 0, 255 });
  delay(500);
  writeLED(configuration.color);

  Serial.println(PRINT_PREFIX + "Calibrating motor...");

  // Store intended position from NVS, then calibrate to closed
  float saved_position = configuration.motor_position;
  intended_motor_position = MOTOR_POSITION_OPEN;
  move(MOTOR_POSITION_CLOSED, MOTOR_SPEED_FAST);
  delay((1000 * MOTOR_FULL_STEP_COUNT * MOTOR_SPEED_FAST) + 20);

  while (!motor.isCalibrated()) {
    Serial.println(PRINT_PREFIX + "Still not calibrated.");
    delay(100);
  }

  motor_calibration_finished = true;
  Serial.println(PRINT_PREFIX + "Motor calibration done.");

  // Move to saved position after calibration
  if (saved_position > 0.01f) {
    Serial.println(PRINT_PREFIX + "Moving to saved position: " + String(saved_position));
    move(saved_position, MOTOR_SPEED_FAST);
    configuration.motor_position = saved_position;
  }

  return true;
}

void HardwareService::setConfiguration(Configuration new_configuration) {
  Serial.print(PRINT_PREFIX + "New Configuration");
  Serial.print(": Motor Position: " + String(new_configuration.motor_position));
  Serial.print(", Lower Brightness Threshold: " + String(new_configuration.lower_brightness_threshold));
  Serial.print(", Upper Brightness Threshold: " + String(new_configuration.upper_brightness_threshold));
  Serial.print(", Distance Threshold: " + String(new_configuration.distance_threshold));
  Serial.print(", Red: " + String(new_configuration.color.red));
  Serial.print(", Green: " + String(new_configuration.color.green));
  Serial.print(", Blue: " + String(new_configuration.color.blue));
  Serial.println();

  boolean color_changed = (new_configuration.color.red != configuration.color.red)
      || (new_configuration.color.blue != configuration.color.blue)
      || (new_configuration.color.green != configuration.color.green);

  writeLED(new_configuration.color);

  // Motor position change via MQTT/Web
  MQTTService* mqtt = MQTTService::getSharedInstance();
  bool sensor_effect_active = mqtt->isSensorEnabled();
  bool weather_effect_active = mqtt->isWeatherEnabled();

  // Only allow manual motor control if Sensor and Weather effects are not active
  if (((new_configuration.motor_position != configuration.motor_position) || (new_configuration.speed != configuration.speed))
      && !sensor_effect_active && !weather_effect_active && !color_changed) {

    // The correct way to calculate the speed would be:
    // MOTOR_SPEED_FAST + ((1 - new_configuration.speed) * (MOTOR_SPEED_SLOW - MOTOR_SPEED_FAST));
    // but since Ticker is limited to a time granularity of 0.001, we cannot set any value between 0.001 and 0.002
    float motor_speed = (new_configuration.speed < 0.5) ? MOTOR_SPEED_SLOW : MOTOR_SPEED_FAST;
    move(new_configuration.motor_position, motor_speed);
  }

  this->configuration = new_configuration;

  // Save state to NVS
  saveStateToNVS();
}

void HardwareService::resetSensorData() {
  light_measurement_count = 0;
}

void HardwareService::writeLED(Color color) {
  for (int i = 0; i < LED_COUNT; i++) {
    leds[i].setRGB(color.red, color.green, color.blue);
  }

  FastLED.show();
}

void HardwareService::loop(const boolean has_active_connection, uint32_t loop_counter) {
  if ((loop_counter % 6) == 0) {
    updateMotor();
  } else {
    readSensors();
  }

  // Update adaptive brightness (every 15s internally)
  updateAdaptiveBrightness();

  // Check if NVS save is pending (debounced)
  checkPendingNVSSave();

  // Check MQTT light state
  MQTTService* mqtt = MQTTService::getSharedInstance();
  bool light_on = mqtt->isLightOn();
  bool rainbow_enabled = mqtt->isRainbowEnabled();
  bool rainbow_multi_enabled = mqtt->isRainbowMultiEnabled();
  bool circadian_enabled = mqtt->isCircadianEnabled();
  bool weather_enabled = mqtt->isWeatherEnabled();

  // Apply adaptive brightness to MQTT brightness setting
  uint8_t raw_mqtt_brightness = mqtt->getBrightness();
  uint8_t mqtt_brightness = (raw_mqtt_brightness * adaptive_brightness_factor) / 255;

  // Sensor effect state for touch handling
  bool sensor_enabled = mqtt->isSensorEnabled();

  // Touch handling (always active)
  // Left touch: Toggle light on/off
  // Right touch: Cycle through effects
  if (sensor_data.has_touch_sensor) {
    // Left touch: toggle light on/off
    if (sensor_data.touch_left && !touch_left_was_pressed) {
      touch_left_was_pressed = true;
      mqtt->setLightOn(!light_on);
      mqtt->publishLightState();
      light_on = mqtt->isLightOn();
    } else if (!sensor_data.touch_left) {
      touch_left_was_pressed = false;
    }

    // Right touch: cycle effects (None -> Rainbow -> Rainbow Multi -> Circadian -> Weather -> Sensor -> None)
    if (sensor_data.touch_right && !touch_right_was_pressed) {
      touch_right_was_pressed = true;
      if (rainbow_enabled) {
        mqtt->setRainbowEnabled(false);
        mqtt->setRainbowMultiEnabled(true);
      } else if (rainbow_multi_enabled) {
        mqtt->setRainbowMultiEnabled(false);
        mqtt->setCircadianEnabled(true);
      } else if (circadian_enabled) {
        mqtt->setCircadianEnabled(false);
        mqtt->setWeatherEnabled(true);
      } else if (weather_enabled) {
        mqtt->setWeatherEnabled(false);
        mqtt->setSensorEnabled(true);
      } else if (sensor_enabled) {
        mqtt->setSensorEnabled(false);
        // None - static color
      } else {
        // None -> Rainbow
        mqtt->setRainbowEnabled(true);
      }
      mqtt->publishLightState();

      // Re-read effect states
      rainbow_enabled = mqtt->isRainbowEnabled();
      rainbow_multi_enabled = mqtt->isRainbowMultiEnabled();
      circadian_enabled = mqtt->isCircadianEnabled();
      weather_enabled = mqtt->isWeatherEnabled();
      sensor_enabled = mqtt->isSensorEnabled();
    } else if (!sensor_data.touch_right) {
      touch_right_was_pressed = false;
    }
  }

  // Weather motor control - runs independently of LED state
  if (weather_enabled) {
    String weather_state = mqtt->getWeatherState();
    float target_position = MOTOR_POSITION_OPEN; // Default: open (sunny)

    if (weather_state == "rainy" || weather_state == "pouring" ||
        weather_state == "lightning" || weather_state == "lightning-rainy" ||
        weather_state == "hail" || weather_state == "snowy" || weather_state == "snowy-rainy") {
      target_position = MOTOR_POSITION_CLOSED;
    } else if (weather_state == "partlycloudy") {
      target_position = 0.75f; // 75% open
    } else if (weather_state == "cloudy" || weather_state == "fog" ||
               weather_state == "windy" || weather_state == "windy-variant") {
      target_position = 0.5f;
    }

    // Move motor to target position if different
    if (abs(configuration.motor_position - target_position) > 0.01f) {
      move(target_position, MOTOR_SPEED_FAST);
      configuration.motor_position = target_position;
    }
  }

  // Circadian motor control - gradual open/close based on time of day
  if (circadian_enabled) {
    int preview_hour = mqtt->getCircadianPreviewHour();
    float target_position = MOTOR_POSITION_OPEN;

    if (preview_hour >= 0) {
      // Preview mode: use preview hour for motor position
      int hour = preview_hour;
      if (hour >= 22 || hour < 7) {
        target_position = MOTOR_POSITION_CLOSED;
      } else if (hour >= 7 && hour < 10) {
        // Gradual opening: 7:00=0%, 10:00=100%
        target_position = (float)(hour - 7) / 3.0f;
      } else if (hour >= 10 && hour < 19) {
        target_position = MOTOR_POSITION_OPEN;
      } else {
        // Gradual closing: 19:00=100%, 22:00=0%
        target_position = 1.0f - ((float)(hour - 19) / 3.0f);
      }
    } else {
      // Real time mode: use actual time with minute precision for 10-min steps
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        int hour = timeinfo.tm_hour;
        int minute = timeinfo.tm_min;

        if (hour >= 22 || hour < 7) {
          target_position = MOTOR_POSITION_CLOSED;
        } else if (hour >= 7 && hour < 10) {
          // Gradual opening 07:00-10:00 in 10-min steps (18 steps total)
          int minutes_since_7 = (hour - 7) * 60 + minute;
          int step = minutes_since_7 / 10;  // 0-17
          target_position = (float)step / 18.0f;
        } else if (hour >= 10 && hour < 19) {
          target_position = MOTOR_POSITION_OPEN;
        } else {
          // Gradual closing 19:00-22:00 in 10-min steps (18 steps total)
          int minutes_since_19 = (hour - 19) * 60 + minute;
          int step = minutes_since_19 / 10;  // 0-17
          target_position = 1.0f - ((float)step / 18.0f);
        }
      }
    }

    // Move motor to target position if different
    if (abs(configuration.motor_position - target_position) > 0.01f) {
      move(target_position, MOTOR_SPEED_FAST);
      configuration.motor_position = target_position;
    }
  }

  // If light is turned off via MQTT, turn off LEDs
  if (!light_on) {
    writeLED({ 0, 0, 0 });
  }
  // Turn off LEDs when light sensor detects darkness (only in Sensor effect mode)
  else if (sensor_enabled && sensor_data.has_light_sensor && sensor_data.brightness <= configuration.lower_brightness_threshold) {
    writeLED({ 0, 0, 0 });
  } else if (sensor_enabled) {
    // Sensor effect: static color from configuration, scaled by MQTT brightness
    Color scaled_color;
    scaled_color.red = (configuration.color.red * mqtt_brightness) / 255;
    scaled_color.green = (configuration.color.green * mqtt_brightness) / 255;
    scaled_color.blue = (configuration.color.blue * mqtt_brightness) / 255;
    writeLED(scaled_color);
  } else if (weather_enabled) {
    // Weather mode: Individual animations for each weather state
    String weather_state = mqtt->getWeatherState();

    // Individual weather animations
    // Luminance: white/gray ~2x, yellow/cyan ~1.5x, blue needs ~3x boost
    if (weather_state == "sunny") {
      // Rich golden yellow with visible breathing effect
      // Use millis() for time-based animation (loop_counter is too slow at 10Hz)
      // ~4 second cycle = comfortable, visible breathing
      uint8_t phase = (uint8_t)((millis() / 15) % 256);  // ~4 second cycle
      uint8_t sine = sin8(phase);  // 0-255
      // Breathing: 50%-100% range for visible but not too strong effect
      uint8_t breath = (sine * 5 / 10) + 128;  // 128-255 (50%-100%)
      uint8_t r = (uint8_t)((255UL * mqtt_brightness * breath) / (255UL * 255));
      uint8_t g = (uint8_t)((180UL * mqtt_brightness * breath) / (255UL * 255));  // Golden orange
      uint8_t b = (uint8_t)((30UL * mqtt_brightness * breath) / (255UL * 255));   // Saturated
      writeLED({ r, g, b });

    } else if (weather_state == "clear-night") {
      // Dark blue base with twinkling stars
      // Blue needs ~3x intensity, white ~0.5x (R+G+B combined is very bright)
      for (int i = 0; i < LED_COUNT; i++) {
        bool is_star = ((loop_counter + i * 50) % 120 < 8) || (random(100) < 2);
        if (is_star) {
          // Twinkling star: warm white, heavily reduced (white is 2x brighter)
          uint8_t val = (80 * mqtt_brightness) / 255;
          leds[i] = CRGB(val, (uint8_t)((val * 95) / 100), (uint8_t)((val * 80) / 100));
        } else {
          // Deep blue night sky: boosted for perceived brightness
          uint8_t b = (200 * mqtt_brightness) / 255;  // Blue boosted
          uint8_t r = (15 * mqtt_brightness) / 255;
          uint8_t g = (30 * mqtt_brightness) / 255;
          leds[i] = CRGB(r, g, b);
        }
      }
      FastLED.show();

    } else if (weather_state == "cloudy") {
      // Gray colors slowly drifting across LEDs
      // Gray/white is ~2x brighter, reduce by half
      uint8_t wave_pos = (loop_counter / 3) % (LED_COUNT * 2);
      for (int i = 0; i < LED_COUNT; i++) {
        uint8_t dist = abs((int)wave_pos - i - LED_COUNT);
        uint8_t brightness_mod = 255 - (dist * 30);
        if (brightness_mod > 255) brightness_mod = 100;
        uint8_t gray = (brightness_mod * mqtt_brightness) / (255 * 2);  // Halved for white
        // Slight blue tint for cloudy sky
        leds[i] = CRGB((uint8_t)((gray * 85) / 100), gray, (uint8_t)((gray * 120) / 100));
      }
      FastLED.show();

    } else if (weather_state == "partlycloudy") {
      // Alternating golden sun and cloud gray
      for (int i = 0; i < LED_COUNT; i++) {
        if (i % 2 == 0) {
          // Golden sun - saturated warm color
          uint8_t r = (255 * mqtt_brightness) / 255;  // Full red
          uint8_t g = (160 * mqtt_brightness) / 255;  // Orange-gold
          uint8_t b = (0 * mqtt_brightness) / 255;    // No blue
          leds[i] = CRGB(r, g, b);
        } else {
          // Cloudy gray - halved for white brightness
          uint8_t gray = (70 * mqtt_brightness) / 255;
          leds[i] = CRGB((uint8_t)((gray * 85) / 100), gray, (uint8_t)((gray * 115) / 100));
        }
      }
      FastLED.show();

    } else if (weather_state == "fog") {
      // Pale white/gray with very slow breathing
      // White is ~2x brighter, halve it
      uint8_t breath = sin8(loop_counter / 4) / 3 + 150;
      uint8_t val = (breath * mqtt_brightness) / (255 * 2);
      writeLED({ val, val, (uint8_t)((val * 95) / 100) });

    } else if (weather_state == "rainy") {
      // Blue raindrops falling down (sequential LED lighting)
      // Blue boosted for perceived brightness
      uint8_t drop_pos = (loop_counter / 4) % LED_COUNT;
      for (int i = 0; i < LED_COUNT; i++) {
        uint8_t intensity = (i == drop_pos) ? 255 : 60;
        uint8_t r = (40 * mqtt_brightness * intensity) / (255 * 255);
        uint8_t g = (100 * mqtt_brightness * intensity) / (255 * 255);
        uint8_t b = (255 * mqtt_brightness * intensity) / (255 * 255);  // Blue boosted
        leds[i] = CRGB(r, g, b);
      }
      FastLED.show();

    } else if (weather_state == "pouring") {
      // Intense blue, fast raindrops
      // Blue boosted for perceived brightness
      uint8_t drop_pos = (loop_counter / 2) % LED_COUNT;
      uint8_t drop_pos2 = (loop_counter / 2 + 2) % LED_COUNT;
      for (int i = 0; i < LED_COUNT; i++) {
        uint8_t intensity = (i == drop_pos || i == drop_pos2) ? 255 : 100;
        uint8_t r = (30 * mqtt_brightness * intensity) / (255 * 255);
        uint8_t g = (80 * mqtt_brightness * intensity) / (255 * 255);
        uint8_t b = (255 * mqtt_brightness * intensity) / (255 * 255);  // Blue boosted
        leds[i] = CRGB(r, g, b);
      }
      FastLED.show();

    } else if (weather_state == "lightning") {
      // Dark gray base with random white flashes
      // Flash intentionally very bright, base gray reduced
      bool flash = (random(100) < 5);
      if (flash) {
        // Lightning flash - full brightness intentionally!
        uint8_t val = mqtt_brightness;
        writeLED({ val, val, val });
      } else {
        // Dark base - gray halved
        uint8_t gray = (25 * mqtt_brightness) / 255;
        writeLED({ gray, gray, (uint8_t)((gray * 130) / 100) });
      }

    } else if (weather_state == "lightning-rainy") {
      // Rain animation with occasional lightning flashes
      // Flash intentionally very bright, blue boosted
      bool flash = (random(100) < 3);
      if (flash) {
        // Lightning flash - full brightness intentionally!
        uint8_t val = mqtt_brightness;
        writeLED({ val, val, val });
      } else {
        uint8_t drop_pos = (loop_counter / 3) % LED_COUNT;
        for (int i = 0; i < LED_COUNT; i++) {
          uint8_t intensity = (i == drop_pos) ? 255 : 80;
          uint8_t r = (35 * mqtt_brightness * intensity) / (255 * 255);
          uint8_t g = (90 * mqtt_brightness * intensity) / (255 * 255);
          uint8_t b = (255 * mqtt_brightness * intensity) / (255 * 255);  // Blue boosted
          leds[i] = CRGB(r, g, b);
        }
        FastLED.show();
      }

    } else if (weather_state == "windy" || weather_state == "windy-variant") {
      // Green-yellow leaves blowing in the wind - sweeping pattern
      uint8_t pos = (sin8(loop_counter * 3) * (LED_COUNT - 1)) / 255;
      for (int i = 0; i < LED_COUNT; i++) {
        uint8_t dist = abs((int)pos - i);
        uint8_t intensity = 255 - (dist * 50);
        if (intensity > 255) intensity = 60;
        // Alternate between green and yellow-green for leaf effect
        bool is_yellow = ((loop_counter / 8) + i) % 3 == 0;
        uint8_t r, g, b;
        if (is_yellow) {
          // Yellow-green leaf
          r = (180 * mqtt_brightness * intensity) / (255 * 255);
          g = (220 * mqtt_brightness * intensity) / (255 * 255);
          b = (30 * mqtt_brightness * intensity) / (255 * 255);
        } else {
          // Green leaf
          r = (60 * mqtt_brightness * intensity) / (255 * 255);
          g = (200 * mqtt_brightness * intensity) / (255 * 255);
          b = (40 * mqtt_brightness * intensity) / (255 * 255);
        }
        leds[i] = CRGB(r, g, b);
      }
      FastLED.show();

    } else if (weather_state == "snowy") {
      // White with random sparkles
      // White is ~2x brighter, halve values
      for (int i = 0; i < LED_COUNT; i++) {
        uint8_t sparkle = (random(100) < 10) ? 110 : 80;
        uint8_t val = (sparkle * mqtt_brightness) / 255;
        leds[i] = CRGB(val, val, val);
      }
      FastLED.show();

    } else if (weather_state == "snowy-rainy") {
      // Alternating white and blue drops
      // White ~2x brighter (halved), blue boosted
      uint8_t drop_pos = (loop_counter / 3) % LED_COUNT;
      for (int i = 0; i < LED_COUNT; i++) {
        bool is_snow = ((loop_counter / 10) + i) % 2 == 0;
        uint8_t intensity = (i == drop_pos) ? 255 : 100;
        if (is_snow) {
          // White snow: halved for brightness match
          uint8_t val = (intensity * mqtt_brightness) / (255 * 2);
          leds[i] = CRGB(val, val, val);
        } else {
          // Blue rain: boosted
          uint8_t r = (30 * mqtt_brightness * intensity) / (255 * 255);
          uint8_t g = (70 * mqtt_brightness * intensity) / (255 * 255);
          uint8_t b = (220 * mqtt_brightness * intensity) / (255 * 255);
          leds[i] = CRGB(r, g, b);
        }
      }
      FastLED.show();

    } else if (weather_state == "hail") {
      // White with harsh random flicker
      // White is ~2x brighter, halve values
      for (int i = 0; i < LED_COUNT; i++) {
        uint8_t flicker = (random(100) < 30) ? 100 : (random(100) < 50 ? 60 : 20);
        uint8_t val = (flicker * mqtt_brightness) / 255;
        leds[i] = CRGB(val, val, val);
      }
      FastLED.show();

    } else if (weather_state == "exceptional") {
      // Rainbow multi effect for exceptional weather
      // HSV handles brightness internally, reduce for balance
      uint8_t base_hue = rgb_hue >> 8;
      rgb_hue += 20;
      uint8_t balanced_brightness = (mqtt_brightness * 180) / 255;
      for (int i = 0; i < LED_COUNT; i++) {
        uint8_t hue = base_hue + (i * 255 / LED_COUNT);
        CHSV hsv(hue, 255, balanced_brightness);
        CRGB rgb;
        hsv2rgb_rainbow(hsv, rgb);
        leds[i] = rgb;
      }
      FastLED.show();

    } else {
      // Default/unknown: warm white
      // Warm white (R+G+small B) is ~1.8x brighter, reduce
      uint8_t val = (80 * mqtt_brightness) / 255;
      writeLED({ val, (uint8_t)((val * 85) / 100), (uint8_t)((val * 60) / 100) });
    }
  } else if (circadian_enabled) {
    // Circadian mode: Colors based on time of day (via NTP)
    // Use preview hour if set, otherwise use real time
    int preview_hour = mqtt->getCircadianPreviewHour();
    uint8_t hour;
    if (preview_hour >= 0) {
      hour = (uint8_t)preview_hour;
    } else {
      struct tm timeinfo;
      hour = 12; // Default fallback
      if (getLocalTime(&timeinfo)) {
        hour = timeinfo.tm_hour;
      }
    }

    if (hour >= 22 || hour < 6) {
      // Night (22:00 - 06:00): Starry night - same as clear-night weather
      for (int i = 0; i < LED_COUNT; i++) {
        bool is_star = ((loop_counter + i * 50) % 120 < 8) || (random(100) < 2);
        if (is_star) {
          // Twinkling star: warm white, reduced (same as clear-night)
          uint8_t val = (80 * mqtt_brightness) / 255;
          leds[i] = CRGB(val, (uint8_t)((val * 95) / 100), (uint8_t)((val * 80) / 100));
        } else {
          // Deep blue night sky: boosted for perceived brightness (same as clear-night)
          uint8_t b = (200 * mqtt_brightness) / 255;
          uint8_t r = (15 * mqtt_brightness) / 255;
          uint8_t g = (30 * mqtt_brightness) / 255;
          leds[i] = CRGB(r, g, b);
        }
      }
      FastLED.show();
    } else if (hour >= 6 && hour < 8) {
      // Early morning sunrise: orange-pink with slow rotating glow
      uint8_t pos = (uint8_t)((millis() / 80) % LED_COUNT);  // Slow rotation ~0.4s per LED
      for (int i = 0; i < LED_COUNT; i++) {
        uint8_t dist = (i >= pos) ? (i - pos) : (LED_COUNT - pos + i);
        uint8_t intensity = 255 - (dist * 35);  // Subtle gradient
        uint8_t r = (255 * mqtt_brightness * intensity) / (255 * 255);
        uint8_t g = (100 * mqtt_brightness * intensity) / (255 * 255);
        uint8_t b = (60 * mqtt_brightness * intensity) / (255 * 255);
        leds[i] = CRGB(r, g, b);
      }
      FastLED.show();
    } else if (hour >= 8 && hour < 11) {
      // Late morning: warm golden with visible breathing
      uint8_t phase = (uint8_t)((millis() / 15) % 256);  // ~4 second cycle
      uint8_t sine = sin8(phase);
      uint8_t breath = (sine * 5 / 10) + 128;  // 50%-100% range
      uint8_t r = (255 * mqtt_brightness * breath) / (255 * 255);
      uint8_t g = (180 * mqtt_brightness * breath) / (255 * 255);
      uint8_t b = (40 * mqtt_brightness * breath) / (255 * 255);
      writeLED({ r, g, b });
    } else if (hour >= 11 && hour < 16) {
      // Midday: bright gold-white (full sun) - static, no animation
      uint8_t r = (255 * mqtt_brightness) / 255;
      uint8_t g = (220 * mqtt_brightness) / 255;
      uint8_t b = (120 * mqtt_brightness) / 255;
      writeLED({ r, g, b });
    } else if (hour >= 16 && hour < 19) {
      // Afternoon/early evening: golden orange with visible breathing
      uint8_t phase = (uint8_t)((millis() / 15) % 256);  // ~4 second cycle
      uint8_t sine = sin8(phase);
      uint8_t breath = (sine * 5 / 10) + 128;  // 50%-100% range
      uint8_t r = (255 * mqtt_brightness * breath) / (255 * 255);
      uint8_t g = (150 * mqtt_brightness * breath) / (255 * 255);
      uint8_t b = (30 * mqtt_brightness * breath) / (255 * 255);
      writeLED({ r, g, b });
    } else {
      // Late evening sunset (19:00 - 22:00): deep red-orange with slow rotating glow
      uint8_t pos = (uint8_t)((millis() / 100) % LED_COUNT);  // Slower rotation ~0.5s per LED
      for (int i = 0; i < LED_COUNT; i++) {
        uint8_t dist = (i >= pos) ? (i - pos) : (LED_COUNT - pos + i);
        uint8_t intensity = 255 - (dist * 35);  // Subtle gradient
        uint8_t r = (255 * mqtt_brightness * intensity) / (255 * 255);
        uint8_t g = (60 * mqtt_brightness * intensity) / (255 * 255);
        uint8_t b = (20 * mqtt_brightness * intensity) / (255 * 255);
        leds[i] = CRGB(r, g, b);
      }
      FastLED.show();
    }
  } else if (rainbow_multi_enabled) {
    // Rainbow Multi: Each LED has a different color, rotating together
    // HSV brightness reduced for balance with other effects
    rgb_hue += 20;
    uint8_t base_hue = rgb_hue >> 8;

    // Scale brightness by MQTT brightness setting - reduced for balance
    uint8_t base_brightness = (mqtt_brightness * 150) / 255;
    uint8_t pulse_range = (mqtt_brightness * 50) / 255;
    uint8_t led_brightness = base_brightness + ((sin8(loop_counter) * pulse_range) / 255);

    // Each LED gets a different hue offset (evenly distributed across spectrum)
    for (int i = 0; i < LED_COUNT; i++) {
      uint8_t hue = base_hue + (i * 255 / LED_COUNT);
      CHSV hsv(hue, 255, led_brightness);
      CRGB rgb;
      hsv2rgb_rainbow(hsv, rgb);
      leds[i] = rgb;
    }
    FastLED.show();
  } else if (rainbow_enabled) {
    // Rainbow: All LEDs same color, rotating through spectrum
    // HSV brightness reduced for balance with other effects
    rgb_hue += 20;
    uint8_t hue8 = rgb_hue >> 8;

    // Scale base brightness by MQTT brightness setting - reduced for balance
    uint8_t base_brightness = (mqtt_brightness * 150) / 255;
    uint8_t pulse_range = (mqtt_brightness * 50) / 255;
    uint8_t led_brightness = base_brightness + ((sin8(loop_counter) * pulse_range) / 255);

    CHSV hsv(hue8, 255, led_brightness);
    CRGB rgb;
    hsv2rgb_rainbow(hsv, rgb);
    // Don't overwrite configuration.color - keep user's static color intact
    writeLED({ rgb.r, rgb.g, rgb.b });
  } else {
    // Static color from configuration, scaled by MQTT brightness
    Color scaled_color;
    scaled_color.red = (configuration.color.red * mqtt_brightness) / 255;
    scaled_color.green = (configuration.color.green * mqtt_brightness) / 255;
    scaled_color.blue = (configuration.color.blue * mqtt_brightness) / 255;
    writeLED(scaled_color);
  }
}

void HardwareService::readSensors() {

  // update sensor connection status

  if (light_sensor.is_connected() != sensor_data.has_light_sensor) {
    light_sensor = RPR0521RS();
    sensor_data.has_light_sensor = light_sensor.init() == 0;
    Serial.println(PRINT_PREFIX + "Reconnected light sensor? " + (sensor_data.has_light_sensor ? "Success." : "Failed."));
  }

  sensor_data.has_touch_sensor = touch_sensor.isConnected();

  if (!sensor_data.has_touch_sensor) {
    sensor_data.has_touch_sensor = touch_sensor.begin();
    if (sensor_data.has_touch_sensor) {
      Serial.println(PRINT_PREFIX + "Reconnected touch sensor.");
    }
  }

  // update sensor data

  if (sensor_data.has_light_sensor) {
    uint32_t distance;
    float brightness;

    uint8_t rc = light_sensor.get_psalsval(&distance, &brightness);
    if ((distance >= 0) && (brightness >= 0) && (brightness <= MAX_BRIGHTNESS) && (distance <= MAX_DISTANCE) && (rc == 0)) {
      brightness = brightness / MAX_BRIGHTNESS;
      if (light_measurement_count < MAX_MEASUREMENT_COUNT) {
        light_measurement_count++;
        if (light_measurement_count == 1) {
          ambient_brightness = brightness;
        } else {
          ambient_brightness = (0.9 * ambient_brightness) + (0.1 * brightness);
        }
        if (light_measurement_count == MAX_MEASUREMENT_COUNT) {
          configuration.lower_brightness_threshold = max(0.0f, ambient_brightness - DEFAULT_BRIGHTNESS_THRESHOLD_DISTANCE);
          configuration.upper_brightness_threshold = min(1.0f, ambient_brightness + DEFAULT_BRIGHTNESS_THRESHOLD_DISTANCE);
        }
      }

      sensor_data.brightness = brightness;
      sensor_data.distance = 1 - (distance / MAX_DISTANCE);
    }
  }

  if (sensor_data.has_touch_sensor) {
    // They are intentionally flipped, since the existing code recognizes them as the opposite
    sensor_data.touch_left = touch_sensor.isRightTouched();
    sensor_data.touch_right = touch_sensor.isLeftTouched();
  } else {
    sensor_data.touch_left = false;
    sensor_data.touch_right = false;
  }

  // print sensor data

#if DEBUG_AUTONOMOUS_MODE
  Serial.print(PRINT_PREFIX + "Sensor Data ");

  if (sensor_data.has_light_sensor) {
    Serial.print("(brightness: " + String(sensor_data.brightness));
    Serial.print(", ambient: " + String(ambient_brightness));
    Serial.print(", distance: " + String(sensor_data.distance));
    Serial.print(") ");
  }

  if (sensor_data.has_touch_sensor) {
    Serial.print("(touch_left: " + String(sensor_data.touch_left));
    Serial.print(", touch_right: " + String(sensor_data.touch_right));
    Serial.print(") ");
  }

  Serial.println();
  Serial.flush();
#endif
}

void HardwareService::updateMotor() {
  if (!motor_calibration_finished) return;

  // Only run sensor-based motor control if Sensor effect is active
  MQTTService* mqtt = MQTTService::getSharedInstance();
  if (!mqtt->isSensorEnabled()) return;

  configuration.motor_position = 1 - ((float)(motor.getMotorPosition()) / (float)(32 * MOTOR_FULL_STEP_COUNT));
  Serial.println(PRINT_PREFIX + "Sensor effect: Updating motor..." + String(light_measurement_count));

  if ((sensor_data.has_light_sensor) && (light_measurement_count >= MAX_MEASUREMENT_COUNT)) {
    #if ENABLE_DISTANCE
    if (configuration.distance_threshold > sensor_data.distance) {
      Serial.println(PRINT_PREFIX + "Close due to: Too close");
      move(MOTOR_POSITION_CLOSED, MOTOR_SPEED_FAST);
      reopen_cycle_count = MAX_REOPENCYCLES_DISTANCE;
      return;
    }
    #endif

    // Close when brightness <= lower threshold (1%)
    if (sensor_data.brightness <= configuration.lower_brightness_threshold) {
      Serial.println(PRINT_PREFIX + "Close due to: Too dark (" + String(sensor_data.brightness * 100) + "%)");
      move(MOTOR_POSITION_CLOSED, MOTOR_SPEED_FAST);
      reopen_cycle_count = MAX_REOPENCYCLES_LIGHT;
      return;
    }
    // Open when brightness >= upper threshold (3%)
    else if (sensor_data.brightness >= configuration.upper_brightness_threshold) {
      if (reopen_cycle_count <= 0) {
        Serial.println(PRINT_PREFIX + "Open due to: Bright enough (" + String(sensor_data.brightness * 100) + "%)");
        move(MOTOR_POSITION_OPEN, MOTOR_SPEED_FAST);
        reopen_cycle_count = 0;
        return;
      } else {
        Serial.println(PRINT_PREFIX + "Could open due to: Bright enough");
        reopen_cycle_count--;
      }
    }
  }

  motor.stop();
}

// position: 1.0f (open), 0.0f (close)
// speed: Use MOTOR_SPEED_SLOW or MOTOR_SPEED_FAST
void HardwareService::move(float position, float speed) {
  Serial.println(PRINT_PREFIX + "move(" + String(position) + ", " + String(speed * 100) + ")");
  if (abs(intended_motor_position - position) < 0.005) {
    motor.rotate(speed);
    return;
  }

  intended_motor_position = position;

  if (motor.isOpening() || motor.isClosing()) {
    motor.stop();
  }

  int32_t steps = position * MOTOR_FULL_STEP_COUNT - (((float)motor.getMotorPosition()) / 32);
  if (abs(steps) < 1) return;

  Serial.println(PRINT_PREFIX + "Current Motor Position: " + String(motor.getMotorPosition() / 32));
  Serial.println(PRINT_PREFIX + "Position: " + String(position));
  Serial.println(PRINT_PREFIX + "Intended Motor Position: " + String(position * MOTOR_FULL_STEP_COUNT));
  Serial.println(PRINT_PREFIX + "Move to " + String(position) + " with speed " + String(speed) + ", steps: " + steps);

  motor.setMotorCurrent(MOTOR_CURRENT_LOW);
  motor.setDirection(steps > 0 ? MotorLogic::OPEN : MotorLogic::CLOSE);
  motor.setNSteps(abs(steps));
  motor.setSteppingMode(MotorLogic::M1);
  motor.wakeup();
  motor.rotate(speed);
}

void HardwareService::updateAdaptiveBrightness() {
  MQTTService* mqtt = MQTTService::getSharedInstance();
  if (!mqtt->isAdaptiveBrightnessEnabled()) {
    adaptive_brightness_factor = 255;
    return;
  }

  // Only update every 15 seconds
  unsigned long now = millis();
  if (now - last_adaptive_brightness_update < 15000) {
    return;
  }
  last_adaptive_brightness_update = now;

  if (!sensor_data.has_light_sensor) {
    adaptive_brightness_factor = 255;
    return;
  }

  // Adaptive brightness scaling:
  // 1% ambient = 5% LED (13/255)
  // 9% ambient = 100% LED (255/255)
  // Linear interpolation between these points
  float brightness_percent = sensor_data.brightness * 100.0f;  // 0-10% range typically

  if (brightness_percent >= 9.0f) {
    adaptive_brightness_factor = 255;
  } else if (brightness_percent <= 1.0f) {
    adaptive_brightness_factor = 13;  // 5% of 255
  } else {
    // Linear interpolation: 1% -> 13, 9% -> 255
    // slope = (255 - 13) / (9 - 1) = 242 / 8 = 30.25
    adaptive_brightness_factor = (uint8_t)(13 + (brightness_percent - 1.0f) * 30.25f);
  }

  Serial.println(PRINT_PREFIX + "Adaptive brightness: ambient=" + String(brightness_percent, 1) +
                 "% -> LED factor=" + String(adaptive_brightness_factor * 100 / 255) + "%");
}

void HardwareService::saveStateToNVS() {
  // Debounce: only mark as pending, actual save happens in loop()
  nvs_save_pending = true;
  nvs_save_requested_at = millis();
}

void HardwareService::checkPendingNVSSave() {
  // Save to NVS 3 seconds after last change request (debouncing)
  if (!nvs_save_pending) return;

  unsigned long now = millis();
  if (now - nvs_save_requested_at < 3000) return;

  nvs_save_pending = false;

  Preferences prefs;
  prefs.begin("bionic", false);

  // Save configuration
  prefs.putFloat("motor_pos", configuration.motor_position);
  prefs.putFloat("speed", configuration.speed);
  prefs.putFloat("lower_bright", configuration.lower_brightness_threshold);
  prefs.putFloat("upper_bright", configuration.upper_brightness_threshold);
  prefs.putFloat("dist_thresh", configuration.distance_threshold);
  prefs.putBool("is_auto", configuration.is_autonomous);
  prefs.putUChar("color_r", configuration.color.red);
  prefs.putUChar("color_g", configuration.color.green);
  prefs.putUChar("color_b", configuration.color.blue);

  // Save MQTT state
  MQTTService* mqtt = MQTTService::getSharedInstance();
  prefs.putBool("light_on", mqtt->isLightOn());
  prefs.putUChar("brightness", mqtt->getBrightness());
  prefs.putBool("rainbow", mqtt->isRainbowEnabled());
  prefs.putBool("rainbow_m", mqtt->isRainbowMultiEnabled());
  prefs.putBool("circadian", mqtt->isCircadianEnabled());
  prefs.putBool("weather", mqtt->isWeatherEnabled());
  prefs.putBool("sensor", mqtt->isSensorEnabled());
  prefs.putBool("adapt_br", mqtt->isAdaptiveBrightnessEnabled());

  prefs.end();
  Serial.println(PRINT_PREFIX + "State saved to NVS");
}

void HardwareService::loadStateFromNVS() {
  Preferences prefs;
  prefs.begin("bionic", true);  // read-only

  // Check if data exists
  if (!prefs.isKey("color_r")) {
    prefs.end();
    Serial.println(PRINT_PREFIX + "No saved state in NVS");
    return;
  }

  // Load configuration
  configuration.motor_position = prefs.getFloat("motor_pos", MOTOR_POSITION_CLOSED);
  configuration.speed = prefs.getFloat("speed", 0.5f);
  configuration.lower_brightness_threshold = prefs.getFloat("lower_bright", DEFAULT_LOWER_BRIGHTNESS_THRESHOLD);
  configuration.upper_brightness_threshold = prefs.getFloat("upper_bright", DEFAULT_UPPER_BRIGHTNESS_THRESHOLD);
  configuration.distance_threshold = prefs.getFloat("dist_thresh", DEFAULT_DISTANCE_THRESHOLD);
  configuration.is_autonomous = prefs.getBool("is_auto", false);
  configuration.color.red = prefs.getUChar("color_r", 0);
  configuration.color.green = prefs.getUChar("color_g", 145);
  configuration.color.blue = prefs.getUChar("color_b", 220);

  // Load MQTT state
  MQTTService* mqtt = MQTTService::getSharedInstance();
  mqtt->setLightOn(prefs.getBool("light_on", true));
  mqtt->setBrightness(prefs.getUChar("brightness", 255));
  mqtt->setRainbowEnabled(prefs.getBool("rainbow", false));
  mqtt->setRainbowMultiEnabled(prefs.getBool("rainbow_m", false));
  mqtt->setCircadianEnabled(prefs.getBool("circadian", false));
  mqtt->setWeatherEnabled(prefs.getBool("weather", false));
  mqtt->setSensorEnabled(prefs.getBool("sensor", false));
  mqtt->setAdaptiveBrightnessEnabled(prefs.getBool("adapt_br", true));

  prefs.end();

  Serial.println(PRINT_PREFIX + "State loaded from NVS:");
  Serial.println(PRINT_PREFIX + "  Color: R=" + String(configuration.color.red) +
                 " G=" + String(configuration.color.green) +
                 " B=" + String(configuration.color.blue));
  Serial.println(PRINT_PREFIX + "  Brightness: " + String(mqtt->getBrightness()));
  Serial.println(PRINT_PREFIX + "  Light on: " + String(mqtt->isLightOn()));
}
