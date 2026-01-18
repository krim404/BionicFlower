
// MARK: Includes

#include "WebService.h"
#include "MQTTService.h"

// MARK: Constants

const String PRINT_PREFIX = "[WEB]: ";
const String TEXT_PLAIN = "text/plain";

const String KEY_MOTOR_POSITION = "motor_position";
const String KEY_DISTANCE_THRESHOLD = "distance_threshold";
const String KEY_UPPER_BRIGHTNESS_THRESHOLD = "upper_brightness_threshold";
const String KEY_LOWER_BRIGHTNESS_THRESHOLD = "lower_brightness_threshold";
const String KEY_COLOR = "color";
const String KEY_IS_AUTONOMOUS = "is_autonomous";
const String KEY_BRIGHTNESS = "brightness";
const String KEY_DISTANCE = "distance";
const String KEY_TOUCH_LEFT = "touch_left";
const String KEY_TOUCH_RIGHT = "touch_right";
const String KEY_HAS_TOUCH = "has_touch";
const String KEY_HAS_LIGHT = "has_light";
const String KEY_SPEED = "speed";
const String KEY_EFFECT = "effect";
const String KEY_LED_BRIGHTNESS = "led_brightness";
const String KEY_ADAPTIVE_BRIGHTNESS = "adaptive_brightness";
const String KEY_WEATHER_DEBUG = "weather_debug";
const String KEY_WEATHER_STATE = "weather_state";

// MARK: Initialization

WebService::WebService() {
  hardware_service = HardwareService::getSharedInstance();
  dns_service = new DNSService();
  wifi_service = new WiFiService();
}

// MARK: Methods

void WebService::start(IPAddress ip, const uint16_t port, std::function<void(bool)> completion) {
  SPIFFS.begin();

  if (!hardware_service->start()) {
    Serial.println(PRINT_PREFIX + "Calibration failed.");
    completion(false);
    return;
  }

  wifi_service->start(ip, [this, ip, port, completion](boolean success) {
    if (!success) {
      Serial.println(PRINT_PREFIX + "WiFi setup failed.");
      completion(false);
      return;
    }

    if (!dns_service->start(ip)) {
      Serial.println(PRINT_PREFIX + "DNS setup failed.");
      completion(false);
      return;
    }

    if (!startWebServer(port)) {
      Serial.println(PRINT_PREFIX + "Web setup failed.");
      completion(false);
      return;
    }

    completion(true);
  });
}

void WebService::loop(uint32_t count) {
  boolean has_active_connection = wifi_service->getActiveConnectionCount() > 0;
  if (has_active_connection) {
    dns_service->processRequest();
  }
  hardware_service->loop(has_active_connection, count);
}

// MARK: Helpers

boolean WebService::startWebServer(const uint16_t port) {

  // Create and start the webserver with the given port
  server = new AsyncWebServer(port);

  // Tells the webserver where the website is stored in the internal file system
  // Important to cache the files, because the web server crashes when refreshing the page on mobile devices when files not cashed
  server->serveStatic("/", SPIFFS, "/").setDefaultFile("index.html").setCacheControl("max-age=6000");

  // Handle if the requested file is not found
  server->onNotFound(std::bind(&WebService::handleNotFound, this, std::placeholders::_1));

  // Bind the different requests to the appropriate callback
  server->on("/generate_204", HTTP_GET, std::bind(&WebService::handleGenerate, this, std::placeholders::_1));
  server->on("/configuration", HTTP_GET, std::bind(&WebService::handleUpdateWeb, this, std::placeholders::_1));
  server->on("/configuration", HTTP_POST, std::bind(&WebService::handleUpdateFromWeb, this, std::placeholders::_1));
  server->on("/calibrate", HTTP_POST, std::bind(&WebService::handleCalibrate, this, std::placeholders::_1));
  server->on("/sensorData", HTTP_GET, std::bind(&WebService::handleReadADC, this, std::placeholders::_1));

  server->begin();

  Serial.println(PRINT_PREFIX + "Async-Web-Server initialized!");

  return true;
}

void WebService::handleGenerate(AsyncWebServerRequest *request) {
  Serial.println(PRINT_PREFIX + "Generate 204 answer: " + request->url());
  request->send(204, TEXT_PLAIN, "No Content");
}

void WebService::handleNotFound(AsyncWebServerRequest *request) {
  Serial.println(PRINT_PREFIX + "Requested file not found: " + request->url());
  request->send(404, TEXT_PLAIN, "Not found");
}

void WebService::handleCalibrate(AsyncWebServerRequest *request) {
  Serial.println(PRINT_PREFIX + "Calibrate");
  hardware_service->resetSensorData();
  handleUpdateWeb(request);
}

void WebService::handleUpdateWeb(AsyncWebServerRequest *request) {
  Configuration configuration = hardware_service->getConfiguration();
  SensorData sensor_data = hardware_service->getSensorData();
  MQTTService* mqtt = MQTTService::getSharedInstance();

  // Determine current effect
  String effect = "none";
  if (mqtt->isRainbowEnabled()) effect = "rainbow";
  else if (mqtt->isRainbowMultiEnabled()) effect = "rainbow_multi";
  else if (mqtt->isCircadianEnabled()) effect = "circadian";
  else if (mqtt->isWeatherEnabled()) effect = "weather";
  else if (mqtt->isSensorEnabled()) effect = "sensor";

  String response =
    KEY_MOTOR_POSITION + "=" + String(configuration.motor_position * 100) + "&" +
    KEY_SPEED + "=" + String(configuration.speed * 100) + "&" +
    KEY_UPPER_BRIGHTNESS_THRESHOLD + "=" + String(configuration.upper_brightness_threshold * 100) + "&" +
    KEY_LOWER_BRIGHTNESS_THRESHOLD + "=" + String(configuration.lower_brightness_threshold * 100) + "&" +
    KEY_DISTANCE_THRESHOLD + "=" + String(configuration.distance_threshold * 100) + "&" +
    KEY_IS_AUTONOMOUS + "=" + String(configuration.is_autonomous ? 1 : 0) + "&" +
    KEY_COLOR + "=" + configuration.color.hexString() + "&" +
    KEY_TOUCH_LEFT + "=" + String(sensor_data.touch_left ? 1 : 0) + "&" +
    KEY_TOUCH_RIGHT + "=" + String(sensor_data.touch_right ? 1 : 0) + "&" +
    KEY_HAS_LIGHT + "=" + String(sensor_data.has_light_sensor ? 1 : 0) + "&" +
    KEY_HAS_TOUCH + "=" + String(sensor_data.has_touch_sensor ? 1 : 0) + "&" +
    KEY_EFFECT + "=" + effect + "&" +
    KEY_LED_BRIGHTNESS + "=" + String(mqtt->getBrightness() * 100 / 255) + "&" +
    KEY_ADAPTIVE_BRIGHTNESS + "=" + String(mqtt->isAdaptiveBrightnessEnabled() ? 1 : 0) + "&" +
    KEY_WEATHER_STATE + "=" + (mqtt->isWeatherEnabled() ? mqtt->getWeatherState() : "none");

  Serial.println(PRINT_PREFIX + "Update web with \"" + response + "\".");
  request->send(200, TEXT_PLAIN, response);
}

void WebService::handleReadADC(AsyncWebServerRequest *request) {
  Serial.println(PRINT_PREFIX + "Read sensor data.");
  Configuration configuration = hardware_service->getConfiguration();
  SensorData sensor_data = hardware_service->getSensorData();
  MQTTService* mqtt = MQTTService::getSharedInstance();

  // Determine current effect
  String effect = "none";
  if (mqtt->isRainbowEnabled()) effect = "rainbow";
  else if (mqtt->isRainbowMultiEnabled()) effect = "rainbow_multi";
  else if (mqtt->isCircadianEnabled()) effect = "circadian";
  else if (mqtt->isWeatherEnabled()) effect = "weather";
  else if (mqtt->isSensorEnabled()) effect = "sensor";

  String response =
    KEY_BRIGHTNESS + "=" + String(sensor_data.brightness * 100) + "&" +
    KEY_DISTANCE + "=" + String(sensor_data.distance * 100) + "&" +
    KEY_MOTOR_POSITION + "=" + String(configuration.motor_position * 100) + "&" +
    KEY_SPEED + "=" + String(configuration.speed * 100) + "&" +
    KEY_UPPER_BRIGHTNESS_THRESHOLD + "=" + String(configuration.upper_brightness_threshold * 100) + "&" +
    KEY_LOWER_BRIGHTNESS_THRESHOLD + "=" + String(configuration.lower_brightness_threshold * 100) + "&" +
    KEY_DISTANCE_THRESHOLD + "=" + String(configuration.distance_threshold * 100) + "&" +
    KEY_IS_AUTONOMOUS + "=" + String(configuration.is_autonomous ? 1 : 0) + "&" +
    KEY_COLOR + "=" + configuration.color.hexString() + "&" +
    KEY_TOUCH_LEFT + "=" + String(sensor_data.touch_left ? 1 : 0) + "&" +
    KEY_TOUCH_RIGHT + "=" + String(sensor_data.touch_right ? 1 : 0) + "&" +
    KEY_HAS_LIGHT + "=" + String(sensor_data.has_light_sensor ? 1 : 0) + "&" +
    KEY_HAS_TOUCH + "=" + String(sensor_data.has_touch_sensor ? 1 : 0) + "&" +
    KEY_EFFECT + "=" + effect + "&" +
    KEY_LED_BRIGHTNESS + "=" + String(mqtt->getBrightness() * 100 / 255) + "&" +
    KEY_ADAPTIVE_BRIGHTNESS + "=" + String(mqtt->isAdaptiveBrightnessEnabled() ? 1 : 0) + "&" +
    KEY_WEATHER_STATE + "=" + (mqtt->isWeatherEnabled() ? mqtt->getWeatherState() : "none");

  request->send(200, TEXT_PLAIN, response);
}

void WebService::handleUpdateFromWeb(AsyncWebServerRequest *request) {
  Serial.println(PRINT_PREFIX + "Update configuration from web.");

  Configuration configuration = hardware_service->getConfiguration();

  if (request->hasArg(KEY_MOTOR_POSITION.c_str())) {
    configuration.motor_position = request->arg(KEY_MOTOR_POSITION.c_str()).toFloat() / 100;
  }

  if (request->hasArg(KEY_UPPER_BRIGHTNESS_THRESHOLD.c_str())) {
    configuration.upper_brightness_threshold = request->arg(KEY_UPPER_BRIGHTNESS_THRESHOLD.c_str()).toFloat() / 100;
  }

  if (request->hasArg(KEY_LOWER_BRIGHTNESS_THRESHOLD.c_str())) {
    configuration.lower_brightness_threshold = request->arg(KEY_LOWER_BRIGHTNESS_THRESHOLD.c_str()).toFloat() / 100;
  }

  if (request->hasArg(KEY_DISTANCE_THRESHOLD.c_str())) {
    configuration.distance_threshold = request->arg(KEY_DISTANCE_THRESHOLD.c_str()).toFloat() / 100;
  }

  if (request->hasArg(KEY_IS_AUTONOMOUS.c_str())) {
    configuration.is_autonomous = request->arg(KEY_IS_AUTONOMOUS.c_str()).toInt() > 0;
  }

  if (request->hasArg(KEY_COLOR.c_str())) {
    configuration.color = Color::fromHexString(request->arg(KEY_COLOR.c_str()));
  }

  if (request->hasArg(KEY_SPEED.c_str())) {
    configuration.speed = request->arg(KEY_SPEED.c_str()).toFloat() / 100;
  }

  hardware_service->setConfiguration(configuration);

  // Sync with MQTT
  MQTTService* mqtt = MQTTService::getSharedInstance();

  // Handle effect change
  if (request->hasArg(KEY_EFFECT.c_str())) {
    String effect = request->arg(KEY_EFFECT.c_str());
    // Disable all effects first
    mqtt->setRainbowEnabled(false);
    mqtt->setRainbowMultiEnabled(false);
    mqtt->setCircadianEnabled(false);
    mqtt->setWeatherEnabled(false);
    mqtt->setSensorEnabled(false);
    // Enable selected effect
    if (effect == "rainbow") mqtt->setRainbowEnabled(true);
    else if (effect == "rainbow_multi") mqtt->setRainbowMultiEnabled(true);
    else if (effect == "circadian") mqtt->setCircadianEnabled(true);
    else if (effect == "weather") mqtt->setWeatherEnabled(true);
    else if (effect == "sensor") mqtt->setSensorEnabled(true);
    mqtt->publishLightState();
  }

  // Handle LED brightness change
  if (request->hasArg(KEY_LED_BRIGHTNESS.c_str())) {
    int brightness_percent = request->arg(KEY_LED_BRIGHTNESS.c_str()).toInt();
    mqtt->setBrightness((uint8_t)(brightness_percent * 255 / 100));
    mqtt->publishLightState();
  }

  // Handle adaptive brightness change
  if (request->hasArg(KEY_ADAPTIVE_BRIGHTNESS.c_str())) {
    mqtt->setAdaptiveBrightnessEnabled(request->arg(KEY_ADAPTIVE_BRIGHTNESS.c_str()).toInt() > 0);
    mqtt->publishAdaptiveBrightnessState();
  }

  // Handle weather preview - sets weather effect and state locally (no MQTT publish)
  if (request->hasArg(KEY_WEATHER_DEBUG.c_str())) {
    String weather_state = request->arg(KEY_WEATHER_DEBUG.c_str());
    if (weather_state.length() > 0) {
      // Disable adaptive brightness and set to full brightness (locally only)
      mqtt->setAdaptiveBrightnessEnabled(false);
      mqtt->setBrightness(255);
      // Enable weather effect (locally only)
      mqtt->setRainbowEnabled(false);
      mqtt->setRainbowMultiEnabled(false);
      mqtt->setCircadianEnabled(false);
      mqtt->setSensorEnabled(false);
      mqtt->setWeatherEnabled(true);
      // Set weather state directly (no MQTT publish)
      mqtt->setWeatherState(weather_state);

      // Calculate and set motor position for preview
      float target_position = MOTOR_POSITION_OPEN; // Default: open (sunny)
      if (weather_state == "rainy" || weather_state == "pouring" ||
          weather_state == "lightning" || weather_state == "lightning-rainy" ||
          weather_state == "hail" || weather_state == "snowy" || weather_state == "snowy-rainy") {
        target_position = MOTOR_POSITION_CLOSED;
      } else if (weather_state == "partlycloudy") {
        target_position = 0.75f;
      } else if (weather_state == "cloudy" || weather_state == "fog" ||
                 weather_state == "windy" || weather_state == "windy-variant") {
        target_position = 0.5f;
      }
      // Force motor to move to target position
      configuration.motor_position = target_position;
      // No publishLightState() - this is just a local preview
    }
  }

  // If color was changed via web, publish to MQTT
  if (request->hasArg(KEY_COLOR.c_str())) {
    // If no effect specified, disable all effects (static color mode)
    if (!request->hasArg(KEY_EFFECT.c_str())) {
      mqtt->setRainbowEnabled(false);
      mqtt->setRainbowMultiEnabled(false);
      mqtt->setCircadianEnabled(false);
      mqtt->setWeatherEnabled(false);
      mqtt->setSensorEnabled(false);
    }
    mqtt->publishLightState();
  }

  // If mode was changed via web, sync with MQTT
  if (request->hasArg(KEY_IS_AUTONOMOUS.c_str())) {
    mqtt->publishModeState();
  }

  handleUpdateWeb(request);
}
