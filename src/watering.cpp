// Soil reading, watering rules and the pump state machine.
#include "config.h"
#include "weather.h"
#include "watering.h"

PumpState pumpState = PUMP_IDLE;
unsigned long dayStartMs = 0;                 // when the daily counters last reset
static unsigned long stateSinceMs = 0, lastCheckMs = 0, pulseDurationMs = 0;
static bool checkSoon = true;
bool wateringActive = false;
bool pumpFault = false;
bool rainHold = false;
bool freezeLock = false;
static uint8_t pulsesInSession = 0;
static int lastPulseMl = 0;
int mlPumpedToday = 0;
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
  if (soilPct < SOIL_START_PCT) return F("DRY");
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

// A pulse is running: stop the pump once its time is up.
static void finishPulseIfDone(unsigned long now) {
  if (now - stateSinceMs < pulseDurationMs) return;
  pumpOff();
  mlPumpedToday += lastPulseMl;
  pulsesInSession++;
  pumpState = PUMP_SOAKING;
  stateSinceMs = now;
}

// Water is spreading through the soil: wait, then check the soil again.
static void waitForSoak(unsigned long now) {
  if (now - stateSinceMs < SOAK_MS) return;
  pumpState = PUMP_IDLE;
  checkSoon = true;
}

// Start a session when the soil gets too dry, end it once it is wet enough
// (hysteresis), and clear any pump fault when the soil recovers.
static void updateSession() {
  if (!wateringActive && soilPct < SOIL_START_PCT) { wateringActive = true; pulsesInSession = 0; }
  if (wateringActive && soilPct >= SOIL_STOP_PCT)  { wateringActive = false; pulsesInSession = 0; pumpFault = false; }
}

// True if every safety rule allows another pulse right now.
// Also records why not (rainHold / pumpFault) for the status line.
static bool pulseAllowed() {
  rainHold = false;
  if (!wateringActive || pumpFault) return false;
  if (pulsesInSession >= MAX_PULSES_PER_SESSION) { pumpFault = true; return false; }
  if (freezeLock) return false;
  if (mlPumpedToday >= MAX_ML_PER_DAY) return false;
  if (rainLikely() && soilPct >= SOIL_CRITICAL_PCT) { rainHold = true; return false; }
  return true;
}

// Size the pulse for today's evaporation (capped by MAX_PUMP_MS) and turn the pump on.
static void startPulse(unsigned long now) {
  lastPulseMl = (int)(PULSE_ML * etScale() + 0.5);
  pulseDurationMs = (unsigned long)lastPulseMl * 1000UL / PUMP_ML_PER_SEC;
  if (pulseDurationMs > MAX_PUMP_MS) pulseDurationMs = MAX_PUMP_MS;
  lastPulseMl = (int)(pulseDurationMs * PUMP_ML_PER_SEC / 1000UL);
  pumpOn();
  pumpState = PUMP_RUNNING;
  stateSinceMs = now;
}

void waterLogic(unsigned long now) {
#if CAL_MODE
  pumpOff();
  return;
#endif
  // Rain mode: pump locked out, soil readings ignored, drainage takes over.
  if (rainMode) {
    pumpOff();
    pumpState = PUMP_IDLE;
    rainHold = false;
    return;
  }
  if (pumpState == PUMP_RUNNING) { finishPulseIfDone(now); return; }
  if (pumpState == PUMP_SOAKING) { waitForSoak(now);       return; }

  // Idle: decide whether to start a pulse.
  if (!checkSoon && now - lastCheckMs < waterCheckMs()) return;
  checkSoon = false;
  lastCheckMs = now;

  freezeLock = weather.valid && weather.minC <= FREEZE_LOCK_C;
  updateSession();
  if (pulseAllowed()) startPulse(now);
}

// Reset the pump's daily tracking: millilitres pumped today, the daily limit
// and any pump fault from yesterday. Rain mode is NOT cleared (it follows the weather).
void midnightReset(unsigned long now) {
  dayStartMs = now;
  mlPumpedToday = 0;
  pumpFault = false;
  pulsesInSession = 0;
  checkSoon = true;
  Serial.println(F("*** Midnight reset: pump counters cleared ***"));
}
