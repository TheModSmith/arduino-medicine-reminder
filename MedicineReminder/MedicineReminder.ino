/*
  Standalone medicine reminder for LOLIN D1 mini v4 (ESP8266).
  Edit Config.h. Optional Home Assistant integration uses MQTT discovery.
  Open the accompanying README.md for installation, wiring, and behaviour.
*/
#include <ESP8266WiFi.h>
#include <coredecls.h>
#include <time.h>
#include <Adafruit_NeoPixel.h>
#include <AsyncMqttClient.h>
#include <ArduinoJson.h>
#include "Config.h"
#include "ReminderCore.h"
#include "Storage.h"
#include "Discovery.h"

static_assert(Config::DEFAULT_REMINDER_MINUTE < 1440, "Invalid default reminder time");
static_assert(Config::LED_COUNT > 0 && Config::BRIGHTNESS > 0, "LEDs must be visible");
static_assert(Config::DEFAULT_REMINDER_COLOUR > 0 && Config::DEFAULT_REMINDER_COLOUR <= 0xFFFFFF,
              "Reminder colour must be a non-black RGB value");
static_assert(Config::DEFAULT_ACK_COLOUR > 0 && Config::DEFAULT_ACK_COLOUR <= 0xFFFFFF,
              "Acknowledgement colour must be a non-black RGB value");

Reminder::Engine engine;
Reminder::Button button;
Reminder::LedTest ledTest;
Storage storage;
Adafruit_NeoPixel pixels(Config::LED_COUNT, Config::LED_PIN, NEO_GRB + NEO_KHZ800);
AsyncMqttClient mqtt;

char deviceId[32], baseTopic[64], availabilityTopic[96];
char stateTopic[96], commandTopic[Discovery::COMMAND_COUNT][96];
bool clockReady = false;
uint32_t localDate = 0;
volatile bool timeWasSet = false;
bool saveNeeded = false, stateDirty = true;
uint32_t saveAt = 0, lastSaveAttempt = 0;
bool saveFailed = false;
bool mqttConnecting = false, mqttJustConnected = false, availabilityNeeded = false;
uint32_t mqttAttemptAt = 0, mqttRetryAt = 0, publishAt = 0, stateSentAt = 0;
uint16_t outstandingPacket = 0;
unsigned discoveryIndex = Discovery::COUNT;
uint32_t discoveryAt = 0;
uint32_t lastWifiRetry = 0;
uint32_t lastStatusPrint = 0;
struct QueuedCommand { uint8_t setting; char value[32]; };
QueuedCommand commandQueue[8];
unsigned commandHead = 0, commandCount = 0;
bool commandOverflow = false;

bool due(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

void requestSave(uint32_t now, bool immediately) {
  saveNeeded = true;
  saveAt = immediately ? now : now + 500;
  stateDirty = true;
}

void handleCommand(uint8_t command, const char* value) {
  if (command >= 3) {
    if (strcmp(value, "PRESS")) {
      Serial.println(F("Rejected MQTT button command."));
      return;
    }
    const uint32_t now = millis();
    if (command == 3) {
      ledTest.start(now);
      Serial.println(F("Home Assistant LED test started (5 seconds)."));
    } else if (command == 4) {
      ledTest.stop();
      if (engine.clearReminder()) {
        requestSave(now, true);
        Serial.println(F("Reminder cleared from Home Assistant."));
      }
    } else if (command == 5) {
      ledTest.stop();
      if (engine.rearmReminder()) requestSave(now, true);
      Serial.println(F("Reminder re-armed; today's scheduled time is eligible again."));
    }
    stateDirty = true;
    return;
  }
  bool changed = false, valid = false;
  if (command == 0) {
    uint16_t minute;
    valid = Reminder::parseTime(value, minute);
    if (valid && minute != engine.settings.minute) {
      engine.settings.minute = minute;
      changed = true;
    }
  } else {
    uint32_t colour = 0;
    valid = Reminder::parseColour(value, colour);
    uint32_t* destination = command == 1 ? &engine.settings.reminderColour : &engine.settings.ackColour;
    if (valid && *destination != colour) { *destination = colour; changed = true; }
  }
  if (changed) requestSave(millis(), false);
  if (!valid) Serial.println(F("Rejected MQTT setting: expected HH:MM or non-black #RRGGBB."));
  stateDirty = true;  // echo the actual value, including after rejected commands
}

void receiveMqtt(char* topic, char* payload, AsyncMqttClientMessageProperties properties,
                 size_t len, size_t index, size_t total) {
  // MQTT can split a payload over multiple callbacks. Never assume NUL termination.
  static char message[32];
  static char messageTopic[128];
  static size_t received = 0;
  if (!index) {
    received = 0;
    messageTopic[0] = '\0';
    if (total >= sizeof(message) || strlen(topic) >= sizeof(messageTopic)) return;
    strcpy(messageTopic, topic);
  }
  if (total >= sizeof(message) || index > total || index != received || len > total - index ||
      strcmp(topic, messageTopic)) return;
  memcpy(message + index, payload, len);
  received += len;
  if (received != total) return;
  message[total] = '\0';
  if (strlen(message) != total) return;  // reject embedded NULs
  if (!strcmp(topic, Config::HA_BIRTH_TOPIC)) {
    if (!strcmp(message, "online")) {
      discoveryIndex = 0;
      discoveryAt = millis() + 1000 + ESP.getChipId() % 500;
      stateDirty = true;
    }
    return;
  }
  // Retained old commands must not undo saved settings on each reconnect.
  if (!properties.retain) {
    for (unsigned i = 0; i < Discovery::COMMAND_COUNT; ++i) {
      if (strcmp(topic, commandTopic[i])) continue;
      if (commandCount == 8) { commandOverflow = true; return; }
      QueuedCommand& queued = commandQueue[(commandHead + commandCount) % 8];
      queued.setting = i;
      strcpy(queued.value, message);
      ++commandCount;
      break;
    }
  }
}

void setupMqtt() {
  snprintf(deviceId, sizeof(deviceId), "medicine_%06lx", static_cast<unsigned long>(ESP.getChipId()));
  snprintf(baseTopic, sizeof(baseTopic), "medicine_reminder/%s", deviceId);
  snprintf(availabilityTopic, sizeof(availabilityTopic), "%s/availability", baseTopic);
  snprintf(stateTopic, sizeof(stateTopic), "%s/state", baseTopic);
  for (unsigned i = 0; i < Discovery::COMMAND_COUNT; ++i) {
    snprintf(commandTopic[i], sizeof(commandTopic[i]), "%s/%s/set", baseTopic, Discovery::commandKey(i));
  }
  if (!Config::MQTT_HOST[0]) return;
  mqtt.setServer(Config::MQTT_HOST, Config::MQTT_PORT);
  mqtt.setClientId(deviceId);
  mqtt.setKeepAlive(30);
  mqtt.setCleanSession(true);
  mqtt.setMaxTopicLength(127);
  if (Config::MQTT_USERNAME[0]) mqtt.setCredentials(Config::MQTT_USERNAME, Config::MQTT_PASSWORD);
  mqtt.setWill(availabilityTopic, 1, true, "offline");
  mqtt.onConnect([](bool) {
    mqttConnecting = false;
    mqttJustConnected = true;
  });
  mqtt.onDisconnect([](AsyncMqttClientDisconnectReason reason) {
    mqttConnecting = false;
    mqttJustConnected = false;
    outstandingPacket = 0;
    mqttRetryAt = millis() + 30000;
    Serial.printf("MQTT disconnected (%u); standalone reminder continues.\n", static_cast<unsigned>(reason));
  });
  mqtt.onPublish([](uint16_t packetId) {
    if (packetId == outstandingPacket) outstandingPacket = 0;
  });
  mqtt.onMessage(receiveMqtt);
}

bool publishText(const char* topic, const char* payload, uint32_t now) {
  outstandingPacket = mqtt.publish(topic, 1, true, payload);
  if (!outstandingPacket) return false;
  publishAt = now;
  return true;
}

bool publishState(uint32_t now) {
  JsonDocument doc;
  char timeText[6], reminderColour[8], ackColour[8];
  Reminder::formatTime(engine.settings.minute, timeText);
  Reminder::formatColour(engine.settings.reminderColour, reminderColour);
  Reminder::formatColour(engine.settings.ackColour, ackColour);
  doc["reminder_time"] = timeText;
  doc["reminder_colour"] = reminderColour;
  doc["ack_colour"] = ackColour;
  doc["reminder_active"] = engine.history.pending ? "ON" : "OFF";
  doc["clock_ready"] = clockReady ? "ON" : "OFF";
  doc["storage_ok"] = storage.healthy() && !saveNeeded ? "ON" : "OFF";
  doc["state"] = ledTest.active() ? "Checking all LEDs" : engine.displayState(clockReady, localDate);
  char lastDate[11];
  if (engine.history.lastTriggeredDate) {
    const uint32_t date = engine.history.lastTriggeredDate;
    snprintf(lastDate, sizeof(lastDate), "%04lu-%02lu-%02lu",
             static_cast<unsigned long>(date / 10000),
             static_cast<unsigned long>((date / 100) % 100),
             static_cast<unsigned long>(date % 100));
  } else {
    strcpy(lastDate, "never");
  }
  doc["last_triggered_date"] = lastDate;
  char payload[512];
  if (measureJson(doc) >= sizeof(payload)) return false;
  serializeJson(doc, payload, sizeof(payload));
  if (!publishText(stateTopic, payload, now)) return false;
  stateDirty = false;
  stateSentAt = now;
  return true;
}

void serviceMqtt(uint32_t now) {
  if (!Config::MQTT_HOST[0]) return;
  if (WiFi.status() != WL_CONNECTED) {
    if (mqtt.connected() || mqttConnecting) mqtt.disconnect(true);
    return;
  }
  if (!mqtt.connected()) {
    if (mqttConnecting && now - mqttAttemptAt >= 15000) {
      mqtt.disconnect(true);
      mqttConnecting = false;
      mqttRetryAt = now + 30000;
    } else if (!mqttConnecting && due(now, mqttRetryAt)) {
      mqttConnecting = true;
      mqttAttemptAt = now;
      mqtt.connect();  // asynchronous DNS/TCP/MQTT; no wait-for-connect loop
    }
    return;
  }
  if (mqttJustConnected) {
    mqttJustConnected = false;
    for (unsigned i = 0; i < Discovery::COMMAND_COUNT; ++i) mqtt.subscribe(commandTopic[i], 1);
    mqtt.subscribe(Config::HA_BIRTH_TOPIC, 1);
    availabilityNeeded = true;
    discoveryIndex = 0;
    discoveryAt = now;
    stateDirty = true;
    Serial.println(F("MQTT connected; publishing Home Assistant discovery."));
  }
  // Bound the output queue even if a broker stops acknowledging packets.
  if (outstandingPacket) {
    if (now - publishAt >= 15000) mqtt.disconnect(true);
    return;
  }
  if (availabilityNeeded) {
    if (publishText(availabilityTopic, "online", now)) availabilityNeeded = false;
    return;
  }
  if (discoveryIndex < Discovery::COUNT && due(now, discoveryAt)) {
    JsonDocument doc;
    Discovery::build(doc, discoveryIndex, deviceId, baseTopic, Config::DEVICE_NAME, Config::FIRMWARE_VERSION);
    char topic[160], payload[1400];
    snprintf(topic, sizeof(topic), "%s/%s/%s/%s/config", Config::DISCOVERY_PREFIX,
             Discovery::component(discoveryIndex), deviceId, Discovery::KEYS[discoveryIndex]);
    if (measureJson(doc) >= sizeof(payload)) return;
    serializeJson(doc, payload, sizeof(payload));
    if (publishText(topic, payload, now)) {
      ++discoveryIndex;
      discoveryAt = now + 100;
    }
    return;
  }
  if ((stateDirty && now - stateSentAt >= 200) || now - stateSentAt >= 30000) publishState(now);
}

void renderLeds(uint32_t now) {
  static bool wasTest = false;
  static uint16_t previousTestFrame = UINT16_MAX;
  if (ledTest.active()) {
    // Check every pixel in red, green and blue, then show all pixels together.
    const uint16_t frame = ledTest.frame(now, Config::LED_COUNT);
    if (!wasTest || frame != previousTestFrame) {
      pixels.clear();
      if (frame < Config::LED_COUNT * 3) {
        const uint32_t colour = frame % 3 == 0 ? pixels.Color(255, 0, 0) :
                                frame % 3 == 1 ? pixels.Color(0, 255, 0) :
                                                 pixels.Color(0, 0, 255);
        pixels.setPixelColor(frame / 3, colour);
      } else {
        pixels.fill(pixels.Color(255, 255, 255));
      }
      pixels.show();
      previousTestFrame = frame;
    }
    wasTest = true;
    return;
  }
  uint32_t colour = 0;
  switch (engine.mode()) {
    case Reminder::Mode::Reminder: colour = engine.settings.reminderColour; break;
    case Reminder::Mode::Acknowledging: colour = engine.settings.ackColour; break;
    case Reminder::Mode::Preview: colour = Config::PREVIEW_COLOUR; break;
    default: break;
  }
  const uint16_t level = Adafruit_NeoPixel::gamma8(engine.level(now));
  const uint32_t output = pixels.Color(((colour >> 16) & 255) * level / 255,
                                       ((colour >> 8) & 255) * level / 255,
                                       (colour & 255) * level / 255);
  static uint32_t previous = 0xFFFFFFFF;
  if (wasTest || output != previous) {
    pixels.fill(output);
    pixels.show();
    previous = output;
  }
  wasTest = false;
}

void setup() {
  Serial.begin(115200);
  Serial.printf("\nMedicine Reminder %s\n", Config::FIRMWARE_VERSION);
  pinMode(Config::BUTTON_PIN, INPUT_PULLUP);
  Serial.printf("Button GPIO14 at boot: %s (released should be HIGH).\n",
                digitalRead(Config::BUTTON_PIN) == LOW ? "LOW" : "HIGH");
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  button.begin(digitalRead(Config::BUTTON_PIN) == LOW, millis());
  pixels.begin();
  pixels.setBrightness(Config::BRIGHTNESS);
  pixels.clear();
  pixels.show();

  engine.settings.minute = Config::DEFAULT_REMINDER_MINUTE;
  engine.settings.reminderColour = Config::DEFAULT_REMINDER_COLOUR;
  engine.settings.ackColour = Config::DEFAULT_ACK_COLOUR;
  if (!storage.begin(engine, Config::RESET_SAVED_SETTINGS)) {
    Serial.println(F("Storage fault: settings/history may not survive restart. Check before use."));
  }
  renderLeds(millis());  // restore a pending reminder without waiting for the network

  setupMqtt();
  settimeofday_cb([]() { timeWasSet = true; });
  configTime(REMINDER_TIMEZONE, Config::NTP_SERVER_1, Config::NTP_SERVER_2, Config::NTP_SERVER_3);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(deviceId);
  WiFi.setAutoReconnect(true);
  WiFi.begin(Config::WIFI_SSID, Config::WIFI_PASSWORD);
  Serial.println(F("Connecting to Wi-Fi; waiting for network time. No HA connection is required."));
}

void loop() {
  const uint32_t now = millis();
  // Apply commands in the main loop, never while a flash write yields to Wi-Fi.
  while (commandCount) {
    const QueuedCommand queued = commandQueue[commandHead];
    commandHead = (commandHead + 1) % 8;
    --commandCount;
    handleCommand(queued.setting, queued.value);
  }
  if (commandOverflow) {
    commandOverflow = false;
    stateDirty = true;
    Serial.println(F("MQTT command burst exceeded queue; resend the last setting."));
  }
  if (timeWasSet) {
    timeWasSet = false;
    const time_t utc = time(nullptr);
    struct tm local;
    localtime_r(&utc, &local);
    const uint32_t date = (local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday;
    clockReady = Reminder::validDate(date);
    stateDirty = true;
    Serial.printf("Network time received: %04d-%02d-%02d %02d:%02d:%02d local.\n",
                  local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                  local.tm_hour, local.tm_min, local.tm_sec);
  }
  uint32_t date = 0;
  uint16_t minute = 0;
  if (clockReady) {
    const time_t utc = time(nullptr);
    struct tm local;
    localtime_r(&utc, &local);
    date = (local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday;
    minute = local.tm_hour * 60 + local.tm_min;
  }
  localDate = date;
  const Reminder::Mode previousMode = engine.mode();
  if (engine.tick(now, clockReady, date, minute)) {
    ledTest.stop();  // a newly due reminder takes precedence over a test
    Serial.println(F("Daily reminder due."));
    requestSave(now, true);
  }
  if (button.update(digitalRead(Config::BUTTON_PIN) == LOW, now)) {
    Serial.println(F("Physical button press detected on GPIO14."));
    if (ledTest.active()) { ledTest.stop(); stateDirty = true; }
    if (engine.press(now)) {
      Serial.println(F("Reminder acknowledged."));
      requestSave(now, true);
    }
  }
  if (ledTest.update(now)) stateDirty = true;
  if (engine.mode() != previousMode) stateDirty = true;
  renderLeds(now);

  if (saveNeeded && due(now, saveAt) && (!saveFailed || now - lastSaveAttempt >= 30000)) {
    lastSaveAttempt = now;
    saveFailed = !storage.save(engine);
    saveNeeded = saveFailed;
    stateDirty = true;
    if (saveFailed) Serial.println(F("Could not save settings/history; retrying in 30 seconds."));
  }
  // The D1's small onboard LED indicates time/storage status, not medication events.
  digitalWrite(LED_BUILTIN, !storage.healthy() ? LOW : !clockReady && now % 1000 < 500 ? LOW : HIGH);
  if (WiFi.status() != WL_CONNECTED && now - lastWifiRetry >= 30000) {
    lastWifiRetry = now;
    WiFi.reconnect();
  }
  serviceMqtt(now);
  if (now - lastStatusPrint >= 10000) {
    lastStatusPrint = now;
    Serial.printf("Status: Wi-Fi %s, time %s, MQTT %s, state %s, last trigger %lu\n",
                  WiFi.status() == WL_CONNECTED ? "connected" : "disconnected",
                  clockReady ? "ready" : "waiting",
                  !Config::MQTT_HOST[0] ? "disabled" : mqtt.connected() ? "connected" : "disconnected",
                  engine.state(clockReady),
                  static_cast<unsigned long>(engine.history.lastTriggeredDate));
  }
  delay(1);  // yield to ESP8266 Wi-Fi; animations never use blocking delays
}
