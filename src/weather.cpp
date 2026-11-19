// Weather and location client: ESP-01 AT commands, Open-Meteo forecast, ip-api.com lookup.
#include "config.h"
#include "esp.h"
#include "weather.h"

Weather weather;

// Rain mode: while it's raining (and for RAIN_LOCK_MS afterwards) the pump
// is locked out and soil readings are ignored; drainage takes over.
bool rainSeen = false, rainMode = false;
unsigned long lastRainMs = 0;

// Local clock, taken from the weather reply (timezone=auto) and kept by millis().
bool clockValid = false;
uint16_t clockSyncMinOfDay = 0;      // minutes since local midnight at the last sync
unsigned long clockSyncMs = 0;       // millis() at the last sync

uint16_t localMinOfDay(unsigned long now) {
  return (clockSyncMinOfDay + (now - clockSyncMs) / 60000UL) % 1440;
}

// Where we are. Starts as the fallback values in config.h; replaced by the
// IP-based lookup when AUTO_LOCATION is on and it succeeds.
char locationName[22];
char locationLat[10];
char locationLon[11];
bool locationKnown = false;

// Weather request, in flash pieces (too much RAM for one buffer on an Uno).
// The latitude/longitude are sent between the pieces. timezone=auto makes
// Open-Meteo return the local time for wherever those coordinates are.
static const char WEATHER_REQ_A[] PROGMEM = "GET /v1/forecast?latitude=";
static const char WEATHER_REQ_B[] PROGMEM = "&longitude=";
static const char WEATHER_REQ_C[] PROGMEM =
  "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,precipitation"
  "&hourly=temperature_2m,precipitation_probability,weather_code"
  "&daily=precipitation_probability_max,precipitation_sum,"
  "et0_fao_evapotranspiration,temperature_2m_min,temperature_2m_max"
  "&forecast_days=1&forecast_hours=4&timezone=auto"
  " HTTP/1.0\r\nHost: api.open-meteo.com\r\nConnection: close\r\n\r\n";

// Free IP geolocation (plain HTTP). Only city, region and coordinates are asked for.
static const char LOCATION_REQ[] PROGMEM =
  "GET /json/?fields=status,regionName,city,lat,lon HTTP/1.0\r\n"
  "Host: ip-api.com\r\nConnection: close\r\n\r\n";

static bool isRainCode(int c) {
  return (c >= 51 && c <= 67) || (c >= 80 && c <= 82) || c >= 95;
}

const __FlashStringHelper* weatherText(int c) {
  if (c == 0)  return F("Clear");
  if (c == 1)  return F("Mostly clear");
  if (c == 2)  return F("Partly cloudy");
  if (c == 3)  return F("Overcast");
  if (c <= 48) return F("Fog");
  if (c <= 57) return F("Drizzle");
  if (c <= 67) return F("Rain");
  if (c <= 77) return F("Snow");
  if (c <= 82) return F("Rain showers");
  if (c <= 86) return F("Snow showers");
  return F("Thunderstorm");
}

// Which stage the last fetch got to (printed on failure to aid debugging).
static uint8_t fetchStep = 0;
static char fetchDbg[120];   // first bytes the ESP-01 sent back (for debugging)

// Steps 1-3: open the TCP connection and send the forecast request.
static bool sendWeatherRequest() {
  fetchStep = 1;   // AT+CIPMUX
  if (!espCmd(F("AT+CIPMUX=0"), F("OK"), 2000)) return false;
  fetchStep = 2;   // open TCP connection (fails on DNS / no internet)
  espFlush();
  esp.println(F("AT+CIPSTART=\"TCP\",\"api.open-meteo.com\",80"));
  if (!espWaitFor(F("OK"), 8000)) return false;

  fetchStep = 3;   // ask to send, wait for '>'
  uint16_t len = strlen_P(WEATHER_REQ_A) + strlen(locationLat) + strlen_P(WEATHER_REQ_B) +
                 strlen(locationLon) + strlen_P(WEATHER_REQ_C);
  espFlush();
  esp.print(F("AT+CIPSEND="));
  esp.println(len);
  if (!espWaitFor(F(">"), 3000)) return false;
  espWriteP(WEATHER_REQ_A);
  esp.print(locationLat);
  espWriteP(WEATHER_REQ_B);
  esp.print(locationLon);
  espWriteP(WEATHER_REQ_C);
  return true;
}

// Step 4: skip ahead to "current":{ while keeping the first bytes received,
// so a failure can show what the ESP-01 actually sent.
static bool waitForJsonStart() {
  fetchStep = 4;
  static const char KEY[] PROGMEM = "\"current\":{";
  uint8_t dbgLen = 0, matched = 0;
  bool gotKey = false;
  unsigned long t0 = millis();
  while (millis() - t0 < 10000UL && !gotKey) {
    if (!esp.available()) continue;
    char c = esp.read();
    if (dbgLen < sizeof(fetchDbg) - 1) fetchDbg[dbgLen++] = (c >= 32 && c < 127) ? c : '.';
    if (c == (char)pgm_read_byte(&KEY[matched])) {
      if (++matched == sizeof(KEY) - 1) gotKey = true;
    } else {
      matched = (c == '"') ? 1 : 0;
    }
  }
  fetchDbg[dbgLen] = 0;
  return gotKey;
}

// "current" block: local time (hh:mm) and the conditions right now.
static bool parseCurrent(Weather& w, int& hh, int& mm) {
  // "time":"2026-11-04T16:00" (local time) -> hour and minute
  if (!esp.find((char*)"\"time\":\"")) return false;
  if (!esp.find((char*)"T"))           return false;
  hh = esp.parseInt();
  mm = esp.parseInt();
  if (!esp.find((char*)"\"temperature_2m\":"))       return false;
  w.tempC = esp.parseFloat();
  if (!esp.find((char*)"\"relative_humidity_2m\":")) return false;
  w.humidity = esp.parseInt();
  if (!esp.find((char*)"\"weather_code\":"))         return false;
  w.code = esp.parseInt();
  if (!esp.find((char*)"\"wind_speed_10m\":"))       return false;
  w.windKmh = esp.parseFloat();
  if (!esp.find((char*)"\"precipitation\":"))        return false;
  w.precipNow = esp.parseFloat();
  return true;
}

// "hourly" block: this hour and the next 3.
static bool parseHourly(Weather& w) {
  // "hourly":{ does not match "hourly_units":{ .
  if (!esp.find((char*)"\"hourly\":{")) return false;
  if (!esp.find((char*)"\"time\":["))   return false;
  for (uint8_t i = 0; i < 4; i++) {          // "2026-11-04T20:00" -> 20
    if (!esp.find((char*)"T")) return false;
    w.hourly[i].hour = esp.parseInt();
  }
  if (!esp.find((char*)"\"temperature_2m\":["))           return false;
  for (uint8_t i = 0; i < 4; i++) w.hourly[i].tempC = (int8_t)lroundf(esp.parseFloat());
  if (!esp.find((char*)"\"precipitation_probability\":[")) return false;
  for (uint8_t i = 0; i < 4; i++) w.hourly[i].rainProb = esp.parseInt();
  if (!esp.find((char*)"\"weather_code\":["))             return false;
  for (uint8_t i = 0; i < 4; i++) w.hourly[i].code = esp.parseInt();
  return true;
}

// "daily" block: today's rain, evaporation and temperature range.
static bool parseDaily(Weather& w) {
  // "daily":{ does not match "daily_units":{ so we land in the right object.
  if (!esp.find((char*)"\"daily\":{")) return false;
  if (!esp.find((char*)"\"precipitation_probability_max\":[")) return false;
  w.rainProb = esp.parseInt();
  if (!esp.find((char*)"\"precipitation_sum\":["))             return false;
  w.rainMm = esp.parseFloat();
  if (!esp.find((char*)"\"et0_fao_evapotranspiration\":["))    return false;
  w.et0 = esp.parseFloat();
  if (!esp.find((char*)"\"temperature_2m_min\":["))            return false;
  w.minC = esp.parseFloat();
  if (!esp.find((char*)"\"temperature_2m_max\":["))            return false;
  w.maxC = esp.parseFloat();
  return true;
}

// Store a good reading and update the clock and rain-mode trackers.
static void applyWeather(const Weather& w, int hh, int mm) {
  weather = w;

  if (hh >= 0 && hh < 24 && mm >= 0 && mm < 60) {
    clockSyncMinOfDay = hh * 60 + mm;
    clockSyncMs = millis();
    clockValid = true;
  }

  if (w.precipNow > 0.0 || isRainCode(w.code)) {
    rainSeen = true;
    lastRainMs = millis();
  }
}

static bool fetchWeatherInner() {
  if (!sendWeatherRequest()) return false;
  if (!waitForJsonStart())   return false;

  esp.setTimeout(8000);
  Weather w;
  int hh, mm;
  fetchStep = 5;   // parsing the fields
  if (!parseCurrent(w, hh, mm)) return false;
  if (!parseHourly(w))          return false;
  if (!parseDaily(w))           return false;

  w.valid = true;
  applyWeather(w, hh, mm);
  return true;
}

// Read characters up to the closing quote of a JSON string.
static void readUntilQuote(char* out, uint8_t n) {
  uint8_t i = 0;
  unsigned long t = millis();
  while (millis() - t < 3000UL) {
    if (!esp.available()) continue;
    char c = esp.read();
    if (c == '"') break;
    if (i < n - 1) out[i++] = c;
  }
  out[i] = 0;
}

// Ask ip-api.com where this internet connection is. Sets locationName, locationLat, locationLon.
static bool fetchLocationInner() {
  if (!espCmd(F("AT+CIPMUX=0"), F("OK"), 2000)) return false;
  espFlush();
  esp.println(F("AT+CIPSTART=\"TCP\",\"ip-api.com\",80"));
  if (!espWaitFor(F("OK"), 8000)) return false;

  espFlush();
  esp.print(F("AT+CIPSEND="));
  esp.println((uint16_t)strlen_P(LOCATION_REQ));
  if (!espWaitFor(F(">"), 3000)) return false;
  espWriteP(LOCATION_REQ);

  esp.setTimeout(8000);
  char region[16], city[16];
  if (!esp.find((char*)"\"regionName\":\"")) return false;   // missing if status != success
  readUntilQuote(region, sizeof(region));
  if (!esp.find((char*)"\"city\":\""))       return false;
  readUntilQuote(city, sizeof(city));
  if (!esp.find((char*)"\"lat\":"))          return false;
  float lat = esp.parseFloat();
  if (!esp.find((char*)"\"lon\":"))          return false;
  float lon = esp.parseFloat();
  if (lat == 0 && lon == 0) return false;

  dtostrf(lat, 1, 3, locationLat);
  dtostrf(lon, 1, 3, locationLon);
  snprintf_P(locationName, sizeof(locationName), PSTR("%s, %s"), city, region);
  return true;
}

bool fetchLocation() {
  bool ok = fetchLocationInner();
  esp.println(F("AT+CIPCLOSE"));
  delay(300);
  espFlush();
  if (ok) {
    locationKnown = true;
    Serial.print(F("Location: ")); Serial.print(locationName);
    Serial.print(F(" (")); Serial.print(locationLat); Serial.print(F(", "));
    Serial.print(locationLon); Serial.println(')');
  } else {
    Serial.print(F("Location lookup failed, using ")); Serial.println(locationName);
  }
  return ok;
}

// Say (on the Serial Monitor) which stage the last weather fetch got to.
static void printFetchFailure() {
  Serial.print(F("  fetch failed at step "));
  Serial.print(fetchStep);
  switch (fetchStep) {
    case 1: Serial.println(F(" (ESP-01 not answering)")); break;
    case 2: Serial.println(F(" (can't reach api.open-meteo.com: no internet / DNS?)")); break;
    case 3: Serial.println(F(" (connected but couldn't send request)")); break;
    case 4:
      Serial.println(F(" (no/odd reply from server)"));
      Serial.print(F("  ESP-01 sent: \""));
      Serial.print(fetchDbg);
      Serial.println('"');
      break;
    default: Serial.println(F(" (reply didn't match expected format)")); break;
  }
}

// Always closes the TCP connection afterwards. Without this, a failed fetch
// left the connection open and every retry died with "ALREADY CONNECTED".
bool fetchWeather() {
  bool ok = fetchWeatherInner();
  if (!ok) printFetchFailure();
  esp.println(F("AT+CIPCLOSE"));
  delay(300);
  espFlush();
  return ok;
}
