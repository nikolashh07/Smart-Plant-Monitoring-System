#pragma once
// All user-editable settings live here.

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
#define FALLBACK_LOCATION_NAME "Austin, Texas"   // top line of weather screen (max 21 chars)
#define FALLBACK_LATITUDE      "30.27"
#define FALLBACK_LONGITUDE     "-97.74"

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
#define SOIL_START_PCT      75   // start watering below this
#define SOIL_STOP_PCT       95   // stop once soil reaches this
#define SOIL_CRITICAL_PCT   50   // below this, water even if rain is forecast

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
