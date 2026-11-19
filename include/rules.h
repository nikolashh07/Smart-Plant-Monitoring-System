#pragma once
#include <stdint.h>

// Watering rules as pure functions: inputs in, answer out. No hardware and no
// globals, so they run in the native unit tests (test/test_rules).

// Capacitive sensor reading -> soil %, 0 = dry mix, 100 = field capacity.
// Can read a bit over 100 when soggy.
int soilPercent(int raw);

// True when the forecast says to hold off watering (rain likely today).
bool rainForecastHolds(bool weatherValid, int rainProb, float rainMm);

// True when today's forecast low is cold enough to lock the pump out.
bool freezeLockActive(bool weatherValid, float minC);

// Hot, dry, windy day -> bigger pulses (up to 1.5x); cool day -> smaller (0.7x).
float etScale(bool weatherValid, float et0);

// How often to check the soil: more often on hot days, less on cool ones.
unsigned long waterCheckInterval(bool weatherValid, float et0);

// Session hysteresis: starts below SOIL_START_PCT, ends at SOIL_STOP_PCT.
bool nextSessionActive(bool active, int soilPct);

// One pump run, sized by `scale` and capped at MAX_PUMP_MS.
struct Pulse {
  int ml;
  unsigned long durationMs;
};
Pulse planPulse(float scale);

// Everything that decides whether another pulse may start.
struct PulseInputs {
  bool active;            // a watering session is in progress
  bool fault;             // an earlier fault is still latched
  int  pulsesInSession;
  bool freezeLock;
  int  mlToday;
  bool rainLikely;
  int  soilPct;
};

enum PulseCheck : uint8_t {
  PULSE_OK,
  PULSE_NOT_NEEDED,       // no session, or a fault is already latched
  PULSE_NEW_FAULT,        // too many pulses and the soil didn't respond
  PULSE_FREEZE,
  PULSE_DAILY_LIMIT,
  PULSE_RAIN_HOLD
};
PulseCheck checkPulse(const PulseInputs& in);
