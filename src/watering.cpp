#include "config.h"
#include "weather.h"
#include "watering.h"

PumpState pumpState = P_IDLE;
unsigned long dayStart = 0;
static unsigned long stateSince = 0, lastCheck = 0, pulseMs = 0;
static bool checkNow = true;
bool session = false;     // true from "too dry" until "wet enough" (hysteresis)
bool fault = false;       // soil didn't respond to water: pump/reservoir problem
bool holdRain = false;
bool freezeLock = false;
static uint8_t pulsesInSession = 0;
static int lastPulseMl = 0;
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

static bool rainLikely() {
  return weather.valid && (weather.rainProb >= RAIN_PROB_HOLD || weather.rainMm >= RAIN_MM_HOLD);
}

// Hot, dry, windy day -> bigger pulses; cool day -> smaller.
static float etScale() {
  if (!weather.valid) return 1.0;
  float s = weather.et0 / 3.0;
  return constrain(s, 0.7, 1.5);
}

static unsigned long waterCheckMs() {
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
