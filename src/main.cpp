#include <Arduino.h>
#include "config.h"
#include "oled.h"
#include "esp.h"
#include "weather.h"
#include "watering.h"
#include "screens.h"

// =====================================================================
void setup() {
  pinMode(PUMP_PIN, OUTPUT);
  pumpOff();

  // Start with the fallback location from the settings at the top.
  strncpy(locationName, FALLBACK_LOCATION_NAME, sizeof(locationName) - 1);
  strncpy(locationLat, FALLBACK_LATITUDE, sizeof(locationLat) - 1);
  strncpy(locationLon, FALLBACK_LONGITUDE, sizeof(locationLon) - 1);

  Serial.begin(9600);
  Serial.println(F("Booting..."));
  esp.begin(ESP_BAUD);

  oledInit();
  Serial.println(F("OLED init done"));
  oledClear();
  oledPrintLine(F("Connecting WiFi..."), 3);
  Serial.println(F("Starting. Joining WiFi (up to ~20 s)..."));

  if (wifiConnect()) {
    oledPrintLine(F("WiFi connected"), 3);
    Serial.println(F("WiFi connected"));
  } else {
    oledPrintLine(F("WiFi failed (retrying)"), 3);
    Serial.println(F("WiFi FAILED (no reply from ESP-01 or wrong SSID/password)"));
  }
}

void loop() {
  static unsigned long lastWeather = 0, lastSoil = 0;
  static bool firstFetch = true;
  static uint8_t lastScreen = 255;
  unsigned long now = millis();

  // Midnight (local time) reset of the pump's daily tracking. Until the first
  // weather fetch gives us the time, fall back to 24 h after boot.
  static uint16_t prevMinOfDay = 0;
  if (clockValid) {
    uint16_t m = localMinOfDay(now);
    if (prevMinOfDay > 1200 && m < 240) midnightReset(now);   // clock wrapped past 00:00
    prevMinOfDay = m;
  } else if (now - dayStartMs >= 86400000UL) {
    midnightReset(now);
  }

  // Never block on WiFi while the pump is running.
  if (pumpState != PUMP_RUNNING &&
      (firstFetch || now - lastWeather >= WEATHER_REFRESH_MS)) {
    firstFetch = false;
    lastWeather = now;
    oledPrintLine(F("Updating..."), 7);
#if AUTO_LOCATION
    if (!locationKnown) fetchLocation();   // retried each refresh until it works
#endif
    Serial.println(F("Fetching weather (can take up to ~20 s)..."));
    bool ok = fetchWeather();
    if (!ok) {
      Serial.println(F("Fetch failed. Rejoining WiFi..."));
      if (wifiConnect()) ok = fetchWeather();
      if (!ok) lastWeather = now - WEATHER_REFRESH_MS + 30000UL; // retry in 30s
    }
    Serial.println(ok ? F("weather ok") : F("weather FAILED, retrying in ~30 s"));
    lastSoil = 0;
    lastScreen = 255;
  }

  bool soilTick = (lastSoil == 0 || now - lastSoil >= SOIL_REFRESH_MS);
  if (soilTick) {
    lastSoil = now ? now : 1;
    soilRaw = readSoilRaw();
    soilPct = rawToPercent(soilRaw);
  }

  rainMode = rainSeen && (now - lastRainMs < RAIN_LOCK_MS);

  waterLogic(now);

  if (soilTick) printSerialStatus();

  uint8_t screen = (now / SCREEN_SWITCH_MS) % SCREEN_COUNT;
  if (soilTick || screen != lastScreen) {
    drawScreen(screen, screen != lastScreen);
    lastScreen = screen;
  }
}


