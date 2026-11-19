// Soil reading, watering rules and the pump state machine.
#include "config.h"
#include "rules.h"
#include "weather.h"
#include "watering.h"

Soil soil;
Watering watering;

// Private to this file: pump timing and the current session.
static unsigned long stateSinceMs = 0, lastCheckMs = 0, pulseDurationMs = 0;
static bool checkSoon = true;
static uint8_t pulsesInSession = 0;
static int lastPulseMl = 0;

void pumpOn()  { digitalWrite(PUMP_PIN, PUMP_ON); }
void pumpOff() { digitalWrite(PUMP_PIN, PUMP_OFF); }

int readSoilRaw() {
  long sum = 0;
  for (uint8_t i = 0; i < 16; i++) { sum += analogRead(A0); delay(2); }
  return (int)(sum / 16);
}

const __FlashStringHelper* soilLabel() {
  if (soil.pct < SOIL_START_PCT) return F("DRY");
  if (soil.pct > 108)       return F("WET");
  return F("GOOD");
}

// A pulse is running: stop the pump once its time is up.
static void finishPulseIfDone(unsigned long now) {
  if (now - stateSinceMs < pulseDurationMs) return;
  pumpOff();
  watering.mlToday += lastPulseMl;
  pulsesInSession++;
  watering.pump = PUMP_SOAKING;
  stateSinceMs = now;
}

// Water is spreading through the soil: wait, then check the soil again.
static void waitForSoak(unsigned long now) {
  if (now - stateSinceMs < SOAK_MS) return;
  watering.pump = PUMP_IDLE;
  checkSoon = true;
}

// Start a session when the soil gets too dry, end it once it is wet enough,
// and clear any pump fault when the soil recovers.
static void updateSession() {
  bool next = nextSessionActive(watering.active, soil.pct);
  if (next == watering.active) return;
  watering.active = next;
  pulsesInSession = 0;
  if (!next) watering.fault = false;
}

// True if every safety rule allows another pulse right now.
// Also records why not (rainHold / fault) for the status line.
static bool pulseAllowed() {
  PulseInputs in = { watering.active, watering.fault, pulsesInSession, watering.freezeLock,
                     watering.mlToday, rainForecastHolds(weather.valid, weather.rainProb, weather.rainMm),
                     soil.pct };
  PulseCheck check = checkPulse(in);
  watering.rainHold = (check == PULSE_RAIN_HOLD);
  if (check == PULSE_NEW_FAULT) watering.fault = true;
  return check == PULSE_OK;
}

// Size the pulse for today's evaporation (capped by MAX_PUMP_MS) and turn the pump on.
static void startPulse(unsigned long now) {
  Pulse p = planPulse(etScale(weather.valid, weather.et0));
  lastPulseMl = p.ml;
  pulseDurationMs = p.durationMs;
  pumpOn();
  watering.pump = PUMP_RUNNING;
  stateSinceMs = now;
}

void waterLogic(unsigned long now) {
#if CAL_MODE
  pumpOff();
  return;
#endif
  // Rain mode: pump locked out, soil readings ignored, drainage takes over.
  if (rain.mode) {
    pumpOff();
    watering.pump = PUMP_IDLE;
    watering.rainHold = false;
    return;
  }
  if (watering.pump == PUMP_RUNNING) { finishPulseIfDone(now); return; }
  if (watering.pump == PUMP_SOAKING) { waitForSoak(now);       return; }

  // Idle: decide whether to start a pulse.
  if (!checkSoon && now - lastCheckMs < waterCheckInterval(weather.valid, weather.et0)) return;
  checkSoon = false;
  lastCheckMs = now;

  watering.freezeLock = freezeLockActive(weather.valid, weather.minC);
  updateSession();
  if (pulseAllowed()) startPulse(now);
}

// Reset the pump's daily tracking: millilitres pumped today, the daily limit
// and any pump fault from yesterday. Rain mode is NOT cleared (it follows the weather).
void midnightReset(unsigned long now) {
  watering.dayStartMs = now;
  watering.mlToday = 0;
  watering.fault = false;
  pulsesInSession = 0;
  checkSoon = true;
  Serial.println(F("*** Midnight reset: pump counters cleared ***"));
}
