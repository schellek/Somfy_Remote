#pragma once

#include <cstdint>
#include "Arduino.h"

constexpr uint8_t TX_GPIO                   = D1;
constexpr uint8_t UP_BTN                    = D2;
constexpr uint8_t STOP_BTN                  = D3;
constexpr uint8_t DOWN_BTN                  = D4;
constexpr uint8_t CHANNEL_INC_BTN           = D5;

constexpr unsigned int REMOTE_BASE_ID       = 0x121305U;
constexpr unsigned int ROLLING_CODE_BEGIN   = 1U;
constexpr unsigned int AMOUNT_CHANNELS      = 3U;

constexpr const char WIFI_SSID              = "<WiFi SSID>";
constexpr const char WIFI_PASSWORD          = "<WiFi Password>";
constexpr const char MQTT_SERVER            = "<MQTT Server>.local";
constexpr const unsigned int MQTT_PORT      = 1883;
constexpr const char MQTT_USER              = "<MQTT User>";
constexpr const char MQTT_PASSWORD          = "<MQTT Password>";

#define TOPIC_PREFIX                          "Garden"
#define CLIENT_ID                             "Awning"
constexpr char MAIN_TOPIC[]                 = TOPIC_PREFIX "/" CLIENT_ID;
constexpr char FEEDBACK_TOPIC[]             = TOPIC_PREFIX "/" CLIENT_ID "/Feedback";
