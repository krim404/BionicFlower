
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

    // Rainbow effect control
    bool isRainbowEnabled() { return rainbow_enabled; }
    void setRainbowEnabled(bool enabled) { rainbow_enabled = enabled; }
    uint8_t getBrightness() { return brightness; }
    void setBrightness(uint8_t b) { brightness = b; }
    bool isLightOn() { return light_on; }
    void setLightOn(bool on) { light_on = on; }

  private:

    // MARK: Initialization
    MQTTService();

    // MARK: Properties
    WiFiClient wifi_client;
    PubSubClient mqtt_client;

    unsigned long last_reconnect_attempt;
    unsigned long last_sensor_publish;

    bool rainbow_enabled;
    bool light_on;
    uint8_t brightness;
    bool last_has_light_sensor;
    bool last_has_touch_sensor;

    // MARK: Methods
    void connect();
    void reconnect();
    void subscribeTopics();

    // Discovery
    void sendDiscoveryAll();
    void sendLightDiscovery();
    void sendCoverDiscovery();
    void sendModeDiscovery();
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

};

#endif
