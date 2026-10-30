#pragma once

#include <Arduino.h>
#include <TZ.h>

// UK local time, including automatic GMT/BST changes. See TZ.h for other zones.
// Keep this as a macro: ESP8266 timezone definitions are flash-string macros.
#define REMINDER_TIMEZONE TZ_Europe_London

namespace Config {
// Edit these, then upload using the D1 Mini's own USB-C connector.
constexpr char WIFI_SSID[] = "";
constexpr char WIFI_PASSWORD[] = "";

constexpr char NTP_SERVER_1[] = "pool.ntp.org";
constexpr char NTP_SERVER_2[] = "time.cloudflare.com";
constexpr char NTP_SERVER_3[] = "time.google.com";

// Optional Home Assistant MQTT integration. Empty host = completely disabled.
// Use your broker's LAN address, not the Home Assistant web URL.
constexpr char MQTT_HOST[] = "";  // e.g. "192.168.1.10"
constexpr uint16_t MQTT_PORT = 1883;
constexpr char MQTT_USERNAME[] = "";
constexpr char MQTT_PASSWORD[] = "";
constexpr char DISCOVERY_PREFIX[] = "homeassistant";
constexpr char HA_BIRTH_TOPIC[] = "homeassistant/status";
constexpr char DEVICE_NAME[] = "Medicine Reminder";

constexpr uint8_t LED_PIN = 4;      // Arduino D2; V4 pad printed 4/SDA -> strip DIN
constexpr uint8_t BUTTON_PIN = 14;  // Arduino D5; V4 pad printed 14/SCK -> switch -> GND
constexpr uint16_t LED_COUNT = 14;
constexpr uint8_t BRIGHTNESS = 64;  // 0..255; applies to every event
constexpr uint32_t PREVIEW_COLOUR = 0xFFFFFF;  // soft white at the above brightness

// These are first-run defaults. Saved Home Assistant changes take precedence.
constexpr uint16_t DEFAULT_REMINDER_MINUTE = 21 * 60;
constexpr uint32_t DEFAULT_REMINDER_COLOUR = 0xFF0000;
constexpr uint32_t DEFAULT_ACK_COLOUR = 0x00FF00;

// Leave false normally. Set true for ONE upload to reset settings/history,
// then return it to false and upload again. Does not erase Wi-Fi credentials.
constexpr bool RESET_SAVED_SETTINGS = false;
constexpr char FIRMWARE_VERSION[] = "1.3.0";
}

