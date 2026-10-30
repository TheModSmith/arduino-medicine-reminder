#pragma once

#include <ArduinoJson.h>
#include <stdio.h>

namespace Discovery {
constexpr unsigned COUNT = 11;
constexpr const char* KEYS[] = {"reminder_time", "reminder_colour", "ack_colour",
                               "reminder_active", "clock_ready", "state", "storage_ok",
                               "test_leds", "clear_reminder", "rearm_reminder", "last_triggered_date"};
constexpr const char* NAMES[] = {"Daily reminder time (24h)", "Reminder colour (#RRGGBB)",
                                "Acknowledgement colour (#RRGGBB)", "Reminder due", "Time synced",
                                "Reminder status", "Settings saved", "Check all LEDs (5 seconds)",
                                "Dismiss current reminder", "Allow another reminder today",
                                "Last reminder date"};
constexpr unsigned COMMAND_COUNT = 6;
inline const char* commandKey(unsigned i) {
  constexpr unsigned discoveryIndex[] = {0, 1, 2, 7, 8, 9};
  return i < COMMAND_COUNT ? KEYS[discoveryIndex[i]] : nullptr;
}
inline const char* component(unsigned i) {
  return i < 3 ? "text" : (i == 5 || i == 10) ? "sensor" : i >= 7 ? "button" : "binary_sensor";
}

// Kept separate so the exact discovery payloads can be tested on a computer.
inline void build(JsonDocument& doc, unsigned i, const char* id, const char* base,
                  const char* deviceName, const char* version) {
  doc.clear();
  if (i >= COUNT) return;
  char text[160];
  doc["name"] = NAMES[i];
  snprintf(text, sizeof(text), "%s_%s", id, KEYS[i]);
  doc["unique_id"] = text;
  if (i < 7 || i == 10) {
    snprintf(text, sizeof(text), "%s/state", base);
    doc["state_topic"] = text;
    snprintf(text, sizeof(text), "{{ value_json.%s }}", KEYS[i]);
    doc["value_template"] = text;
  }
  snprintf(text, sizeof(text), "%s/availability", base);
  doc["availability_topic"] = text;
  doc["payload_available"] = "online";
  doc["payload_not_available"] = "offline";
  doc["qos"] = 1;
  doc["device"]["identifiers"][0] = id;
  doc["device"]["name"] = deviceName;
  doc["device"]["manufacturer"] = "DIY";
  doc["device"]["model"] = "LOLIN D1 mini v4 / 14 LED medicine reminder";
  doc["device"]["sw_version"] = version;
  if (i < 3) {
    doc["entity_category"] = "config";
    snprintf(text, sizeof(text), "%s/%s/set", base, KEYS[i]);
    doc["command_topic"] = text;
    doc["retain"] = false;
    doc["mode"] = "text";
    doc["min"] = i == 0 ? 5 : 7;
    doc["max"] = i == 0 ? 5 : 7;
    doc["pattern"] = i == 0 ? "^([01][0-9]|2[0-3]):[0-5][0-9]$" :
                             "^#(?!000000$)[0-9A-Fa-f]{6}$";
    doc["icon"] = i == 0 ? "mdi:clock-outline" : "mdi:palette";
  } else if (i < 7 || i == 10) {
    // The due flag and plain-language status are useful every day. Keep only
    // clock, flash-save and event-history details in Diagnostics.
    if (i == 4 || i == 6 || i == 10) doc["entity_category"] = "diagnostic";
    if (i != 5 && i != 10) {
      doc["payload_on"] = "ON";
      doc["payload_off"] = "OFF";
    }
    doc["icon"] = i == 3 ? "mdi:pill" : i == 4 ? "mdi:clock-check-outline" :
                  i == 5 ? "mdi:information-outline" : i == 6 ? "mdi:content-save-check-outline" :
                           "mdi:calendar-check-outline";
  } else {
    snprintf(text, sizeof(text), "%s/%s/set", base, KEYS[i]);
    doc["command_topic"] = text;
    doc["payload_press"] = "PRESS";
    doc["retain"] = false;
    doc["icon"] = i == 7 ? "mdi:led-strip-variant" :
                  i == 8 ? "mdi:check-circle-outline" : "mdi:calendar-refresh";
  }
}
}
