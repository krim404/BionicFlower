
#ifndef HARDWARESERICE_H_
#define HARDWARESERICE_H_

// MARK: Includes

#include <FastLED.h>
#include <Preferences.h>
#include "Models.h"
#include "Settings.h"
#include <Wire.h>
#include "RPR-0521RS.h"
#include "SparkFun_CAP1203.h"
#include "MotorLogic.h"

// Forward declaration
class MQTTService;

class HardwareService {

  public:

    // MARK: Static Methods

    static HardwareService* getSharedInstance();

    // MARK: Methods

    Configuration getConfiguration() {
      return configuration;
    }

    SensorData getSensorData() {
      return sensor_data;
    }

    void loop(const boolean has_active_connection, uint32_t count);
    void setConfiguration(Configuration configuration);
    void readSensors();
    void updateMotor();
    boolean start();
    void resetSensorData();
    void saveStateToNVS();
    void loadStateFromNVS();

  protected:

  private:

    // MARK: Initialization

    HardwareService();

    // MARK: Properties

    Configuration configuration;
    SensorData sensor_data;
    Ticker sensor_timer;

    CRGB leds[LED_COUNT];
    RPR0521RS light_sensor = RPR0521RS();
    CAP1203 touch_sensor = CAP1203(0x28);
    MotorLogic motor;
    TaskHandle_t task;

    float ambient_brightness;
    uint32_t light_measurement_count;
    uint32_t reopen_cycle_count;
    float intended_motor_position;
    boolean motor_calibration_finished;
    uint16_t rgb_hue;

    // Touch handling for manual mode
    unsigned long touch_left_start;
    unsigned long touch_right_start;
    bool touch_left_was_pressed;
    bool touch_right_was_pressed;
    bool touch_left_long_triggered;
    bool touch_right_long_triggered;

    // Adaptive brightness
    unsigned long last_adaptive_brightness_update;
    uint8_t adaptive_brightness_factor;

    // NVS debouncing
    unsigned long nvs_save_requested_at;
    bool nvs_save_pending;

    // MARK: Methods

    void checkPendingNVSSave();

    void updateAdaptiveBrightness();
    
    void move(float position, float speed);
    void writeLED(Color color);

};

#endif
