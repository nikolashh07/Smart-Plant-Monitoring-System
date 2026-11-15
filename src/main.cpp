#include <Arduino.h>
#include <Wire.h>
#include <SoftwareSerial.h>

#define OLED_ADDR 0x3C

// =====================================================================
// 1) EDIT THESE: WiFi + location
// =====================================================================
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASS     "YOUR_WIFI_PASSWORD"
// Auto-detect where we are from the internet connection (IP address).
// 1 = look it up (these three below become the fallback if it fails)
// 0 = always use the three values below
// NOTE: IP lookup finds your internet provider's hub, which can be a city or
// two away, and a phone hotspot is often worse. Set to 0 for exact weather.
#define AUTO_LOCATION 1
#define LOCATION_NAME "Austin, Texas"   // top line of weather screen (max 21 chars)
#define LATITUDE      "30.27"
#define LONGITUDE     "-97.74"

// ESP-01 wiring: ESP TX -> D2, ESP RX -> D3 (through a 3.3V level divider!)
// Set the ESP to 9600 baud once with:  AT+UART_DEF=9600,8,1,0,0
#define ESP_RX_PIN 2
#define ESP_TX_PIN 3
#define ESP_BAUD   9600

// =====================================================================
// 2) Pump wiring
// =====================================================================
// Drive the pump through a logic-level MOSFET (or relay) from D4, with a
// flyback diode across the pump. Never power the pump from the Arduino pin.
// Active-LOW relay board? Swap PUMP_ON and PUMP_OFF.
#define PUMP_PIN 4
#define PUMP_ON  HIGH
#define PUMP_OFF LOW

// =====================================================================
// 3) Soil calibration (capacitive sensor: HIGHER reading = DRIER)
// =====================================================================
// Set CAL_MODE to 1, upload, and watch the raw number on the OLED/Serial.
//   SOIL_DRY_RAW = raw reading with the sensor in dry potting mix
//   SOIL_FC_RAW  = raw reading after soaking the bed, letting it drain
//                  for a few hours (field capacity), sensor at 2-3 in deep
// These two defaults are GUESSES. Replace them with your own readings.
// Reported soil % = 0 at dry mix, 100 at field capacity.
#define CAL_MODE      0
#define SOIL_DRY_RAW  540
#define SOIL_FC_RAW   360

// =====================================================================
// 4) Watering rules for radishes in an 8x12 in bed, 5 in deep
// =====================================================================
// Bed = 96 sq in (~619 sq cm), ~7.9 L of mix. 100 mL on top is ~1.6 mm.
// Radishes: start watering at ~25% depletion, stop near field capacity.
#define START_PCT      75   // start watering below this
#define STOP_PCT       95   // stop once soil reaches this
#define CRITICAL_PCT   50   // below this, water even if rain is forecast

#define PUMP_ML_PER_SEC 20  // MEASURE THIS: time the pump into a measuring cup
#define PULSE_ML        100 // water per pulse (before ET scaling)
#define SOAK_MS         180000UL // wait after each pulse so water spreads
#define MAX_PUMP_MS     15000UL  // hard cap on any single pump run
#define MAX_ML_PER_DAY  600      // ~10 mm/day safety cap
#define MAX_PULSES_PER_SESSION 5 // if soil doesn't respond, flag a fault

// Forecast rules
#define RAIN_PROB_HOLD   60    // % chance of rain today that holds watering
#define RAIN_MM_HOLD     3.0   // mm of rain today that holds watering
#define FREEZE_LOCK_C    0.0   // pump off if today's low is at/below this

#define RAIN_LOCK_MS      7200000UL // stay in rain mode 2 h after rain stops

#define WATER_CHECK_MS    600000UL // base interval between watering checks
#define WEATHER_REFRESH_MS 600000UL
#define SOIL_REFRESH_MS    5000UL
#define SCREEN_SWITCH_MS   6000UL

SoftwareSerial esp(ESP_RX_PIN, ESP_TX_PIN);

// Minimal 5x8 font array (ASCII 32 to 122)
const uint8_t font5x8[][5] PROGMEM = {
  {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00}, {0x00,0x07,0x00,0x07,0x00}, {0x14,0x7F,0x14,0x7F,0x14},
  {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62}, {0x36,0x49,0x55,0x22,0x50}, {0x00,0x05,0x03,0x00,0x00},
  {0x00,0x1C,0x22,0x41,0x00}, {0x00,0x41,0x22,0x1C,0x00}, {0x08,0x2A,0x1C,0x2A,0x08}, {0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08}, {0x00,0x60,0x60,0x00,0x00}, {0x20,0x10,0x08,0x04,0x02},
  {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00}, {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
  {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}, {0x00,0x36,0x36,0x00,0x00}, {0x00,0x56,0x36,0x00,0x00},
  {0x00,0x08,0x14,0x22,0x41}, {0x14,0x14,0x14,0x14,0x14}, {0x41,0x22,0x14,0x08,0x00}, {0x02,0x01,0x51,0x09,0x06},
  {0x32,0x49,0x79,0x41,0x3E}, {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x01,0x01}, {0x3E,0x41,0x41,0x51,0x32},
  {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41},
  {0x7F,0x40,0x40,0x40,0x40}, {0x7F,0x02,0x04,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46}, {0x46,0x49,0x49,0x49,0x31},
  {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F}, {0x7F,0x20,0x18,0x20,0x7F},
  {0x63,0x14,0x08,0x14,0x63}, {0x03,0x04,0x78,0x04,0x03}, {0x61,0x51,0x49,0x45,0x43}, {0x00,0x00,0x7F,0x41,0x41},
  {0x02,0x04,0x08,0x10,0x20}, {0x41,0x41,0x7F,0x00,0x00}, {0x04,0x02,0x01,0x02,0x04}, {0x40,0x40,0x40,0x40,0x40},
  {0x00,0x01,0x02,0x04,0x00}, {0x20,0x54,0x54,0x54,0x78}, {0x7F,0x48,0x44,0x44,0x38}, {0x38,0x44,0x44,0x44,0x20},
  {0x38,0x44,0x44,0x48,0x7F}, {0x38,0x54,0x54,0x54,0x18}, {0x08,0x7E,0x09,0x01,0x02}, {0x08,0x14,0x54,0x54,0x3C},
  {0x7F,0x08,0x04,0x04,0x78}, {0x00,0x44,0x7D,0x40,0x00}, {0x20,0x40,0x44,0x3D,0x00}, {0x00,0x7F,0x10,0x28,0x44},
  {0x00,0x41,0x7F,0x40,0x00}, {0x7C,0x04,0x18,0x04,0x78}, {0x7C,0x08,0x04,0x04,0x78}, {0x38,0x44,0x44,0x44,0x38},
  {0x7C,0x14,0x14,0x14,0x08}, {0x08,0x14,0x14,0x18,0x7C}, {0x7C,0x08,0x04,0x04,0x08}, {0x48,0x54,0x54,0x54,0x20},
  {0x04,0x3F,0x44,0x40,0x20}, {0x3C,0x40,0x40,0x20,0x7C}, {0x1C,0x20,0x40,0x20,0x1C}, {0x3C,0x40,0x30,0x40,0x3C},
  {0x44,0x28,0x10,0x28,0x44}, {0x0C,0x50,0x50,0x50,0x3C}, {0x44,0x64,0x54,0x4C,0x44}
};

// =====================================================================
// OLED driver
// =====================================================================
void oledCommand(uint8_t c) {
  Wire.beginTransmission(OLED_ADDR);
  Wire.write(0x00);
  Wire.write(c);
  Wire.endTransmission();
}

void oledSetCursor(uint8_t col, uint8_t page) {
  oledCommand(0xB0 + page);
  oledCommand(0x00 + (col & 0x0F));
  oledCommand(0x10 + ((col >> 4) & 0x0F));
}

void oledClear() {
  for (uint8_t page = 0; page < 8; page++) {
    oledSetCursor(0, page);
    for (uint8_t col = 0; col < 128; col += 16) {
      Wire.beginTransmission(OLED_ADDR);
      Wire.write(0x40);
      for (uint8_t i = 0; i < 16; i++) { Wire.write(0x00); }
      Wire.endTransmission();
    }
  }
}

void oledInit() {
  Wire.begin();
  Wire.setWireTimeout(5000, true);   // never hang if the OLED/bus misbehaves
  uint8_t initSeq[] = {
    0xAE, 0x20, 0x00, 0x21, 0x00, 0x7F, 0x22, 0x00, 0x07, 0x40,
    0xA1, 0xC8, 0xA8, 0x3F, 0xD3, 0x00, 0xD5, 0x80, 0xD9, 0xF1,
    0xDA, 0x12, 0xDB, 0x40, 0x8D, 0x14, 0xA4, 0xA6, 0xAF
  };
  for (uint8_t i = 0; i < sizeof(initSeq); i++) {
    oledCommand(initSeq[i]);
  }
}

void oledPrintChar(char c) {
  if (c < 32 || c > 122) return;
  Wire.beginTransmission(OLED_ADDR);
  Wire.write(0x40);
  for (uint8_t i = 0; i < 5; i++) {
    Wire.write(pgm_read_byte(&font5x8[c - 32][i]));
  }
  Wire.write(0x00);
  Wire.endTransmission();
}

// Print a RAM string padded to 21 chars (overwrites old text, no flicker).
void oledPrintLine(const char* str, uint8_t page) {
  oledSetCursor(0, page);
  uint8_t n = 0;
  while (*str && n < 21) { oledPrintChar(*str++); n++; }
  while (n < 21) { oledPrintChar(' '); n++; }
}

// Same, but for F("...") strings so they don't eat the Uno's 2KB of RAM.
void oledPrintLine(const __FlashStringHelper* fs, uint8_t page) {
  const char* p = (const char*)fs;
  oledSetCursor(0, page);
  uint8_t n = 0;
  char c;
  while ((c = pgm_read_byte(p++)) && n < 21) { oledPrintChar(c); n++; }
  while (n < 21) { oledPrintChar(' '); n++; }
}

static uint8_t expandNibble(uint8_t nib) {
  uint8_t r = 0;
  for (uint8_t i = 0; i < 4; i++) if (nib & (1 << i)) r |= 3 << (2 * i);
  return r;
}

// 2x-scaled text, occupies `page` and `page + 1`.
void oledPrintBig(const char* str, uint8_t col, uint8_t page) {
  for (uint8_t half = 0; half < 2; half++) {
    oledSetCursor(col, page + half);
    for (const char* s = str; *s; s++) {
      if (*s < 32 || *s > 122) continue;
      Wire.beginTransmission(OLED_ADDR);
      Wire.write(0x40);
      for (uint8_t i = 0; i < 5; i++) {
        uint8_t b = pgm_read_byte(&font5x8[*s - 32][i]);
        uint8_t v = expandNibble(half ? (b >> 4) : (b & 0x0F));
        Wire.write(v); Wire.write(v);
      }
      Wire.write(0x00); Wire.write(0x00);
      Wire.endTransmission();
    }
  }
}

// =====================================================================
// Weather + forecast client (ESP-01 AT firmware, Open-Meteo over HTTP)
// =====================================================================
struct HourFc {          // one hour of the short-range forecast
  uint8_t hour;          // local hour 0-23
  int8_t  tempC;
  uint8_t rainProb;      // %
  uint8_t code;          // WMO weather code
};

struct Weather {
  bool  valid = false;
  float tempC = 0;
  int   humidity = 0;
  int   code = 0;
  float windKmh = 0;
  float precipNow = 0;  // rain right now, mm
  HourFc hr[4];         // this hour + the next 3
  int   rainProb = 0;   // today's max chance of rain, %
  float rainMm = 0;     // today's expected rain, mm
  float et0 = 3.0;      // today's reference evapotranspiration, mm
  float minC = 20;      // today's low, C
  float maxC = 20;      // today's high, C
};

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
const char REQ_A[] PROGMEM = "GET /v1/forecast?latitude=";
const char REQ_B[] PROGMEM = "&longitude=";
const char REQ_C[] PROGMEM =
  "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,precipitation"
  "&hourly=temperature_2m,precipitation_probability,weather_code"
  "&daily=precipitation_probability_max,precipitation_sum,"
  "et0_fao_evapotranspiration,temperature_2m_min,temperature_2m_max"
  "&forecast_days=1&forecast_hours=4&timezone=auto"
  " HTTP/1.0\r\nHost: api.open-meteo.com\r\nConnection: close\r\n\r\n";

// Free IP geolocation (plain HTTP). Only city, region and coordinates are asked for.
const char LOC_REQ[] PROGMEM =
  "GET /json/?fields=status,regionName,city,lat,lon HTTP/1.0\r\n"
  "Host: ip-api.com\r\nConnection: close\r\n\r\n";

static void espWriteP(const char* p) {
  char c;
  while ((c = pgm_read_byte(p++))) esp.write(c);
}

bool isRainCode(int c) {
  return (c >= 51 && c <= 67) || (c >= 80 && c <= 82) || c >= 95;
}

bool espWaitFor(const __FlashStringHelper* target, uint16_t timeoutMs) {
  char buf[16];
  uint8_t len = strlen_P((const char*)target);
  if (len >= sizeof(buf)) return false;
  strcpy_P(buf, (const char*)target);
  esp.setTimeout(timeoutMs);
  return esp.find(buf);
}

void espFlush() { while (esp.available()) esp.read(); }

bool espCmd(const __FlashStringHelper* cmd, const __FlashStringHelper* expect, uint16_t timeoutMs) {
  espFlush();
  esp.println(cmd);
  return espWaitFor(expect, timeoutMs);
}

bool wifiConnect() {
  if (!espCmd(F("AT"), F("OK"), 1000)) return false;
  if (!espCmd(F("AT+CWMODE=1"), F("OK"), 2000)) return false;
  espFlush();
  esp.print(F("AT+CWJAP=\""));
  esp.print(F(WIFI_SSID));
  esp.print(F("\",\""));
  esp.print(F(WIFI_PASS));
  esp.println(F("\""));
  return espWaitFor(F("OK"), 15000);
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
uint8_t fetchStep = 0;
char fetchDbg[120];   // first bytes the ESP-01 sent back (for debugging)

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

// =====================================================================
// Soil + pump control
// =====================================================================
enum PumpState : uint8_t { P_IDLE, P_PUMPING, P_SOAKING };

PumpState pumpState = P_IDLE;
unsigned long stateSince = 0, lastCheck = 0, pulseMs = 0, dayStart = 0;
bool checkNow = true;
bool session = false;     // true from "too dry" until "wet enough" (hysteresis)
bool fault = false;       // soil didn't respond to water: pump/reservoir problem
bool holdRain = false;
bool freezeLock = false;
uint8_t pulsesInSession = 0;
int lastPulseMl = 0;
int mlToday = 0;
int soilRaw = 0;
int soilPct = 0;

void pumpOn()  { digitalWrite(PUMP_PIN, PUMP_ON); }
void pumpOff() { digitalWrite(PUMP_PIN, PUMP_OFF); }

int readSoilRaw() {
  long sum = 0;
  for (uint8_t i = 0; i < 16; i++) { sum += analogRead(A0); delay(2); }
  return (int)(sum / 16);
}

// 0% = dry mix, 100% = field capacity (can read a bit over 100 when soggy).
int rawToPercent(int raw) {
  long pct = (long)(SOIL_DRY_RAW - raw) * 100L / (SOIL_DRY_RAW - SOIL_FC_RAW);
  return (int)constrain(pct, 0, 130);
}

const __FlashStringHelper* soilLabel() {
  if (soilPct < START_PCT) return F("DRY");
  if (soilPct > 108)       return F("WET");
  return F("GOOD");
}

bool rainLikely() {
  return weather.valid && (weather.rainProb >= RAIN_PROB_HOLD || weather.rainMm >= RAIN_MM_HOLD);
}

// Hot, dry, windy day -> bigger pulses; cool day -> smaller.
float etScale() {
  if (!weather.valid) return 1.0;
  float s = weather.et0 / 3.0;
  return constrain(s, 0.7, 1.5);
}

unsigned long waterCheckMs() {
  float et = weather.valid ? weather.et0 : 3.0;
  if (et >= 5.0) return WATER_CHECK_MS / 2;
  if (et <= 2.0) return WATER_CHECK_MS * 2;
  return WATER_CHECK_MS;
}

void waterLogic(unsigned long now) {
#if CAL_MODE
  pumpOff();
  return;
#endif
  // Rain mode: pump locked out, soil readings ignored, drainage takes over.
  if (rainMode) {
    pumpOff();
    pumpState = P_IDLE;
    holdRain = false;
    return;
  }
  if (pumpState == P_PUMPING) {
    if (now - stateSince >= pulseMs) {
      pumpOff();
      mlToday += lastPulseMl;
      pulsesInSession++;
      pumpState = P_SOAKING;
      stateSince = now;
    }
    return;
  }
  if (pumpState == P_SOAKING) {
    if (now - stateSince >= SOAK_MS) { pumpState = P_IDLE; checkNow = true; }
    return;
  }

  // Idle: decide whether to start a pulse.
  if (!checkNow && now - lastCheck < waterCheckMs()) return;
  checkNow = false;
  lastCheck = now;

  freezeLock = weather.valid && weather.minC <= FREEZE_LOCK_C;

  if (!session && soilPct < START_PCT) { session = true; pulsesInSession = 0; }
  if (session && soilPct >= STOP_PCT)  { session = false; pulsesInSession = 0; fault = false; }

  holdRain = false;
  if (!session || fault) return;
  if (pulsesInSession >= MAX_PULSES_PER_SESSION) { fault = true; return; }
  if (freezeLock) return;
  if (mlToday >= MAX_ML_PER_DAY) return;
  if (rainLikely() && soilPct >= CRITICAL_PCT) { holdRain = true; return; }

  lastPulseMl = (int)(PULSE_ML * etScale() + 0.5);
  pulseMs = (unsigned long)lastPulseMl * 1000UL / PUMP_ML_PER_SEC;
  if (pulseMs > MAX_PUMP_MS) pulseMs = MAX_PUMP_MS;
  lastPulseMl = (int)(pulseMs * PUMP_ML_PER_SEC / 1000UL);
  pumpOn();
  pumpState = P_PUMPING;
  stateSince = now;
}

// =====================================================================
// Display (two screens that alternate; page 7 is always the status line)
// =====================================================================
// One-line status text, shared by the OLED bottom line and the Serial Monitor.
void buildStatus(char* out, size_t n) {
#if CAL_MODE
  snprintf_P(out, n, PSTR("CAL raw %d  %d%%"), soilRaw, soilPct);
  return;
#endif
  if (rainMode)                     strncpy_P(out, PSTR("RAIN MODE: pump off"), n);
  else if (fault)                   strncpy_P(out, PSTR("CHECK PUMP / WATER"), n);
  else if (pumpState == P_PUMPING)  strncpy_P(out, PSTR("Watering..."), n);
  else if (pumpState == P_SOAKING)  strncpy_P(out, PSTR("Soaking in..."), n);
  else if (freezeLock && session)   strncpy_P(out, PSTR("Freeze: pump off"), n);
  else if (holdRain)                strncpy_P(out, PSTR("Rain soon: holding"), n);
  else if (session && mlToday >= MAX_ML_PER_DAY) strncpy_P(out, PSTR("Daily limit hit"), n);
  else snprintf_P(out, n, PSTR("Soil %d%% %S"), soilPct, (const char*)soilLabel());
  out[n - 1] = 0;
}

void drawStatusLine() {
  char line[24];
  buildStatus(line, sizeof(line));
  oledPrintLine(line, 7);
}

// ---- Text helpers -----------------------------------------------------
// Text at (col, page), padded with spaces to `width` chars (6 px each).
void oledTextAt(const char* s, uint8_t col, uint8_t page, uint8_t width) {
  oledSetCursor(col, page);
  uint8_t n = 0;
  while (*s && n < width) { oledPrintChar(*s++); n++; }
  while (n < width) { oledPrintChar(' '); n++; }
}

void oledTextAt(const __FlashStringHelper* fs, uint8_t col, uint8_t page, uint8_t width) {
  const char* p = (const char*)fs;
  oledSetCursor(col, page);
  uint8_t n = 0;
  char c;
  while ((c = pgm_read_byte(p++)) && n < width) { oledPrintChar(c); n++; }
  while (n < width) { oledPrintChar(' '); n++; }
}

// Centre text on pixel column cx.
void oledCenter(const char* s, uint8_t cx, uint8_t page) {
  uint8_t half = strlen(s) * 3;
  oledSetCursor(cx > half ? cx - half : 0, page);
  while (*s) oledPrintChar(*s++);
}

// "Label ............ value" across the full 21-char width.
void oledRow(const __FlashStringHelper* label, const char* value, uint8_t page) {
  char line[22];
  memset(line, ' ', 21);
  line[21] = 0;
  strncpy_P(line, (const char*)label, 21);
  for (uint8_t i = 0; i < 21; i++) if (line[i] == 0) line[i] = ' ';
  uint8_t vl = strlen(value);
  if (vl > 21) vl = 21;
  memcpy(line + 21 - vl, value, vl);
  oledPrintLine(line, page);
}

void oledDivider(uint8_t page) {
  oledSetCursor(0, page);
  for (uint8_t c = 0; c < 128; c += 16) {
    Wire.beginTransmission(OLED_ADDR);
    Wire.write(0x40);
    for (uint8_t i = 0; i < 16; i++) Wire.write(0x08); // 1px line mid-page
    Wire.endTransmission();
  }
}

// Horizontal bar, 102 px wide, one page tall. pct 0-100.
void oledBar(uint8_t page, int pct) {
  pct = constrain(pct, 0, 100);
  oledSetCursor(0, page);
  for (uint8_t c0 = 0; c0 < 102; c0 += 17) {
    Wire.beginTransmission(OLED_ADDR);
    Wire.write(0x40);
    for (uint8_t c = c0; c < c0 + 17; c++) {
      uint8_t v = (c == 0 || c == 101 || c <= pct) ? 0x7E : 0x42;
      Wire.write(v);
    }
    Wire.endTransmission();
  }
}

// ---- Weather icons, drawn at runtime (no bitmap tables) ---------------
// Shapes are defined on a 24x24 grid and sampled at 16 or 24 px.
enum : uint8_t { IC_SUN, IC_PART, IC_CLOUD, IC_FOG, IC_RAIN, IC_SNOW, IC_STORM };

static bool inCircle(float x, float y, float cx, float cy, float r) {
  float dx = x - cx, dy = y - cy;
  return dx * dx + dy * dy <= r * r;
}

static bool segHit(float x, float y, float x1, float y1, float x2, float y2, float w2) {
  float vx = x2 - x1, vy = y2 - y1, wx = x - x1, wy = y - y1;
  float t = (wx * vx + wy * vy) / (vx * vx + vy * vy);
  t = constrain(t, 0.0f, 1.0f);
  float dx = wx - t * vx, dy = wy - t * vy;
  return dx * dx + dy * dy <= w2;
}

static bool cloudHit(float x, float y, float oy, float pad) {
  y -= oy;
  if (inCircle(x, y, 8, 14, 5 + pad))      return true;
  if (inCircle(x, y, 13, 10.5, 6.5 + pad)) return true;
  if (inCircle(x, y, 18.5, 14.5, 4.5 + pad)) return true;
  return x >= 8 && x <= 18.5 && y >= 14 && y <= 19 + pad;
}

static bool sunHit(float x, float y, float cx, float cy, float r, float rin, float rout) {
  float dx = x - cx, dy = y - cy, d2 = dx * dx + dy * dy;
  if (d2 <= r * r) return true;
  if (d2 < rin * rin || d2 > rout * rout) return false;
  if (fabsf(dx) < 1.0f || fabsf(dy) < 1.0f) return true;         // N/S/E/W rays
  return fabsf(dx - dy) < 1.5f || fabsf(dx + dy) < 1.5f;         // diagonal rays
}

static bool iconPixel(uint8_t t, float x, float y) {
  switch (t) {
    case IC_SUN:   return sunHit(x, y, 12, 12, 5, 7.5, 11);
    case IC_PART:  return cloudHit(x, y, 3, 0) ||
                          (sunHit(x, y, 8, 8, 4, 6, 8.5) && !cloudHit(x, y, 3, 1.6));
    case IC_CLOUD: return cloudHit(x, y, 0.5, 0);
    case IC_FOG:   return x >= 3 && x <= 21 &&
                          (fabsf(y - 6) < 1 || fabsf(y - 11) < 1 || fabsf(y - 16) < 1 || fabsf(y - 21) < 1);
    case IC_RAIN:  return cloudHit(x, y, -2, 0) ||
                          segHit(x, y, 8, 19, 7, 23, 0.9) || segHit(x, y, 13, 19, 12, 23, 0.9) ||
                          segHit(x, y, 18, 19, 17, 23, 0.9);
    case IC_SNOW:  return cloudHit(x, y, -2, 0) ||
                          inCircle(x, y, 7, 20, 1.4) || inCircle(x, y, 12, 22.5, 1.4) ||
                          inCircle(x, y, 17, 20, 1.4);
    case IC_STORM: return cloudHit(x, y, -2, 0) ||
                          segHit(x, y, 14, 18, 11, 21, 1.0) || segHit(x, y, 11, 21, 15, 21, 1.0) ||
                          segHit(x, y, 15, 21, 12, 24, 1.0);
  }
  return false;
}

uint8_t iconFor(int c) {
  if (c <= 1)  return IC_SUN;
  if (c == 2)  return IC_PART;
  if (c == 3)  return IC_CLOUD;
  if (c <= 48) return IC_FOG;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return IC_SNOW;
  if (c >= 95) return IC_STORM;
  return IC_RAIN;
}

// size = 16 or 24 px square; occupies size/8 pages starting at `page`.
void oledIcon(uint8_t type, uint8_t size, uint8_t col, uint8_t page) {
  uint8_t buf[72];
  memset(buf, 0, sizeof(buf));
  for (uint8_t tx = 0; tx < size; tx++) {
    float x = (tx + 0.5f) * 24.0f / size;
    for (uint8_t ty = 0; ty < size; ty++) {
      float y = (ty + 0.5f) * 24.0f / size;
      if (iconPixel(type, x, y)) buf[(ty >> 3) * size + tx] |= 1 << (ty & 7);
    }
  }
  for (uint8_t p = 0; p < size / 8; p++) {
    oledSetCursor(col, page + p);
    for (uint8_t c = 0; c < size; c += 8) {
      Wire.beginTransmission(OLED_ADDR);
      Wire.write(0x40);
      for (uint8_t i = 0; i < 8; i++) Wire.write(buf[p * size + c + i]);
      Wire.endTransmission();
    }
  }
}

// ---- Screens ----------------------------------------------------------
// 1) NOW: icon, big temperature, condition, humidity, wind
void drawNowScreen() {
  char a[12], v[12];
  oledTextAt(locName, 0, 0, rainMode ? 11 : 21);
  if (rainMode) oledTextAt(F("RAIN MODE"), 74, 0, 9);

  if (!weather.valid) {
    oledPrintLine(F("No weather data"), 3);
    oledPrintLine(F("Check WiFi / ESP-01"), 4);
    return;
  }
  oledIcon(iconFor(weather.code), 24, 0, 1);              // pages 1-3

  dtostrf(weather.tempC, 1, 1, a);
  strcat(a, "C");
  oledPrintBig(a, 32, 1);                                  // pages 1-2
  oledTextAt(weatherText(weather.code), 32, 3, 16);

  oledDivider(4);
  snprintf_P(v, sizeof(v), PSTR("%d%%"), weather.humidity);
  oledRow(F("Humidity"), v, 5);
  snprintf_P(v, sizeof(v), PSTR("%d km/h"), (int)(weather.windKmh + 0.5f));
  oledRow(F("Wind"), v, 6);
}

// 2) FORECAST: next 4 hours in four columns, plus today's high/low
void drawForecastScreen() {
  char v[12];
  oledPrintLine(F("FORECAST: NEXT HOURS"), 0);
  if (!weather.valid) {
    oledPrintLine(F("No weather data"), 3);
    return;
  }
  // Row labels on the left, three hours across the right.
  oledTextAt(F("Temp"), 0, 4, 4);
  oledTextAt(F("Rain"), 0, 5, 4);
  for (uint8_t i = 0; i < 3; i++) {
    uint8_t cx = 48 + i * 32;
    if (i == 0) {
      strcpy_P(v, PSTR("Now"));
    } else {
      uint8_t h = weather.hr[i].hour;
      snprintf_P(v, sizeof(v), PSTR("%d%S"), h % 12 == 0 ? 12 : h % 12,
                 (const char*)(h < 12 ? PSTR("AM") : PSTR("PM")));
    }
    oledCenter(v, cx, 1);
    oledIcon(iconFor(weather.hr[i].code), 16, cx - 8, 2);   // pages 2-3
    snprintf_P(v, sizeof(v), PSTR("%dC"), weather.hr[i].tempC);
    oledCenter(v, cx, 4);
    snprintf_P(v, sizeof(v), PSTR("%d%%"), weather.hr[i].rainProb);
    oledCenter(v, cx, 5);
  }
  snprintf_P(v, sizeof(v), PSTR("%d / %dC"), (int)lroundf(weather.maxC), (int)lroundf(weather.minC));
  oledRow(F("Today high / low"), v, 6);
}

// 3) GARDEN: soil level, bar, rain and watering totals
void drawGardenScreen() {
  char a[10], v[14];
  oledPrintLine(F("RADISH BED"), 0);

  snprintf_P(a, sizeof(a), PSTR("%d%%"), min(soilPct, 100));
  uint8_t n = strlen(a);
  while (n < 5) a[n++] = ' ';       // blank-pad so old digits get erased
  a[n] = 0;
  oledPrintBig(a, 0, 1);                                    // pages 1-2
  if (rainMode) oledTextAt(F("(ignored)"), 72, 1, 9);
  else          oledTextAt(soilLabel(), 78, 1, 5);
  snprintf_P(v, sizeof(v), PSTR("goal %d-%d%%"), START_PCT, STOP_PCT);
  oledTextAt(v, 60, 2, 11);
  oledBar(3, soilPct);

  if (weather.valid) {
    dtostrf(weather.rainMm, 1, 1, a);
    snprintf_P(v, sizeof(v), PSTR("%d%% %smm"), weather.rainProb, a);
  } else {
    strcpy_P(v, PSTR("no data"));
  }
  oledRow(F("Rain today"), v, 4);
  snprintf_P(v, sizeof(v), PSTR("%d mL"), mlToday);
  oledRow(F("Pumped today"), v, 5);
  if (weather.valid) {
    dtostrf(weather.et0, 1, 1, a);
    snprintf_P(v, sizeof(v), PSTR("%s mm"), a);
  } else {
    strcpy_P(v, PSTR("--"));
  }
  oledRow(F("Evaporation"), v, 6);
}

#define NUM_SCREENS 3

void drawScreen(uint8_t screen, bool full) {
  // On a plain soil tick only the Garden page and the status line can change,
  // so leave the weather pages alone instead of redrawing them (less flicker).
  if (!full && screen != 2) { drawStatusLine(); return; }
  if (full) oledClear();
  if (screen == 0)      drawNowScreen();
  else if (screen == 1) drawForecastScreen();
  else                  drawGardenScreen();
  drawStatusLine();
}

// ---- Serial Monitor: everything the OLED shows, plus the raw reading ----
void printSerialStatus() {
  char st[24];
  buildStatus(st, sizeof(st));

  Serial.println(F("------------------------------------------"));
  Serial.print(F("Soil:     ")); Serial.print(min(soilPct, 100)); Serial.print(F("%  "));
  Serial.print(soilLabel());
  Serial.print(F("   (raw ")); Serial.print(soilRaw); Serial.println(')');
  Serial.print(F("Status:   ")); Serial.println(st);

  if (!weather.valid) {
    Serial.println(F("Weather:  no data"));
  } else {
    Serial.print(F("Weather:  ")); Serial.print(weather.tempC, 1); Serial.print(F("C "));
    Serial.print(weatherText(weather.code));
    Serial.print(F(" | Hum ")); Serial.print(weather.humidity);
    Serial.print(F("% | Wind ")); Serial.print((int)(weather.windKmh + 0.5f)); Serial.println(F(" km/h"));

    Serial.print(F("Today:    Hi ")); Serial.print(weather.maxC, 1);
    Serial.print(F(" / Lo ")); Serial.print(weather.minC, 1);
    Serial.print(F("C | Rain ")); Serial.print(weather.rainProb);
    Serial.print(F("% ")); Serial.print(weather.rainMm, 1);
    Serial.print(F("mm | Evap ")); Serial.print(weather.et0, 1); Serial.println(F("mm"));

    Serial.print(F("Next 4h:  "));
    for (uint8_t i = 0; i < 4; i++) {
      if (i == 0) Serial.print(F("Now"));
      else        { Serial.print(weather.hr[i].hour); Serial.print('h'); }
      Serial.print(' '); Serial.print(weather.hr[i].tempC);
      Serial.print(F("C ")); Serial.print(weather.hr[i].rainProb); Serial.print('%');
      if (i < 3) Serial.print(F(" | "));
    }
    Serial.println();
  }
  Serial.print(F("Pumped:   ")); Serial.print(mlToday); Serial.println(F(" mL today"));
  if (timeValid) {
    uint16_t m = localMinOfDay(millis());
    Serial.print(F("Time:     ")); Serial.print(m / 60); Serial.print(':');
    if (m % 60 < 10) Serial.print('0');
    Serial.print(m % 60); Serial.println(F(" (local, resets pump counters at 00:00)"));
  }
}

// Reset the pump's daily tracking: millilitres pumped today, the daily limit
// and any fault from yesterday. Rain mode is NOT cleared (it follows the weather).
void midnightReset(unsigned long now) {
  dayStart = now;
  mlToday = 0;
  fault = false;
  pulsesInSession = 0;
  checkNow = true;
  Serial.println(F("*** Midnight reset: pump counters cleared ***"));
}

// =====================================================================
void setup() {
  pinMode(PUMP_PIN, OUTPUT);
  pumpOff();

  // Start with the fallback location from the settings at the top.
  strncpy(locName, LOCATION_NAME, sizeof(locName) - 1);
  strncpy(locLat, LATITUDE, sizeof(locLat) - 1);
  strncpy(locLon, LONGITUDE, sizeof(locLon) - 1);

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
  if (timeValid) {
    uint16_t m = localMinOfDay(now);
    if (prevMinOfDay > 1200 && m < 240) midnightReset(now);   // clock wrapped past 00:00
    prevMinOfDay = m;
  } else if (now - dayStart >= 86400000UL) {
    midnightReset(now);
  }

  // Never block on WiFi while the pump is running.
  if (pumpState != P_PUMPING &&
      (firstFetch || now - lastWeather >= WEATHER_REFRESH_MS)) {
    firstFetch = false;
    lastWeather = now;
    oledPrintLine(F("Updating..."), 7);
#if AUTO_LOCATION
    if (!located) fetchLocation();   // retried each refresh until it works
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

  uint8_t screen = (now / SCREEN_SWITCH_MS) % NUM_SCREENS;
  if (soilTick || screen != lastScreen) {
    drawScreen(screen, screen != lastScreen);
    lastScreen = screen;
  }
}


