// Pure watering rules (see rules.h). Keep this file free of Arduino calls.
#include "config.h"
#include "rules.h"

static long clampLong(long v, long lo, long hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float clampFloat(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

int soilPercent(int raw) {
  long pct = (long)(SOIL_DRY_RAW - raw) * 100L / (SOIL_DRY_RAW - SOIL_FC_RAW);
  return (int)clampLong(pct, 0, 130);
}

bool rainForecastHolds(bool weatherValid, int rainProb, float rainMm) {
  return weatherValid && (rainProb >= RAIN_PROB_HOLD || rainMm >= RAIN_MM_HOLD);
}

bool freezeLockActive(bool weatherValid, float minC) {
  return weatherValid && minC <= FREEZE_LOCK_C;
}

float etScale(bool weatherValid, float et0) {
  if (!weatherValid) return 1.0f;
  return clampFloat(et0 / 3.0f, 0.7f, 1.5f);
}

unsigned long waterCheckInterval(bool weatherValid, float et0) {
  float et = weatherValid ? et0 : 3.0f;
  if (et >= 5.0f) return WATER_CHECK_MS / 2;
  if (et <= 2.0f) return WATER_CHECK_MS * 2;
  return WATER_CHECK_MS;
}

bool nextSessionActive(bool active, int soilPct) {
  if (!active && soilPct < SOIL_START_PCT) return true;
  if (active && soilPct >= SOIL_STOP_PCT)  return false;
  return active;
}

Pulse planPulse(float scale) {
  Pulse p;
  p.ml = (int)(PULSE_ML * scale + 0.5f);
  p.durationMs = (unsigned long)p.ml * 1000UL / PUMP_ML_PER_SEC;
  if (p.durationMs > MAX_PUMP_MS) p.durationMs = MAX_PUMP_MS;
  p.ml = (int)(p.durationMs * PUMP_ML_PER_SEC / 1000UL);   // what was really pumped
  return p;
}

PulseCheck checkPulse(const PulseInputs& in) {
  if (!in.active || in.fault) return PULSE_NOT_NEEDED;
  if (in.pulsesInSession >= MAX_PULSES_PER_SESSION) return PULSE_NEW_FAULT;
  if (in.freezeLock) return PULSE_FREEZE;
  if (in.mlToday >= MAX_ML_PER_DAY) return PULSE_DAILY_LIMIT;
  if (in.rainLikely && in.soilPct >= SOIL_CRITICAL_PCT) return PULSE_RAIN_HOLD;
  return PULSE_OK;
}
