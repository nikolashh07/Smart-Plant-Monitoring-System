#pragma once
#include <Arduino.h>

struct HourForecast {          // one hour of the short-range forecast
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
  HourForecast hourly[4]; // this hour + the next 3
  int   rainProb = 0;   // today's max chance of rain, %
  float rainMm = 0;     // today's expected rain, mm
  float et0 = 3.0;      // today's reference evapotranspiration, mm
  float minC = 20;      // today's low, C
  float maxC = 20;      // today's high, C
};

extern Weather weather;

// Rain mode: pump locked out while raining and for RAIN_LOCK_MS afterwards.
struct RainState {
  bool seen = false;          // has it rained since boot?
  bool mode = false;          // currently in rain mode
  unsigned long lastMs = 0;   // millis() when rain was last seen
};
extern RainState rain;

// Local clock, taken from the weather reply and kept by millis().
struct LocalClock {
  bool valid = false;
  uint16_t syncMinOfDay = 0;  // minutes since local midnight at the last sync
  unsigned long syncMs = 0;   // millis() at the last sync
};
extern LocalClock localClock;
uint16_t localMinOfDay(unsigned long now);

// Where we are (fallback values from config.h, or the IP lookup).
struct Location {
  char name[22];
  char lat[10];
  char lon[11];
  bool known = false;
};
extern Location location;

bool fetchLocation();
bool fetchWeather();
const __FlashStringHelper* weatherText(int code);
