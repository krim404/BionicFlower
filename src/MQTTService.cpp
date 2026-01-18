
#include "MQTTService.h"
#include "HardwareService.h"

const String PRINT_PREFIX = "[MQTT]: ";
const unsigned long RECONNECT_INTERVAL = 5000;
const unsigned long SENSOR_PUBLISH_INTERVAL = 5000;

MQTTService* mqtt_shared_instance = nullptr;

// MARK: Initialization

MQTTService::MQTTService() : mqtt_client(wifi_client) {
  last_reconnect_attempt = 0;
  last_sensor_publish = 0;
  rainbow_enabled = false;
  rainbow_multi_enabled = true;
  circadian_enabled = false;
  weather_enabled = false;
  sensor_enabled = false;
  light_on = true;
  adaptive_brightness_enabled = true;  // Default: ON
  brightness = 255;
  last_has_light_sensor = false;
  last_has_touch_sensor = false;
  circadian_hour = 12;
  weather_state = "sunny";
  weather_temperature = 20.0f;
}

// MARK: Static Methods

MQTTService* MQTTService::getSharedInstance() {
  if (mqtt_shared_instance == nullptr) {
    mqtt_shared_instance = new MQTTService();
  }
  return mqtt_shared_instance;
}

// MARK: Public Methods

void MQTTService::setup() {
  mqtt_client.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt_client.setCallback(messageCallback);
  mqtt_client.setBufferSize(1024);

  Serial.println(PRINT_PREFIX + "Configured for broker: " + MQTT_BROKER);
}

void MQTTService::loop() {
  if (!mqtt_client.connected()) {
    unsigned long now = millis();
    if (now - last_reconnect_attempt > RECONNECT_INTERVAL) {
      last_reconnect_attempt = now;
      reconnect();
    }
  } else {
    mqtt_client.loop();

    // Check for sensor hot-plug changes
    HardwareService* hw = HardwareService::getSharedInstance();
    SensorData data = hw->getSensorData();

    if (data.has_light_sensor != last_has_light_sensor) {
      if (data.has_light_sensor) {
        sendBrightnessSensorDiscovery();
        sendDistanceSensorDiscovery();
      } else {
        removeBrightnessSensorDiscovery();
        removeDistanceSensorDiscovery();
      }
      last_has_light_sensor = data.has_light_sensor;
    }

    if (data.has_touch_sensor != last_has_touch_sensor) {
      if (data.has_touch_sensor) {
        sendTouchLeftDiscovery();
        sendTouchRightDiscovery();
      } else {
        removeTouchLeftDiscovery();
        removeTouchRightDiscovery();
      }
      last_has_touch_sensor = data.has_touch_sensor;
    }

    // Publish sensor states periodically
    unsigned long now = millis();
    if (now - last_sensor_publish > SENSOR_PUBLISH_INTERVAL) {
      last_sensor_publish = now;
      publishSensorStates();
      publishCoverState();
    }
  }
}

bool MQTTService::isConnected() {
  return mqtt_client.connected();
}

// MARK: Connection

void MQTTService::reconnect() {
  Serial.println(PRINT_PREFIX + "Attempting connection...");

  if (mqtt_client.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
    Serial.println(PRINT_PREFIX + "Connected!");
    subscribeTopics();
    sendDiscoveryAll();

    // Publish initial states
    publishLightState();
    publishCoverState();
    publishModeState();
    publishAdaptiveBrightnessState();
    publishSensorStates();
  } else {
    Serial.println(PRINT_PREFIX + "Failed, rc=" + String(mqtt_client.state()));
  }
}

void MQTTService::subscribeTopics() {
  mqtt_client.subscribe(MQTT_BASE_TOPIC "/light/set");
  mqtt_client.subscribe(MQTT_BASE_TOPIC "/cover/set");
  mqtt_client.subscribe(MQTT_BASE_TOPIC "/cover/set_position");
  mqtt_client.subscribe(MQTT_BASE_TOPIC "/select/mode/set");
  mqtt_client.subscribe(MQTT_BASE_TOPIC "/switch/adaptive_brightness/set");
  mqtt_client.subscribe(MQTT_BASE_TOPIC "/weather/state");
  mqtt_client.subscribe(MQTT_BASE_TOPIC "/weather/temperature");

  Serial.println(PRINT_PREFIX + "Subscribed to command topics");
}

// MARK: Discovery

void MQTTService::sendDiscoveryAll() {
  sendLightDiscovery();
  sendCoverDiscovery();
  sendModeDiscovery();
  sendAdaptiveBrightnessDiscovery();
  sendTemperatureDiscovery();

  HardwareService* hw = HardwareService::getSharedInstance();
  SensorData data = hw->getSensorData();

  if (data.has_light_sensor) {
    sendBrightnessSensorDiscovery();
    sendDistanceSensorDiscovery();
    last_has_light_sensor = true;
  }

  if (data.has_touch_sensor) {
    sendTouchLeftDiscovery();
    sendTouchRightDiscovery();
    last_has_touch_sensor = true;
  }
}

void MQTTService::sendLightDiscovery() {
  JsonDocument doc;

  doc["name"] = "Bionic Flower Light";
  doc["unique_id"] = "bionic_flower_light";
  doc["command_topic"] = MQTT_BASE_TOPIC "/light/set";
  doc["state_topic"] = MQTT_BASE_TOPIC "/light/state";
  doc["schema"] = "json";
  doc["brightness"] = true;
  doc["effect"] = true;

  JsonArray color_modes = doc["supported_color_modes"].to<JsonArray>();
  color_modes.add("rgb");

  JsonArray effects = doc["effect_list"].to<JsonArray>();
  effects.add("None");
  effects.add("Rainbow");
  effects.add("Rainbow Multi");
  effects.add("Circadian");
  effects.add("Weather");
  effects.add("Sensor");

  JsonObject device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = "bionic_flower";
  device["name"] = "Bionic Flower";
  device["model"] = "ESP32 Bionic Flower";
  device["manufacturer"] = "DIY";

  char buffer[1024];
  serializeJson(doc, buffer);

  mqtt_client.publish(MQTT_DISCOVERY_PREFIX "/light/bionic_flower/light/config", buffer, true);
  Serial.println(PRINT_PREFIX + "Sent light discovery");
}

void MQTTService::sendCoverDiscovery() {
  JsonDocument doc;

  doc["name"] = "Bionic Flower Cover";
  doc["unique_id"] = "bionic_flower_cover";
  doc["command_topic"] = MQTT_BASE_TOPIC "/cover/set";
  doc["state_topic"] = MQTT_BASE_TOPIC "/cover/state";
  doc["position_topic"] = MQTT_BASE_TOPIC "/cover/position";
  doc["set_position_topic"] = MQTT_BASE_TOPIC "/cover/set_position";
  doc["device_class"] = "shade";
  doc["position_open"] = 100;
  doc["position_closed"] = 0;

  JsonObject device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = "bionic_flower";

  char buffer[512];
  serializeJson(doc, buffer);

  mqtt_client.publish(MQTT_DISCOVERY_PREFIX "/cover/bionic_flower/cover/config", buffer, true);
  Serial.println(PRINT_PREFIX + "Sent cover discovery");
}

void MQTTService::sendModeDiscovery() {
  JsonDocument doc;

  doc["name"] = "Bionic Flower Mode";
  doc["unique_id"] = "bionic_flower_mode";
  doc["command_topic"] = MQTT_BASE_TOPIC "/select/mode/set";
  doc["state_topic"] = MQTT_BASE_TOPIC "/select/mode/state";

  JsonArray options = doc["options"].to<JsonArray>();
  options.add("Manual");
  options.add("Automatic");

  JsonObject device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = "bionic_flower";

  char buffer[512];
  serializeJson(doc, buffer);

  mqtt_client.publish(MQTT_DISCOVERY_PREFIX "/select/bionic_flower/mode/config", buffer, true);
  Serial.println(PRINT_PREFIX + "Sent mode discovery");
}

void MQTTService::sendAdaptiveBrightnessDiscovery() {
  JsonDocument doc;

  doc["name"] = "Bionic Flower Adaptive Brightness";
  doc["unique_id"] = "bionic_flower_adaptive_brightness";
  doc["command_topic"] = MQTT_BASE_TOPIC "/switch/adaptive_brightness/set";
  doc["state_topic"] = MQTT_BASE_TOPIC "/switch/adaptive_brightness/state";
  doc["icon"] = "mdi:brightness-auto";

  JsonObject device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = "bionic_flower";

  char buffer[512];
  serializeJson(doc, buffer);

  mqtt_client.publish(MQTT_DISCOVERY_PREFIX "/switch/bionic_flower/adaptive_brightness/config", buffer, true);
  Serial.println(PRINT_PREFIX + "Sent adaptive brightness discovery");
}

void MQTTService::sendBrightnessSensorDiscovery() {
  JsonDocument doc;

  doc["name"] = "Bionic Flower Illuminance";
  doc["unique_id"] = "bionic_flower_illuminance";
  doc["state_topic"] = MQTT_BASE_TOPIC "/sensor/illuminance";
  doc["unit_of_measurement"] = "%";
  doc["value_template"] = "{{ value | round(1) }}";
  doc["icon"] = "mdi:brightness-percent";

  JsonObject device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = "bionic_flower";

  char buffer[512];
  serializeJson(doc, buffer);

  mqtt_client.publish(MQTT_DISCOVERY_PREFIX "/sensor/bionic_flower/illuminance/config", buffer, true);
  Serial.println(PRINT_PREFIX + "Sent illuminance sensor discovery");
}

void MQTTService::sendDistanceSensorDiscovery() {
  JsonDocument doc;

  doc["name"] = "Bionic Flower Proximity";
  doc["unique_id"] = "bionic_flower_proximity";
  doc["state_topic"] = MQTT_BASE_TOPIC "/sensor/proximity";
  doc["unit_of_measurement"] = "%";
  doc["value_template"] = "{{ value | round(1) }}";
  doc["icon"] = "mdi:signal-distance-variant";

  JsonObject device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = "bionic_flower";

  char buffer[512];
  serializeJson(doc, buffer);

  mqtt_client.publish(MQTT_DISCOVERY_PREFIX "/sensor/bionic_flower/proximity/config", buffer, true);
  Serial.println(PRINT_PREFIX + "Sent proximity sensor discovery");
}

void MQTTService::sendTouchLeftDiscovery() {
  JsonDocument doc;

  doc["name"] = "Bionic Flower Touch Left";
  doc["unique_id"] = "bionic_flower_touch_left";
  doc["state_topic"] = MQTT_BASE_TOPIC "/binary_sensor/touch_left";
  doc["device_class"] = "occupancy";
  doc["payload_on"] = "ON";
  doc["payload_off"] = "OFF";

  JsonObject device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = "bionic_flower";

  char buffer[512];
  serializeJson(doc, buffer);

  mqtt_client.publish(MQTT_DISCOVERY_PREFIX "/binary_sensor/bionic_flower/touch_left/config", buffer, true);
  Serial.println(PRINT_PREFIX + "Sent touch left discovery");
}

void MQTTService::sendTouchRightDiscovery() {
  JsonDocument doc;

  doc["name"] = "Bionic Flower Touch Right";
  doc["unique_id"] = "bionic_flower_touch_right";
  doc["state_topic"] = MQTT_BASE_TOPIC "/binary_sensor/touch_right";
  doc["device_class"] = "occupancy";
  doc["payload_on"] = "ON";
  doc["payload_off"] = "OFF";

  JsonObject device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = "bionic_flower";

  char buffer[512];
  serializeJson(doc, buffer);

  mqtt_client.publish(MQTT_DISCOVERY_PREFIX "/binary_sensor/bionic_flower/touch_right/config", buffer, true);
  Serial.println(PRINT_PREFIX + "Sent touch right discovery");
}

void MQTTService::sendTemperatureDiscovery() {
  JsonDocument doc;

  doc["name"] = "Bionic Flower Temperature";
  doc["unique_id"] = "bionic_flower_temperature";
  doc["state_topic"] = MQTT_BASE_TOPIC "/sensor/temperature";
  doc["device_class"] = "temperature";
  doc["unit_of_measurement"] = "°C";
  doc["value_template"] = "{{ value | round(1) }}";

  JsonObject device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = "bionic_flower";

  char buffer[512];
  serializeJson(doc, buffer);

  mqtt_client.publish(MQTT_DISCOVERY_PREFIX "/sensor/bionic_flower/temperature/config", buffer, true);
  Serial.println(PRINT_PREFIX + "Sent temperature sensor discovery");
}

// MARK: Remove Discovery (hot-unplug)

void MQTTService::removeBrightnessSensorDiscovery() {
  mqtt_client.publish(MQTT_DISCOVERY_PREFIX "/sensor/bionic_flower/illuminance/config", "", true);
  Serial.println(PRINT_PREFIX + "Removed illuminance sensor");
}

void MQTTService::removeDistanceSensorDiscovery() {
  mqtt_client.publish(MQTT_DISCOVERY_PREFIX "/sensor/bionic_flower/proximity/config", "", true);
  Serial.println(PRINT_PREFIX + "Removed proximity sensor");
}

void MQTTService::removeTouchLeftDiscovery() {
  mqtt_client.publish(MQTT_DISCOVERY_PREFIX "/binary_sensor/bionic_flower/touch_left/config", "", true);
  Serial.println(PRINT_PREFIX + "Removed touch left sensor");
}

void MQTTService::removeTouchRightDiscovery() {
  mqtt_client.publish(MQTT_DISCOVERY_PREFIX "/binary_sensor/bionic_flower/touch_right/config", "", true);
  Serial.println(PRINT_PREFIX + "Removed touch right sensor");
}

// MARK: State Publishing

void MQTTService::publishLightState() {
  HardwareService* hw = HardwareService::getSharedInstance();
  Configuration config = hw->getConfiguration();

  JsonDocument doc;

  doc["state"] = light_on ? "ON" : "OFF";
  doc["brightness"] = brightness;
  doc["color_mode"] = "rgb";

  JsonObject color = doc["color"].to<JsonObject>();
  color["r"] = config.color.red;
  color["g"] = config.color.green;
  color["b"] = config.color.blue;

  if (sensor_enabled) {
    doc["effect"] = "Sensor";
  } else if (weather_enabled) {
    doc["effect"] = "Weather";
  } else if (circadian_enabled) {
    doc["effect"] = "Circadian";
  } else if (rainbow_multi_enabled) {
    doc["effect"] = "Rainbow Multi";
  } else if (rainbow_enabled) {
    doc["effect"] = "Rainbow";
  } else {
    doc["effect"] = "None";
  }

  char buffer[256];
  serializeJson(doc, buffer);

  mqtt_client.publish(MQTT_BASE_TOPIC "/light/state", buffer, true);
}

void MQTTService::publishCoverState() {
  HardwareService* hw = HardwareService::getSharedInstance();
  Configuration config = hw->getConfiguration();

  // motor_position: 0 = open (physically), 1 = closed (physically)
  // For Home Assistant: 100% = open, 0% = closed
  // Direct mapping without inversion
  int position = (int)(config.motor_position * 100);

  const char* state;
  if (position >= 99) {
    state = "open";
  } else if (position <= 1) {
    state = "closed";
  } else {
    state = "stopped";
  }

  Serial.println(PRINT_PREFIX + "Cover position: " + String(position) + "%, state: " + state);
  mqtt_client.publish(MQTT_BASE_TOPIC "/cover/state", state, true);
  mqtt_client.publish(MQTT_BASE_TOPIC "/cover/position", String(position).c_str(), true);
}

void MQTTService::publishSensorStates() {
  HardwareService* hw = HardwareService::getSharedInstance();
  SensorData data = hw->getSensorData();

  if (data.has_light_sensor) {
    float illuminance_percent = data.brightness * 100;
    mqtt_client.publish(MQTT_BASE_TOPIC "/sensor/illuminance", String(illuminance_percent).c_str());

    float proximity_percent = data.distance * 100;
    mqtt_client.publish(MQTT_BASE_TOPIC "/sensor/proximity", String(proximity_percent).c_str());
  }

  if (data.has_touch_sensor) {
    mqtt_client.publish(MQTT_BASE_TOPIC "/binary_sensor/touch_left", data.touch_left ? "ON" : "OFF");
    mqtt_client.publish(MQTT_BASE_TOPIC "/binary_sensor/touch_right", data.touch_right ? "ON" : "OFF");
  }

  // ESP32 internal temperature (with calibration offset, raw value is ~30°C too high)
  float temp = temperatureRead() - 30.0f;
  mqtt_client.publish(MQTT_BASE_TOPIC "/sensor/temperature", String(temp).c_str());
}

void MQTTService::publishModeState() {
  HardwareService* hw = HardwareService::getSharedInstance();
  Configuration config = hw->getConfiguration();

  const char* mode = config.is_autonomous ? "Automatic" : "Manual";
  mqtt_client.publish(MQTT_BASE_TOPIC "/select/mode/state", mode, true);
}

void MQTTService::publishAdaptiveBrightnessState() {
  const char* state = adaptive_brightness_enabled ? "ON" : "OFF";
  mqtt_client.publish(MQTT_BASE_TOPIC "/switch/adaptive_brightness/state", state, true);
}

// MARK: Message Callback

void MQTTService::messageCallback(char* topic, byte* payload, unsigned int length) {
  if (mqtt_shared_instance != nullptr) {
    mqtt_shared_instance->handleMessage(topic, payload, length);
  }
}

void MQTTService::handleMessage(char* topic, byte* payload, unsigned int length) {
  String topicStr = String(topic);
  String payloadStr;

  for (unsigned int i = 0; i < length; i++) {
    payloadStr += (char)payload[i];
  }

  Serial.println(PRINT_PREFIX + "Received: " + topicStr + " = " + payloadStr);

  if (topicStr == MQTT_BASE_TOPIC "/light/set") {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payloadStr);
    if (!error) {
      handleLightCommand(doc);
    }
  } else if (topicStr == MQTT_BASE_TOPIC "/cover/set") {
    handleCoverCommand(payloadStr);
  } else if (topicStr == MQTT_BASE_TOPIC "/cover/set_position") {
    handleCoverPositionCommand(payloadStr.toInt());
  } else if (topicStr == MQTT_BASE_TOPIC "/select/mode/set") {
    handleModeCommand(payloadStr);
  } else if (topicStr == MQTT_BASE_TOPIC "/switch/adaptive_brightness/set") {
    handleAdaptiveBrightnessCommand(payloadStr);
  } else if (topicStr == MQTT_BASE_TOPIC "/weather/state") {
    weather_state = payloadStr;
    Serial.println(PRINT_PREFIX + "Weather state: " + weather_state);
  } else if (topicStr == MQTT_BASE_TOPIC "/weather/temperature") {
    weather_temperature = payloadStr.toFloat();
    Serial.println(PRINT_PREFIX + "Weather temperature: " + String(weather_temperature));
  }
}

// MARK: Command Handlers

void MQTTService::handleLightCommand(JsonDocument& doc) {
  HardwareService* hw = HardwareService::getSharedInstance();
  Configuration config = hw->getConfiguration();

  // Check for effect first
  if (doc["effect"].is<const char*>()) {
    String effect = doc["effect"].as<String>();
    // Reset all effects
    rainbow_enabled = false;
    rainbow_multi_enabled = false;
    circadian_enabled = false;
    weather_enabled = false;
    sensor_enabled = false;

    if (effect == "Sensor") {
      sensor_enabled = true;
    } else if (effect == "Weather") {
      weather_enabled = true;
    } else if (effect == "Circadian") {
      circadian_enabled = true;
    } else if (effect == "Rainbow Multi") {
      rainbow_multi_enabled = true;
    } else if (effect == "Rainbow") {
      rainbow_enabled = true;
    }
    // "None" leaves all disabled
  }

  // Handle brightness (applies to both rainbow and static color)
  if (doc["brightness"].is<int>()) {
    brightness = doc["brightness"].as<int>();
  }

  // If color is set, disable all effects
  if (doc["color"].is<JsonObject>()) {
    rainbow_enabled = false;
    rainbow_multi_enabled = false;
    circadian_enabled = false;
    weather_enabled = false;
    sensor_enabled = false;
    JsonObject color = doc["color"];
    config.color.red = color["r"] | config.color.red;
    config.color.green = color["g"] | config.color.green;
    config.color.blue = color["b"] | config.color.blue;
  }

  // Handle on/off
  if (doc["state"].is<const char*>()) {
    String state = doc["state"].as<String>();
    if (state == "OFF") {
      light_on = false;
      Serial.println(PRINT_PREFIX + "Light OFF");
    } else if (state == "ON") {
      light_on = true;
      Serial.println(PRINT_PREFIX + "Light ON");
    }
  }

  Serial.println(PRINT_PREFIX + "Light state: " + String(light_on ? "ON" : "OFF") + ", Rainbow: " + String(rainbow_enabled ? "ON" : "OFF") + ", Brightness: " + String(brightness));

  hw->setConfiguration(config);
  publishLightState();
}

void MQTTService::handleCoverCommand(const String& command) {
  HardwareService* hw = HardwareService::getSharedInstance();
  Configuration config = hw->getConfiguration();

  if (command == "OPEN") {
    config.motor_position = MOTOR_POSITION_OPEN;
  } else if (command == "CLOSE") {
    config.motor_position = MOTOR_POSITION_CLOSED;
  } else if (command == "STOP") {
    // Keep current position
  }

  config.speed = 1.0f;
  hw->setConfiguration(config);
  publishCoverState();
}

void MQTTService::handleCoverPositionCommand(int position) {
  HardwareService* hw = HardwareService::getSharedInstance();
  Configuration config = hw->getConfiguration();

  // Direct mapping: MQTT 100% (open) -> internal 1.0, MQTT 0% (closed) -> internal 0
  config.motor_position = position / 100.0f;
  config.speed = 1.0f;
  hw->setConfiguration(config);
  publishCoverState();
}

void MQTTService::handleModeCommand(const String& mode) {
  HardwareService* hw = HardwareService::getSharedInstance();
  Configuration config = hw->getConfiguration();

  config.is_autonomous = (mode == "Automatic");
  hw->setConfiguration(config);
  publishModeState();
}

void MQTTService::handleAdaptiveBrightnessCommand(const String& state) {
  adaptive_brightness_enabled = (state == "ON");
  Serial.println(PRINT_PREFIX + "Adaptive brightness: " + String(adaptive_brightness_enabled ? "ON" : "OFF"));
  publishAdaptiveBrightnessState();
}
