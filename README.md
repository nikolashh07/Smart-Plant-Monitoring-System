# Plant

Automatic watering controller for a small radish bed (8x12 in, 5 in deep), running on an Arduino Uno.

It reads a capacitive soil-moisture sensor, pulls the local weather forecast over an ESP-01 WiFi module, and runs a pump in short pulses with soak time between them. It holds off for rain, locks the pump out in a freeze, and caps daily water use. Status shows on an SSD1306 OLED.

## Hardware
- Arduino Uno
- ESP-01 (WiFi, 9600 baud) with a 3.3 V level divider on RX
- Capacitive soil-moisture sensor
- SSD1306 128x64 I2C OLED
- Pump on D4 via a logic-level MOSFET or relay, with a flyback diode

## Setup
1. Install [PlatformIO](https://platformio.org/).
2. Set `WIFI_SSID` and `WIFI_PASS` in `src/main.cpp`. Do not commit real credentials.
3. Calibrate the soil sensor (`CAL_MODE 1`) and measure the pump rate (`PUMP_ML_PER_SEC`).
4. Build and upload: `pio run -t upload`

`tools/espcheck.cpp` is an ESP-01 diagnostic: `pio run -e espcheck -t upload`, then open the serial monitor at 9600 baud.
