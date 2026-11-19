#include "config.h"
#include "esp.h"
#include "weather.h"

Weather weather;

// Rain mode: while it's raining (and for RAIN_LOCK_MS afterwards) the pump
// is locked out and soil readings are ignored; drainage takes over.
bool rainSeen = false, rainMode = false;
unsigned long lastRainMs = 0;

// Local clock, taken from the weather reply (timezone=auto) and kept by millis().
bool timeValid = false;
uint16_t syncMinOfDay = 0;      // minutes since local midnight at the last sync
unsigned long syncMs = 0;       // millis() at the last sync

uint16_t localMinOfDay(unsigned long now) {
  return (syncMinOfDay + (now - syncMs) / 60000UL) % 1440;
}

// Where we are. Starts as the fallback values above; replaced by the
// IP-based lookup when AUTO_LOCATION is on and it succeeds.
char locName[22];
char locLat[10];
char locLon[11];
bool located = false;

// Weather request, in flash pieces (too much RAM for one buffer on an Uno).
// The latitude/longitude are sent between the pieces. timezone=auto makes
// Open-Meteo return the local time for wherever those coordinates are.
static const char REQ_A[] PROGMEM = "GET /v1/forecast?latitude=";
static const char REQ_B[] PROGMEM = "&longitude=";
static const char REQ_C[] PROGMEM =
  "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,precipitation"
  "&hourly=temperature_2m,precipitation_probability,weather_code"
  "&daily=precipitation_probability_max,precipitation_sum,"
  "et0_fao_evapotranspiration,temperature_2m_min,temperature_2m_max"
  "&forecast_days=1&forecast_hours=4&timezone=auto"
  " HTTP/1.0\r\nHost: api.open-meteo.com\r\nConnection: close\r\n\r\n";

// Free IP geolocation (plain HTTP). Only city, region and coordinates are asked for.
static const char LOC_REQ[] PROGMEM =
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

static bool fetchWeatherInner() {
  fetchStep = 1;   // AT+CIPMUX
  if (!espCmd(F("AT+CIPMUX=0"), F("OK"), 2000)) return false;
  fetchStep = 2;   // open TCP connection (fails on DNS / no internet)
  espFlush();
  esp.println(F("AT+CIPSTART=\"TCP\",\"api.open-meteo.com\",80"));
  if (!espWaitFor(F("OK"), 8000)) return false;

  fetchStep = 3;   // ask to send, wait for '>'
  uint16_t len = strlen_P(REQ_A) + strlen(locLat) + strlen_P(REQ_B) +
                 strlen(locLon) + strlen_P(REQ_C);
  espFlush();
  esp.print(F("AT+CIPSEND="));
  esp.println(len);
  if (!espWaitFor(F(">"), 3000)) return false;
  espWriteP(REQ_A);
  esp.print(locLat);
  espWriteP(REQ_B);
  esp.print(locLon);
  espWriteP(REQ_C);

  fetchStep = 4;   // wait for the server's JSON
  // Wait for "current":{ while keeping the first bytes received, so a
  // failure can show what the ESP-01 actually sent.
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
  if (!gotKey) return false;

  esp.setTimeout(8000);
  Weather w;
  fetchStep = 5;   // parsing the fields
  // "time":"2026-11-04T16:00" (local time) -> hour and minute
  if (!esp.find((char*)"\"time\":\"")) return false;
  if (!esp.find((char*)"T"))           return false;
  int hh = esp.parseInt();
  int mm = esp.parseInt();
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

  // Next 4 hours. "hourly":{ does not match "hourly_units":{ .
  if (!esp.find((char*)"\"hourly\":{")) return false;
  if (!esp.find((char*)"\"time\":["))   return false;
  for (uint8_t i = 0; i < 4; i++) {          // "2026-11-04T20:00" -> 20
    if (!esp.find((char*)"T")) return false;
    w.hr[i].hour = esp.parseInt();
  }
  if (!esp.find((char*)"\"temperature_2m\":["))           return false;
  for (uint8_t i = 0; i < 4; i++) w.hr[i].tempC = (int8_t)lroundf(esp.parseFloat());
  if (!esp.find((char*)"\"precipitation_probability\":[")) return false;
  for (uint8_t i = 0; i < 4; i++) w.hr[i].rainProb = esp.parseInt();
  if (!esp.find((char*)"\"weather_code\":["))             return false;
  for (uint8_t i = 0; i < 4; i++) w.hr[i].code = esp.parseInt();

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

  w.valid = true;
  weather = w;

  if (hh >= 0 && hh < 24 && mm >= 0 && mm < 60) {
    syncMinOfDay = hh * 60 + mm;
    syncMs = millis();
    timeValid = true;
  }

  if (w.precipNow > 0.0 || isRainCode(w.code)) {
    rainSeen = true;
    lastRainMs = millis();
  }

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

// Ask ip-api.com where this internet connection is. Sets locName, locLat, locLon.
static bool fetchLocationInner() {
  if (!espCmd(F("AT+CIPMUX=0"), F("OK"), 2000)) return false;
  espFlush();
  esp.println(F("AT+CIPSTART=\"TCP\",\"ip-api.com\",80"));
  if (!espWaitFor(F("OK"), 8000)) return false;

  espFlush();
  esp.print(F("AT+CIPSEND="));
  esp.println((uint16_t)strlen_P(LOC_REQ));
  if (!espWaitFor(F(">"), 3000)) return false;
  espWriteP(LOC_REQ);

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

  dtostrf(lat, 1, 3, locLat);
  dtostrf(lon, 1, 3, locLon);
  snprintf_P(locName, sizeof(locName), PSTR("%s, %s"), city, region);
  return true;
}

bool fetchLocation() {
  bool ok = fetchLocationInner();
  esp.println(F("AT+CIPCLOSE"));
  delay(300);
  espFlush();
  if (ok) {
    located = true;
    Serial.print(F("Location: ")); Serial.print(locName);
    Serial.print(F(" (")); Serial.print(locLat); Serial.print(F(", "));
    Serial.print(locLon); Serial.println(')');
  } else {
    Serial.print(F("Location lookup failed, using ")); Serial.println(locName);
  }
  return ok;
}

// Always closes the TCP connection afterwards. Without this, a failed fetch
// left the connection open and every retry died with "ALREADY CONNECTED".
bool fetchWeather() {
  bool ok = fetchWeatherInner();
  if (!ok) {
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
  esp.println(F("AT+CIPCLOSE"));
  delay(300);
  espFlush();
  return ok;
}
