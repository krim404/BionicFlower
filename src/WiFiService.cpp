
// MARK: Includes

#include "WiFiService.h"
#include "Credentials.h"

// MARK: Constants

const String PRINT_PREFIX = "[WiFi]: ";

// MARK: Initialization

WiFiService::WiFiService() {
  this->start_completion = [](boolean success) {
    return;
  };
}

// MARK: Methods

void WiFiService::start(IPAddress ip, std::function<void(bool)> completion) {
  this->ip_address = ip;
  this->start_completion = completion;

  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.onEvent(std::bind(&WiFiService::onEvent, this, std::placeholders::_1));
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

int WiFiService::getActiveConnectionCount() {
  return WiFi.softAPgetStationNum();
}

// MARK: Helpers

void WiFiService::startCompleted(boolean success) {
  this->start_completion(success);
  this->start_completion = [](boolean success) {
    return;
  };
}

void WiFiService::onEvent(WiFiEvent_t event) {
  switch (event) {
    case SYSTEM_EVENT_WIFI_READY:
      Serial.println(PRINT_PREFIX + "Event: Wifi ready!");
      Serial.println(PRINT_PREFIX + "Connecting to " + String(WIFI_SSID) + "...");
      break;
    case SYSTEM_EVENT_SCAN_DONE:
      Serial.println(PRINT_PREFIX + "Event: Scan done!");
      break;
    case SYSTEM_EVENT_STA_START:
      Serial.println(PRINT_PREFIX + "Event: Station started");
      break;
    case SYSTEM_EVENT_STA_STOP:
      Serial.println(PRINT_PREFIX + "Event: Station stopped!");
      break;
    case SYSTEM_EVENT_STA_CONNECTED:
      Serial.println(PRINT_PREFIX + "Event: Station connected!");
      break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
      Serial.println(PRINT_PREFIX + "Disconnected! Reconnecting...");
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      break;
    case SYSTEM_EVENT_STA_AUTHMODE_CHANGE:
      Serial.println(PRINT_PREFIX + "Event: Station auth mode changed!");
      break;
    case SYSTEM_EVENT_STA_GOT_IP:
      Serial.println(PRINT_PREFIX + "Connected! IP: " + WiFi.localIP().toString());
      startCompleted(true);
      break;
    case SYSTEM_EVENT_STA_LOST_IP:
      Serial.println(PRINT_PREFIX + "Event: Station lost IP!");
      break;
    case SYSTEM_EVENT_STA_WPS_ER_SUCCESS:
      Serial.println(PRINT_PREFIX + "Event: Station WPS Enrollee Mode successful!");
      break;
    case SYSTEM_EVENT_STA_WPS_ER_FAILED:
      Serial.println(PRINT_PREFIX + "Event: Station WPS Enrollee Mode failed!");
      break;
    case SYSTEM_EVENT_STA_WPS_ER_TIMEOUT:
      Serial.println(PRINT_PREFIX + "Event: Station WPS Enrollee Mode timed out!");
      break;
    case SYSTEM_EVENT_STA_WPS_ER_PIN:
      Serial.println(PRINT_PREFIX + "Event: Station WPS Enrollee Mode PIN!");
      break;
    case SYSTEM_EVENT_AP_START:
      Serial.println(PRINT_PREFIX + "Event: AP started!");
      break;
    case SYSTEM_EVENT_AP_STOP:
      Serial.println(PRINT_PREFIX + "Event: AP stopped!");
      break;
    case SYSTEM_EVENT_GOT_IP6:
      Serial.println(PRINT_PREFIX + "Event: IPv6 preferred!");
      break;
    case SYSTEM_EVENT_ETH_START:
      Serial.println(PRINT_PREFIX + "Event: Ethernet started!");
      break;
    case SYSTEM_EVENT_ETH_STOP:
      Serial.println(PRINT_PREFIX + "Event: Ethernet stopped!");
      break;
    case SYSTEM_EVENT_ETH_CONNECTED:
      Serial.println(PRINT_PREFIX + "Event: Ethernet link up!");
      break;
    case SYSTEM_EVENT_ETH_DISCONNECTED:
      Serial.println(PRINT_PREFIX + "Event: Ethernet link down!");
      break;
    case SYSTEM_EVENT_ETH_GOT_IP:
      Serial.println(PRINT_PREFIX + "Event: Ethernet got IP!");
      break;
    default:
      Serial.println(PRINT_PREFIX + "Event: Unknown!");
      break;
  }
}

String WiFiService::getMacAddress() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  return mac.substring(8, 12);
}
