#pragma once

#include <LittleFS.h>
#include <ArduinoJson.h>
#include "ReminderCore.h"

// Two alternating records: a failed write never overwrites the latest good one.
class Storage {
 public:
  bool begin(Reminder::Engine& engine, bool reset) {
    // LittleFS initialises an unformatted flash filesystem on first boot.
    mounted_ = LittleFS.begin();
    if (!mounted_) return healthy_ = false;
    if (reset) {
      LittleFS.remove("/state0.json");
      LittleFS.remove("/state1.json");
    }
    Reminder::Settings s[2];
    Reminder::History h[2];
    uint32_t generation[2] = {};
    const bool valid0 = read(0, s[0], h[0], generation[0]);
    const bool valid1 = read(1, s[1], h[1], generation[1]);
    if (valid0 || valid1) {
      active_ = !valid0 ? 1 : !valid1 ? 0 :
                static_cast<int32_t>(generation[1] - generation[0]) > 0 ? 1 : 0;
      generation_ = generation[active_];
      engine.restore(s[active_], h[active_]);
      return healthy_ = true;
    }
    if (LittleFS.exists("/state0.json") || LittleFS.exists("/state1.json")) {
      Serial.println(F("Saved records invalid; using defaults. Check storage before use."));
      return healthy_ = false;
    }
    return save(engine);
  }

  bool save(const Reminder::Engine& engine) {
    if (!mounted_) return healthy_ = false;
    JsonDocument doc;
    doc["schema"] = 1;
    doc["generation"] = generation_ + 1;
    doc["minute"] = engine.settings.minute;
    doc["reminder"] = engine.settings.reminderColour;
    doc["ack"] = engine.settings.ackColour;
    doc["date"] = engine.history.lastTriggeredDate;
    doc["pending"] = engine.history.pending;
    const int target = active_ == 0 ? 1 : 0;
    const char* path = target ? "/state1.json" : "/state0.json";
    File file = LittleFS.open("/state.tmp", "w");
    if (!file) return healthy_ = false;
    const size_t expected = measureJson(doc);
    const size_t written = serializeJson(doc, file);
    file.flush();
    file.close();
    if (written != expected || !LittleFS.rename("/state.tmp", path)) {
      return healthy_ = false;
    }
    // Confirm the committed record can be read back before marking it good.
    Reminder::Settings verifyS;
    Reminder::History verifyH;
    uint32_t verifyGeneration;
    if (!read(target, verifyS, verifyH, verifyGeneration) ||
        verifyGeneration != generation_ + 1) return healthy_ = false;
    active_ = target;
    ++generation_;
    return healthy_ = true;
  }

  bool healthy() const { return healthy_; }

 private:
  bool read(int slot, Reminder::Settings& s, Reminder::History& h, uint32_t& generation) {
    File file = LittleFS.open(slot ? "/state1.json" : "/state0.json", "r");
    if (!file || file.size() > 512) return false;
    JsonDocument doc;
    if (deserializeJson(doc, file)) return false;
    if (doc["schema"] != 1 || !doc["generation"].is<uint32_t>() ||
        !doc["minute"].is<uint16_t>() || !doc["reminder"].is<uint32_t>() ||
        !doc["ack"].is<uint32_t>() || !doc["date"].is<uint32_t>() ||
        !doc["pending"].is<bool>()) return false;
    s.minute = doc["minute"];
    s.reminderColour = doc["reminder"];
    s.ackColour = doc["ack"];
    h.lastTriggeredDate = doc["date"];
    h.pending = doc["pending"];
    generation = doc["generation"];
    return s.minute < 1440 && s.reminderColour > 0 && s.reminderColour <= 0xFFFFFF &&
           s.ackColour > 0 && s.ackColour <= 0xFFFFFF &&
           (Reminder::validDate(h.lastTriggeredDate) || (!h.lastTriggeredDate && !h.pending));
  }

  bool mounted_ = false, healthy_ = false;
  int active_ = -1;
  uint32_t generation_ = 0;
};
