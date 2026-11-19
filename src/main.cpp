// Radish bed watering controller: wires the modules together.
#include <Arduino.h>
#include "config.h"
#include "oled.h"
#include "rules.h"
#include "esp.h"
#include "weather.h"
#include "watering.h"
#include "screens.h"

// Loop timers. 0 / 255 mean "do it on the next pass".
static unsigned long lastWeatherMs = 0, lastSoilMs = 0;
static bool firstFetch = true;
static uint8_t lastScreen = 255;

// Start with the fallback location from config.h.
static void loadFallbackLocation() {
  strncpy(location.name, FALLBACK_LOCATION_NAME, sizeof(location.name) - 1);
  strncpy(location.lat, FALLBACK_LATITUDE, sizeof(location.lat) - 1);
  strncpy(location.lon, FALLBACK_LONGITUDE, sizeof(location.lon) - 1);
}

static void joinWifiAtBoot() {
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

void setup() {
  pinMode(PUMP_PIN, OUTPUT);
  pumpOff();
  loadFallbackLocation();

  Serial.begin(9600);
  Serial.println(F("Booting..."));
  esp.begin(ESP_BAUD);

  oledInit();
  Serial.println(F("OLED init done"));
  oledClear();
  joinWifiAtBoot();
}

// Midnight (local time) reset of the pump's daily tracking. Until the first
// weather fetch gives us the time, fall back to 24 h after boot.
static void checkMidnight(unsigned long now) {
  static uint16_t prevMinOfDay = 0;
  if (localClock.valid) {
    uint16_t m = localMinOfDay(now);
    if (prevMinOfDay > 1200 && m < 240) midnightReset(now);   // clock wrapped past 00:00
    prevMinOfDay = m;
  } else if (now - watering.dayStartMs >= 86400000UL) {
    midnightReset(now);
  }
}

// Refresh location (if still unknown) and weather. Takes up to ~20 s, so it
// never runs while the pump is on.
static void refreshWeatherIfDue(unsigned long now) {
  if (watering.pump == PUMP_RUNNING) return;
  if (!firstFetch && now - lastWeatherMs < WEATHER_REFRESH_MS) return;

  firstFetch = false;
  lastWeatherMs = now;
  oledPrintLine(F("Updating..."), 7);
#if AUTO_LOCATION
  if (!location.known) fetchLocation();   // retried each refresh until it works
#endif
  Serial.println(F("Fetching weather (can take up to ~20 s)..."));
  bool ok = fetchWeather();
  if (!ok) {
    Serial.println(F("Fetch failed. Rejoining WiFi..."));
    if (wifiConnect()) ok = fetchWeather();
    if (!ok) lastWeatherMs = now - WEATHER_REFRESH_MS + 30000UL; // retry in 30s
  }
  Serial.println(ok ? F("weather ok") : F("weather FAILED, retrying in ~30 s"));
  lastSoilMs = 0;     // re-read the soil and redraw right away
  lastScreen = 255;
}

// Read the soil sensor if it is time. Returns true when a new reading was taken.
static bool readSoilIfDue(unsigned long now) {
  if (lastSoilMs != 0 && now - lastSoilMs < SOIL_REFRESH_MS) return false;
  lastSoilMs = now ? now : 1;
  soil.raw = readSoilRaw();
  soil.pct = soilPercent(soil.raw);
  return true;
}

static void updateDisplay(unsigned long now, bool soilTick) {
  uint8_t screen = (now / SCREEN_SWITCH_MS) % SCREEN_COUNT;
  if (!soilTick && screen == lastScreen) return;
  drawScreen(screen, screen != lastScreen);
  lastScreen = screen;
}

void loop() {
  unsigned long now = millis();

  checkMidnight(now);
  refreshWeatherIfDue(now);
  bool soilTick = readSoilIfDue(now);

  rain.mode = rain.seen && (now - rain.lastMs < RAIN_LOCK_MS);
  waterLogic(now);

  if (soilTick) printSerialStatus();
  updateDisplay(now, soilTick);
}
