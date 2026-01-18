
#ifndef MQTTSERVICE_H_
#define MQTTSERVICE_H_

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "Settings.h"
#include "Models.h"

class MQTTService {

  public:

    // MARK: Static Methods
    static MQTTService* getSharedInstance();

    // MARK: Methods
    void setup();
    void loop();
    bool isConnected();

    // State publishing
    void publishLightState();
    void publishCoverState();
    void publishSensorStates();
    void publishModeState();
    void publishAdaptiveBrightnessState();

    // Effect control
    bool isRainbowEnabled() { return rainbow_enabled; }
    void setRainbowEnabled(bool enabled) { rainbow_enabled = enabled; }
    bool isRainbowMultiEnabled() { return rainbow_multi_enabled; }
    void setRainbowMultiEnabled(bool enabled) { rainbow_multi_enabled = enabled; }
    bool isCircadianEnabled() { return circadian_enabled; }
    void setCircadianEnabled(bool enabled) { circadian_enabled = enabled; }
    bool isWeatherEnabled() { return weather_enabled; }
    void setWeatherEnabled(bool enabled) { weather_enabled = enabled; }
    bool isSensorEnabled() { return sensor_enabled; }
    void setSensorEnabled(bool enabled) { sensor_enabled = enabled; }
    uint8_t getBrightness() { return brightness; }
    void setBrightness(uint8_t b) { brightness = b; }
    bool isLightOn() { return light_on; }
    void setLightOn(bool on) { light_on = on; }
    bool isAdaptiveBrightnessEnabled() { return adaptive_brightness_enabled; }
    void setAdaptiveBrightnessEnabled(bool enabled) { adaptive_brightness_enabled = enabled; }

    // External data for effects
    uint8_t getCircadianHour() { return circadian_hour; }
    String getWeatherState() { return weather_state; }
    void setWeatherState(const String& state) { weather_state = state; }
    float getWeatherTemperature() { return weather_temperature; }

  private:

    // MARK: Initialization
    MQTTService();

    // MARK: Properties
    WiFiClient wifi_client;
    PubSubClient mqtt_client;

    unsigned long last_reconnect_attempt;
    unsigned long last_sensor_publish;

    bool rainbow_enabled;
    bool rainbow_multi_enabled;
    bool circadian_enabled;
    bool weather_enabled;
    bool sensor_enabled;
    bool light_on;
    bool adaptive_brightness_enabled;
    uint8_t brightness;
    bool last_has_light_sensor;
    bool last_has_touch_sensor;

    // External data for effects
    uint8_t circadian_hour;
    String weather_state;
    float weather_temperature;

    // MARK: Methods
    void connect();
    void reconnect();
    void subscribeTopics();

    // Discovery
    void sendDiscoveryAll();
    void sendLightDiscovery();
    void sendCoverDiscovery();
    void sendModeDiscovery();
    void sendAdaptiveBrightnessDiscovery();
    void sendBrightnessSensorDiscovery();
    void sendDistanceSensorDiscovery();
    void sendTouchLeftDiscovery();
    void sendTouchRightDiscovery();
    void sendTemperatureDiscovery();

    // Remove discovery (for hot-unplug)
    void removeBrightnessSensorDiscovery();
    void removeDistanceSensorDiscovery();
    void removeTouchLeftDiscovery();
    void removeTouchRightDiscovery();

    // Callback
    static void messageCallback(char* topic, byte* payload, unsigned int length);
    void handleMessage(char* topic, byte* payload, unsigned int length);

    // Command handlers
    void handleLightCommand(JsonDocument& doc);
    void handleCoverCommand(const String& command);
    void handleCoverPositionCommand(int position);
    void handleModeCommand(const String& mode);
    void handleAdaptiveBrightnessCommand(const String& state);

};

#endif
