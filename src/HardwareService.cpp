
// MARK: Includes

#include "HardwareService.h"
#include "MQTTService.h"

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

  writeLED({ 0, 255, 0 });
  delay(500);
  writeLED({ 255, 0, 0 });
  delay(500);
  writeLED({ 0, 0, 255 });
  delay(500);
  writeLED(configuration.color);

  Serial.println(PRINT_PREFIX + "Calibrating motor...");

  intended_motor_position = MOTOR_POSITION_OPEN;
  move(MOTOR_POSITION_CLOSED, MOTOR_SPEED_FAST);
  delay((1000 * MOTOR_FULL_STEP_COUNT * MOTOR_SPEED_FAST) + 20);

  while (!motor.isCalibrated()) {
    Serial.println(PRINT_PREFIX + "Still not calibrated.");
    delay(100);
  }

  motor_calibration_finished = true;

  Serial.println(PRINT_PREFIX + "Motor calibration done.");
  return true;
}

void HardwareService::setConfiguration(Configuration new_configuration) {
  Serial.print(PRINT_PREFIX + "New Configuration");
  Serial.print(": Motor Position: " + String(new_configuration.motor_position));
  Serial.print(", Lower Brightness Threshold: " + String(new_configuration.lower_brightness_threshold));
  Serial.print(", Upper Brightness Threshold: " + String(new_configuration.upper_brightness_threshold));
  Serial.print(", Distance Threshold: " + String(new_configuration.distance_threshold));
  Serial.print(", Autonomous: " + String(new_configuration.is_autonomous));
  Serial.print(", Red: " + String(new_configuration.color.red));
  Serial.print(", Green: " + String(new_configuration.color.green));
  Serial.print(", Blue: " + String(new_configuration.color.blue));
  Serial.println();

  boolean control_changed = false;

  if (new_configuration.is_autonomous != configuration.is_autonomous) {
    control_changed = true;
    if (!new_configuration.is_autonomous) {
      motor.stop();
    }
  }

  if ((new_configuration.color.red != configuration.color.red)
      || (new_configuration.color.blue != configuration.color.blue)
      || (new_configuration.color.green != configuration.color.green)) {
    control_changed = true;
  }

  writeLED(new_configuration.color);

  if (((new_configuration.motor_position != configuration.motor_position) || (new_configuration.speed != configuration.speed))
      && (!new_configuration.is_autonomous)
      && (!control_changed)) {

    // The correct way to calculate the speed would be:
    // MOTOR_SPEED_FAST + ((1 - new_configuration.speed) * (MOTOR_SPEED_SLOW - MOTOR_SPEED_FAST));
    // but since Ticker is limited to a time granularity of 0.001, we cannot set any value between 0.001 and 0.002
    float motor_speed = (new_configuration.speed < 0.5) ? MOTOR_SPEED_SLOW : MOTOR_SPEED_FAST;
    move(new_configuration.motor_position, motor_speed);
  }

  this->configuration = new_configuration;
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

  // Check MQTT light state
  MQTTService* mqtt = MQTTService::getSharedInstance();
  bool light_on = mqtt->isLightOn();
  bool rainbow_enabled = mqtt->isRainbowEnabled();
  bool rainbow_multi_enabled = mqtt->isRainbowMultiEnabled();
  bool circadian_enabled = mqtt->isCircadianEnabled();
  bool weather_enabled = mqtt->isWeatherEnabled();
  uint8_t mqtt_brightness = mqtt->getBrightness();

  // If light is turned off via MQTT, turn off LEDs
  if (!light_on) {
    writeLED({ 0, 0, 0 });
  }
  // Turn off LEDs when light sensor detects darkness (only in autonomous mode)
  else if (configuration.is_autonomous && sensor_data.has_light_sensor && sensor_data.brightness < 0.02f) {
    writeLED({ 0, 0, 0 });
  } else if (weather_enabled) {
    // Weather mode: Individual animations for each weather state
    String weather_state = mqtt->getWeatherState();

    // Determine target motor position based on weather
    float target_position = MOTOR_POSITION_OPEN; // Default: open (sunny)

    if (weather_state == "rainy" || weather_state == "pouring" ||
        weather_state == "lightning" || weather_state == "lightning-rainy" ||
        weather_state == "hail" || weather_state == "snowy" || weather_state == "snowy-rainy") {
      target_position = MOTOR_POSITION_CLOSED;
    } else if (weather_state == "partlycloudy") {
      target_position = 0.25f; // 75% open
    } else if (weather_state == "cloudy" || weather_state == "fog" ||
               weather_state == "windy" || weather_state == "windy-variant") {
      target_position = 0.5f;
    }

    // Move motor to target position if different
    if (abs(configuration.motor_position - target_position) > 0.01f) {
      configuration.motor_position = target_position;
    }

    // Individual weather animations
    if (weather_state == "sunny") {
      // Warm yellow/gold with gentle breathing
      uint8_t breath = sin8(loop_counter) / 4 + 180;
      uint8_t r = (255 * mqtt_brightness * breath) / (255 * 255);
      uint8_t g = (220 * mqtt_brightness * breath) / (255 * 255);
      uint8_t b = (100 * mqtt_brightness * breath) / (255 * 255);
      writeLED({ r, g, b });

    } else if (weather_state == "clear-night") {
      // Dark blue base with twinkling stars
      for (int i = 0; i < LED_COUNT; i++) {
        uint8_t twinkle = ((loop_counter + i * 50) % 100 < 10) ? 255 : 0;
        if (random(100) < 3) twinkle = 255; // Random sparkle
        uint8_t r = (twinkle * mqtt_brightness) / 255;
        uint8_t g = (twinkle * mqtt_brightness) / 255;
        uint8_t b = ((40 + twinkle / 3) * mqtt_brightness) / 255;
        leds[i] = CRGB(r, g, b);
      }
      FastLED.show();

    } else if (weather_state == "cloudy") {
      // Gray colors slowly drifting across LEDs
      uint8_t wave_pos = (loop_counter / 3) % (LED_COUNT * 2);
      for (int i = 0; i < LED_COUNT; i++) {
        uint8_t dist = abs((int)wave_pos - i - LED_COUNT);
        uint8_t brightness_mod = 255 - (dist * 30);
        if (brightness_mod > 255) brightness_mod = 100;
        uint8_t gray = (brightness_mod * mqtt_brightness) / 255;
        leds[i] = CRGB(gray, gray, (gray * 110) / 100);
      }
      FastLED.show();

    } else if (weather_state == "partlycloudy") {
      // Alternating sunny yellow and cloud gray
      for (int i = 0; i < LED_COUNT; i++) {
        if (i % 2 == 0) {
          // Sunny
          uint8_t r = (255 * mqtt_brightness) / 255;
          uint8_t g = (200 * mqtt_brightness) / 255;
          uint8_t b = (80 * mqtt_brightness) / 255;
          leds[i] = CRGB(r, g, b);
        } else {
          // Cloudy gray
          uint8_t gray = (150 * mqtt_brightness) / 255;
          leds[i] = CRGB(gray, gray, gray);
        }
      }
      FastLED.show();

    } else if (weather_state == "fog") {
      // Pale white/gray with very slow breathing
      uint8_t breath = sin8(loop_counter / 4) / 3 + 150;
      uint8_t val = (breath * mqtt_brightness) / 255;
      writeLED({ val, val, (val * 95) / 100 });

    } else if (weather_state == "rainy") {
      // Blue raindrops falling down (sequential LED lighting)
      uint8_t drop_pos = (loop_counter / 4) % LED_COUNT;
      for (int i = 0; i < LED_COUNT; i++) {
        uint8_t intensity = (i == drop_pos) ? 255 : 50;
        uint8_t r = (30 * mqtt_brightness * intensity) / (255 * 255);
        uint8_t g = (80 * mqtt_brightness * intensity) / (255 * 255);
        uint8_t b = (200 * mqtt_brightness * intensity) / (255 * 255);
        leds[i] = CRGB(r, g, b);
      }
      FastLED.show();

    } else if (weather_state == "pouring") {
      // Intense blue, fast raindrops
      uint8_t drop_pos = (loop_counter / 2) % LED_COUNT;
      uint8_t drop_pos2 = (loop_counter / 2 + 2) % LED_COUNT;
      for (int i = 0; i < LED_COUNT; i++) {
        uint8_t intensity = (i == drop_pos || i == drop_pos2) ? 255 : 80;
        uint8_t r = (20 * mqtt_brightness * intensity) / (255 * 255);
        uint8_t g = (60 * mqtt_brightness * intensity) / (255 * 255);
        uint8_t b = (255 * mqtt_brightness * intensity) / (255 * 255);
        leds[i] = CRGB(r, g, b);
      }
      FastLED.show();

    } else if (weather_state == "lightning") {
      // Dark gray base with random white flashes
      bool flash = (random(100) < 5);
      if (flash) {
        uint8_t val = (255 * mqtt_brightness) / 255;
        writeLED({ val, val, val });
      } else {
        uint8_t gray = (40 * mqtt_brightness) / 255;
        writeLED({ gray, gray, (gray * 120) / 100 });
      }

    } else if (weather_state == "lightning-rainy") {
      // Rain animation with occasional lightning flashes
      bool flash = (random(100) < 3);
      if (flash) {
        uint8_t val = (255 * mqtt_brightness) / 255;
        writeLED({ val, val, val });
      } else {
        uint8_t drop_pos = (loop_counter / 3) % LED_COUNT;
        for (int i = 0; i < LED_COUNT; i++) {
          uint8_t intensity = (i == drop_pos) ? 255 : 60;
          uint8_t r = (25 * mqtt_brightness * intensity) / (255 * 255);
          uint8_t g = (70 * mqtt_brightness * intensity) / (255 * 255);
          uint8_t b = (220 * mqtt_brightness * intensity) / (255 * 255);
          leds[i] = CRGB(r, g, b);
        }
        FastLED.show();
      }

    } else if (weather_state == "windy" || weather_state == "windy-variant") {
      // Cyan/turquoise quickly sweeping back and forth
      uint8_t pos = (sin8(loop_counter * 2) * (LED_COUNT - 1)) / 255;
      for (int i = 0; i < LED_COUNT; i++) {
        uint8_t dist = abs((int)pos - i);
        uint8_t intensity = 255 - (dist * 60);
        if (intensity > 255) intensity = 50;
        uint8_t r = (50 * mqtt_brightness * intensity) / (255 * 255);
        uint8_t g = (200 * mqtt_brightness * intensity) / (255 * 255);
        uint8_t b = (180 * mqtt_brightness * intensity) / (255 * 255);
        leds[i] = CRGB(r, g, b);
      }
      FastLED.show();

    } else if (weather_state == "snowy") {
      // White with random sparkles
      for (int i = 0; i < LED_COUNT; i++) {
        uint8_t sparkle = (random(100) < 10) ? 255 : 180;
        uint8_t val = (sparkle * mqtt_brightness) / 255;
        leds[i] = CRGB(val, val, val);
      }
      FastLED.show();

    } else if (weather_state == "snowy-rainy") {
      // Alternating white and blue drops
      uint8_t drop_pos = (loop_counter / 3) % LED_COUNT;
      for (int i = 0; i < LED_COUNT; i++) {
        bool is_snow = ((loop_counter / 10) + i) % 2 == 0;
        uint8_t intensity = (i == drop_pos) ? 255 : 80;
        if (is_snow) {
          uint8_t val = (intensity * mqtt_brightness) / 255;
          leds[i] = CRGB(val, val, val);
        } else {
          uint8_t r = (30 * mqtt_brightness * intensity) / (255 * 255);
          uint8_t g = (80 * mqtt_brightness * intensity) / (255 * 255);
          uint8_t b = (200 * mqtt_brightness * intensity) / (255 * 255);
          leds[i] = CRGB(r, g, b);
        }
      }
      FastLED.show();

    } else if (weather_state == "hail") {
      // White with harsh random flicker
      for (int i = 0; i < LED_COUNT; i++) {
        uint8_t flicker = (random(100) < 30) ? 255 : (random(100) < 50 ? 150 : 50);
        uint8_t val = (flicker * mqtt_brightness) / 255;
        leds[i] = CRGB(val, val, val);
      }
      FastLED.show();

    } else if (weather_state == "exceptional") {
      // Rainbow multi effect for exceptional weather
      uint8_t base_hue = rgb_hue >> 8;
      rgb_hue += 20;
      for (int i = 0; i < LED_COUNT; i++) {
        uint8_t hue = base_hue + (i * 255 / LED_COUNT);
        CHSV hsv(hue, 255, mqtt_brightness);
        CRGB rgb;
        hsv2rgb_rainbow(hsv, rgb);
        leds[i] = rgb;
      }
      FastLED.show();

    } else {
      // Default/unknown: warm white
      uint8_t val = (200 * mqtt_brightness) / 255;
      writeLED({ val, (val * 95) / 100, (val * 85) / 100 });
    }
  } else if (circadian_enabled) {
    // Circadian mode: Color temperature based on time of day
    uint8_t hour = mqtt->getCircadianHour();
    uint8_t r, g, b;

    if (hour >= 6 && hour < 9) {
      // Morning: warm orange/yellow sunrise
      r = 255; g = 180; b = 80;
    } else if (hour >= 9 && hour < 12) {
      // Late morning: bright warm white
      r = 255; g = 240; b = 200;
    } else if (hour >= 12 && hour < 17) {
      // Midday: cool daylight white
      r = 240; g = 250; b = 255;
    } else if (hour >= 17 && hour < 20) {
      // Evening: warm golden
      r = 255; g = 200; b = 100;
    } else if (hour >= 20 && hour < 22) {
      // Late evening: warm amber
      r = 255; g = 150; b = 50;
    } else {
      // Night: dim warm red (sleep friendly)
      r = 180; g = 80; b = 30;
    }

    // Scale by brightness
    r = (r * mqtt_brightness) / 255;
    g = (g * mqtt_brightness) / 255;
    b = (b * mqtt_brightness) / 255;

    writeLED({ r, g, b });
  } else if (rainbow_multi_enabled) {
    // Rainbow Multi: Each LED has a different color, rotating together
    rgb_hue += 20;
    uint8_t base_hue = rgb_hue >> 8;

    // Scale brightness by MQTT brightness setting
    uint8_t base_brightness = (mqtt_brightness * 180) / 255;
    uint8_t pulse_range = (mqtt_brightness * 75) / 255;
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
    rgb_hue += 20;
    uint8_t hue8 = rgb_hue >> 8;

    // Scale base brightness by MQTT brightness setting (0-255)
    // Gentle sine wave pulsing around the set brightness
    uint8_t base_brightness = (mqtt_brightness * 180) / 255;
    uint8_t pulse_range = (mqtt_brightness * 75) / 255;
    uint8_t led_brightness = base_brightness + ((sin8(loop_counter) * pulse_range) / 255);

    CHSV hsv(hue8, 255, led_brightness);
    CRGB rgb;
    hsv2rgb_rainbow(hsv, rgb);
    configuration.color = { rgb.r, rgb.g, rgb.b };
    writeLED(configuration.color);
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
  if (!configuration.is_autonomous) return;

  configuration.motor_position = 1 - ((float)(motor.getMotorPosition()) / (float)(32 * MOTOR_FULL_STEP_COUNT));
  Serial.println(PRINT_PREFIX + "Updating motor..." + String(light_measurement_count));

  if (sensor_data.has_touch_sensor) {
    if (sensor_data.touch_right) {
      Serial.println(PRINT_PREFIX + "Open due to: Touch right");
      move(MOTOR_POSITION_OPEN, MOTOR_SPEED_FAST);
      reopen_cycle_count = MAX_REOPENCYCLES_TOUCH;
      return;
    } else if (sensor_data.touch_left) {
      Serial.println(PRINT_PREFIX + "Close due to: Touch left");
      move(MOTOR_POSITION_CLOSED, MOTOR_SPEED_FAST);
      reopen_cycle_count = MAX_REOPENCYCLES_TOUCH;
      return;
    }
  }

  if ((sensor_data.has_light_sensor) && (light_measurement_count >= MAX_MEASUREMENT_COUNT)) {
    #if ENABLE_DISTANCE
    if (configuration.distance_threshold > sensor_data.distance) {
      Serial.println(PRINT_PREFIX + "Close due to: Too close");
      move(MOTOR_POSITION_CLOSED, MOTOR_SPEED_FAST);
      reopen_cycle_count = MAX_REOPENCYCLES_DISTANCE;
      return;
    }
    #endif

    if (configuration.lower_brightness_threshold > sensor_data.brightness) {
      Serial.println(PRINT_PREFIX + "Close due to: Too dark");
      move(MOTOR_POSITION_CLOSED, MOTOR_SPEED_FAST);
      reopen_cycle_count = MAX_REOPENCYCLES_LIGHT;
      return;
    } else if (configuration.upper_brightness_threshold < sensor_data.brightness) {
      if (reopen_cycle_count <= 0) {
        Serial.println(PRINT_PREFIX + "Open due to: Too bright");
        move(MOTOR_POSITION_OPEN, MOTOR_SPEED_FAST);
        reopen_cycle_count = 0;
        return;
      } else {
        Serial.println(PRINT_PREFIX + "Could open due to: Too bright");
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
