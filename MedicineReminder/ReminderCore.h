#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

namespace Reminder {
struct Settings {
  uint16_t minute = 21 * 60;
  uint32_t reminderColour = 0xFF0000;
  uint32_t ackColour = 0x00FF00;
};

struct History {
  uint32_t lastTriggeredDate = 0;  // local YYYYMMDD; also a duplicate-event guard
  bool pending = false;
};

inline bool validDate(uint32_t date) {
  const unsigned year = date / 10000;
  const unsigned month = (date / 100) % 100;
  const unsigned day = date % 100;
  if (year < 2024 || year > 2099 || month < 1 || month > 12) return false;
  const unsigned days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  const unsigned maxDay = days[month - 1] + (month == 2 && year % 4 == 0);
  return day >= 1 && day <= maxDay;
}

inline bool parseTime(const char* text, uint16_t& result) {
  if (!text || strlen(text) != 5 || text[2] != ':') return false;
  for (unsigned i = 0; i < 5; ++i) {
    if (i == 2) continue;
    if (text[i] < '0' || text[i] > '9') return false;
  }
  const unsigned h = (text[0] - '0') * 10 + text[1] - '0';
  const unsigned m = (text[3] - '0') * 10 + text[4] - '0';
  if (h > 23 || m > 59) return false;
  result = h * 60 + m;
  return true;
}

inline bool parseColour(const char* text, uint32_t& result) {
  if (!text || strlen(text) != 7 || text[0] != '#') return false;
  uint32_t colour = 0;
  for (unsigned i = 1; i < 7; ++i) {
    const char c = text[i];
    int digit = c >= '0' && c <= '9' ? c - '0' :
                c >= 'A' && c <= 'F' ? c - 'A' + 10 :
                c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    if (digit < 0) return false;
    colour = (colour << 4) | static_cast<uint32_t>(digit);
  }
  // An invisible reminder/acknowledgement would be an easy configuration mistake.
  if (!colour) return false;
  result = colour;
  return true;
}

inline void formatTime(uint16_t minute, char* result) {
  const unsigned hour = (minute / 60) % 24;
  const unsigned minutes = minute % 60;
  snprintf(result, 6, "%02u:%02u", hour, minutes);
}

inline void formatColour(uint32_t colour, char* result) {
  snprintf(result, 8, "#%06lX", static_cast<unsigned long>(colour));
}

enum class Mode { Idle, Reminder, Acknowledging, Preview };

class Engine {
 public:
  Settings settings;
  History history;

  void restore(const Settings& s, const History& h) {
    settings = s;
    history = h;
    mode_ = h.pending ? Mode::Reminder : Mode::Idle;
  }

  // Returns true only when history needs saving. No dependency on Wi-Fi or MQTT.
  bool tick(uint32_t now, bool clockReady, uint32_t date, uint16_t minute) {
    bool changed = false;
    if (clockReady && validDate(date) && minute < 1440 &&
        date > history.lastTriggeredDate && minute >= settings.minute) {
      history.lastTriggeredDate = date;
      history.pending = true;
      mode_ = Mode::Reminder;  // a due reminder always takes priority over preview
      changed = true;
    }
    const uint32_t elapsed = now - started_;
    if ((mode_ == Mode::Acknowledging && elapsed >= 3000) ||
        (mode_ == Mode::Preview && elapsed >= 2000)) mode_ = Mode::Idle;
    return changed;
  }

  bool press(uint32_t now) {
    if (history.pending) {
      history.pending = false;
      mode_ = Mode::Acknowledging;
      started_ = now;
      return true;
    }
    if (mode_ == Mode::Idle) {
      mode_ = Mode::Preview;
      started_ = now;
    }
    return false;
  }

  // Remote clear acknowledges a pending reminder without the green animation.
  bool clearReminder() {
    if (!history.pending) return false;
    history.pending = false;
    mode_ = Mode::Idle;
    return true;
  }

  // Explicitly permit another trigger today after changing the schedule.
  // The next tick fires immediately if the selected time has already passed.
  bool rearmReminder() {
    const bool changed = history.lastTriggeredDate || history.pending;
    history.lastTriggeredDate = 0;
    history.pending = false;
    mode_ = Mode::Idle;
    return changed;
  }

  Mode mode() const { return mode_; }

  const char* state(bool clockReady) const {
    switch (mode_) {
      case Mode::Reminder: return "reminder";
      case Mode::Acknowledging: return "acknowledging";
      case Mode::Preview: return "preview";
      default: return clockReady ? "idle" : "waiting_for_time";
    }
  }

  // Human-readable Home Assistant status; scheduling still uses mode_ and history.
  const char* displayState(bool clockReady, uint32_t today) const {
    switch (mode_) {
      case Mode::Reminder: return "Medication reminder due";
      case Mode::Acknowledging: return "Acknowledging reminder";
      case Mode::Preview: return "Button light preview";
      default:
        if (!clockReady) return "Waiting for time sync";
        return today && history.lastTriggeredDate == today ?
               "Reminder finished for today" : "Waiting for reminder";
    }
  }

  uint8_t level(uint32_t now) const {
    const uint32_t elapsed = now - started_;
    switch (mode_) {
      case Mode::Reminder: return 255;
      case Mode::Acknowledging:
        return elapsed < 3000 && elapsed % 1000 < 500 ? 255 : 0;
      case Mode::Preview: {
        if (elapsed >= 2000) return 0;
        const uint32_t ramp = elapsed <= 1000 ? elapsed : 2000 - elapsed;
        return static_cast<uint8_t>(ramp * 255 / 1000);
      }
      default: return 0;
    }
  }

 private:
  Mode mode_ = Mode::Idle;
  uint32_t started_ = 0;
};

class LedTest {
 public:
  static constexpr uint32_t DURATION_MS = 5000;
  static constexpr uint32_t CHASE_MS = 4200;
  void start(uint32_t now) { started_ = now; active_ = true; }
  void stop() { active_ = false; }
  bool active() const { return active_; }
  bool update(uint32_t now) {
    if (!active_ || static_cast<uint32_t>(now - started_) < DURATION_MS) return false;
    active_ = false;
    return true;
  }
  uint32_t elapsed(uint32_t now) const { return static_cast<uint32_t>(now - started_); }
  uint16_t frame(uint32_t now, uint16_t pixels) const {
    const uint32_t progress = elapsed(now);
    const uint32_t frames = static_cast<uint32_t>(pixels) * 3;
    return progress < CHASE_MS ? progress * frames / CHASE_MS : frames;
  }
 private:
  uint32_t started_ = 0;
  bool active_ = false;
};

class Button {
 public:
  void begin(bool pressed, uint32_t now) {
    raw_ = stable_ = pressed;
    changed_ = now;
    armed_ = !pressed;  // a button held at startup must be released first
  }
  bool update(bool pressed, uint32_t now) {
    if (pressed != raw_) { raw_ = pressed; changed_ = now; }
    if (raw_ == stable_ || static_cast<uint32_t>(now - changed_) < 35) return false;
    stable_ = raw_;
    if (!stable_) { armed_ = true; return false; }
    if (!armed_) return false;
    armed_ = false;
    return true;
  }
 private:
  bool raw_ = false, stable_ = false, armed_ = true;
  uint32_t changed_ = 0;
};
}
