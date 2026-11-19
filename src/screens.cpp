#include <Arduino.h>
#include "config.h"
#include "oled.h"
#include "icons.h"
#include "weather.h"
#include "watering.h"
#include "screens.h"

// =====================================================================
// One-line status text, shared by the OLED bottom line and the Serial Monitor.
static void buildStatus(char* out, size_t n) {
#if CAL_MODE
  snprintf_P(out, n, PSTR("CAL raw %d  %d%%"), soilRaw, soilPct);
  return;
#endif
  if (rainMode)                     strncpy_P(out, PSTR("RAIN MODE: pump off"), n);
  else if (pumpFault)                   strncpy_P(out, PSTR("CHECK PUMP / WATER"), n);
  else if (pumpState == PUMP_RUNNING)  strncpy_P(out, PSTR("Watering..."), n);
  else if (pumpState == PUMP_SOAKING)  strncpy_P(out, PSTR("Soaking in..."), n);
  else if (freezeLock && wateringActive)   strncpy_P(out, PSTR("Freeze: pump off"), n);
  else if (rainHold)                strncpy_P(out, PSTR("Rain soon: holding"), n);
  else if (wateringActive && mlPumpedToday >= MAX_ML_PER_DAY) strncpy_P(out, PSTR("Daily limit hit"), n);
  else snprintf_P(out, n, PSTR("Soil %d%% %S"), soilPct, (const char*)soilLabel());
  out[n - 1] = 0;
}

static void drawStatusLine() {
  char line[24];
  buildStatus(line, sizeof(line));
  oledPrintLine(line, 7);
}

// ---- Screens ----------------------------------------------------------
// 1) NOW: icon, big temperature, condition, humidity, wind
static void drawNowScreen() {
  char a[12], v[12];
  oledTextAt(locationName, 0, 0, rainMode ? 11 : 21);
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
static void drawForecastScreen() {
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
      uint8_t h = weather.hourly[i].hour;
      snprintf_P(v, sizeof(v), PSTR("%d%S"), h % 12 == 0 ? 12 : h % 12,
                 (const char*)(h < 12 ? PSTR("AM") : PSTR("PM")));
    }
    oledCenter(v, cx, 1);
    oledIcon(iconFor(weather.hourly[i].code), 16, cx - 8, 2);   // pages 2-3
    snprintf_P(v, sizeof(v), PSTR("%dC"), weather.hourly[i].tempC);
    oledCenter(v, cx, 4);
    snprintf_P(v, sizeof(v), PSTR("%d%%"), weather.hourly[i].rainProb);
    oledCenter(v, cx, 5);
  }
  snprintf_P(v, sizeof(v), PSTR("%d / %dC"), (int)lroundf(weather.maxC), (int)lroundf(weather.minC));
  oledRow(F("Today high / low"), v, 6);
}

// 3) GARDEN: soil level, bar, rain and watering totals
static void drawGardenScreen() {
  char a[10], v[14];
  oledPrintLine(F("RADISH BED"), 0);

  snprintf_P(a, sizeof(a), PSTR("%d%%"), min(soilPct, 100));
  uint8_t n = strlen(a);
  while (n < 5) a[n++] = ' ';       // blank-pad so old digits get erased
  a[n] = 0;
  oledPrintBig(a, 0, 1);                                    // pages 1-2
  if (rainMode) oledTextAt(F("(ignored)"), 72, 1, 9);
  else          oledTextAt(soilLabel(), 78, 1, 5);
  snprintf_P(v, sizeof(v), PSTR("goal %d-%d%%"), SOIL_START_PCT, SOIL_STOP_PCT);
  oledTextAt(v, 60, 2, 11);
  oledBar(3, soilPct);

  if (weather.valid) {
    dtostrf(weather.rainMm, 1, 1, a);
    snprintf_P(v, sizeof(v), PSTR("%d%% %smm"), weather.rainProb, a);
  } else {
    strcpy_P(v, PSTR("no data"));
  }
  oledRow(F("Rain today"), v, 4);
  snprintf_P(v, sizeof(v), PSTR("%d mL"), mlPumpedToday);
  oledRow(F("Pumped today"), v, 5);
  if (weather.valid) {
    dtostrf(weather.et0, 1, 1, a);
    snprintf_P(v, sizeof(v), PSTR("%s mm"), a);
  } else {
    strcpy_P(v, PSTR("--"));
  }
  oledRow(F("Evaporation"), v, 6);
}


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
static void printSerialWeather() {
  if (!weather.valid) {
    Serial.println(F("Weather:  no data"));
    return;
  }
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
    else        { Serial.print(weather.hourly[i].hour); Serial.print('h'); }
    Serial.print(' '); Serial.print(weather.hourly[i].tempC);
    Serial.print(F("C ")); Serial.print(weather.hourly[i].rainProb); Serial.print('%');
    if (i < 3) Serial.print(F(" | "));
  }
  Serial.println();
}

static void printSerialClock() {
  if (!clockValid) return;
  uint16_t m = localMinOfDay(millis());
  Serial.print(F("Time:     ")); Serial.print(m / 60); Serial.print(':');
  if (m % 60 < 10) Serial.print('0');
  Serial.print(m % 60); Serial.println(F(" (local, resets pump counters at 00:00)"));
}

void printSerialStatus() {
  char st[24];
  buildStatus(st, sizeof(st));

  Serial.println(F("------------------------------------------"));
  Serial.print(F("Soil:     ")); Serial.print(min(soilPct, 100)); Serial.print(F("%  "));
  Serial.print(soilLabel());
  Serial.print(F("   (raw ")); Serial.print(soilRaw); Serial.println(')');
  Serial.print(F("Status:   ")); Serial.println(st);

  printSerialWeather();
  Serial.print(F("Pumped:   ")); Serial.print(mlPumpedToday); Serial.println(F(" mL today"));
  printSerialClock();
}
