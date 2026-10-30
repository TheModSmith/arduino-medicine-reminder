# LOLIN Arduino Medicine Reminder

A small, 3D-printed medicine reminder built around a **LOLIN D1 mini v4 (ESP8266)** and programmed with the **Arduino IDE**. A ring of 14 LEDs lights up at the chosen daily time. Press the physical button to acknowledge the reminder with a short green animation.

It works on its own after connecting to Wi-Fi for time sync. **Home Assistant is optional**: MQTT discovery adds a daily time setting, colour settings, status, and buttons to check the LEDs or dismiss a reminder.

**STL downloads:** (https://makerworld.com/en/models/3519608-smart-medicine-reminder-arduino-home-assistant#profileId-4022794)

## Quick start

1. Print the five parts and assemble the electronics.
2. Install **esp8266 by ESP8266 Community (3.1.2)** in Arduino IDE's Boards Manager. In Library Manager, install:
   - **Adafruit NeoPixel** 1.15.5
   - **ArduinoJson** 7.4.2
   - **AsyncMqttClient by Marvin Roger** 0.9.0
   - **ESPAsyncTCP by dvarrel** 1.2.4
3. In `Firmware/MedicineReminder/`, copy `Config.example.h` to `Config.h`. Add your **2.4 GHz Wi-Fi** name and password. Leave `MQTT_HOST` empty if you do not use Home Assistant.
4. Open `MedicineReminder.ino` in Arduino IDE. Select **LOLIN(WEMOS) D1 R2 & mini** and upload through the D1 mini's own USB-C socket.
5. At 21:00 local time by default, the LEDs turn red. Press the button to acknowledge; they flash green for three seconds and turn off until the next day's reminder.

## Parts used

- 1 × **LOLIN D1 mini v4.0.0** (ESP8266, USB-C)
- 1 × **SparkFun BOB-15100** USB-C breakout for power input
- **14 × WS2812B LEDs** cut from a 10 mm-wide, 60 LEDs/m strip
- 1 × **NZN Electronics SW-TACT-12X12-4P** tactile button
- **6 × M2.5 × 8 mm, 90° countersunk screws** (four for the case, two for the USB-C board)
- **2 × M2 × 8 mm, 90° countersunk screws** for the D1 mini
- **White PLA** for the base, lid, button cover and cap; **orange PLA** for the bottle
  
## Essential wiring

| Connection | D1 mini pad |
|---|---|
| LED strip data input (`DIN`) | **4 / SDA** (GPIO4, Arduino D2) |
| Button, one contact | **14 / SCK** (GPIO14, Arduino D5) |
| Button, other contact | **GND** |
| LED strip and D1 mini power | **5 V** from the USB-C breakout, with a common GND |

## Change the reminder time

**In Arduino IDE:** Open the `Config.h` tab and change `DEFAULT_REMINDER_MINUTE`. The default `21 * 60` means 21:00; for 20:30, use `20 * 60 + 30`. Upload the sketch. This is the **first-run default**: a time previously saved through Home Assistant takes priority. To clear saved settings, set `RESET_SAVED_SETTINGS = true`, upload once, then set it back to `false` and upload again.

**In Home Assistant:** Open the **Medicine Reminder** device and set **Daily reminder time (24h)** under Configuration, using `HH:MM` (for example, `20:30`). The device saves it across restarts. If today's reminder has already happened and you want another one today, set a future time, then press **Allow another reminder today**. Otherwise, the new time starts tomorrow.

Keep your configured `Config.h` private; it contains your Wi-Fi and MQTT credentials and is excluded by `.gitignore`.
