// Native unit tests for the pure watering rules. Run: pio test -e native
#include <unity.h>
#include "config.h"
#include "rules.h"

void setUp() {}
void tearDown() {}

// ---- soilPercent ------------------------------------------------------
void test_soil_percent_endpoints() {
  TEST_ASSERT_EQUAL(0,   soilPercent(SOIL_DRY_RAW));
  TEST_ASSERT_EQUAL(100, soilPercent(SOIL_FC_RAW));
}
void test_soil_percent_clamped() {
  TEST_ASSERT_EQUAL(0,   soilPercent(SOIL_DRY_RAW + 200));   // drier than dry mix
  TEST_ASSERT_EQUAL(130, soilPercent(SOIL_FC_RAW - 500));    // sensor in water
}

// ---- forecast rules ---------------------------------------------------
void test_rain_forecast_holds() {
  TEST_ASSERT_TRUE(rainForecastHolds(true, RAIN_PROB_HOLD, 0.0f));
  TEST_ASSERT_TRUE(rainForecastHolds(true, 0, RAIN_MM_HOLD));
  TEST_ASSERT_FALSE(rainForecastHolds(true, RAIN_PROB_HOLD - 1, 0.0f));
  TEST_ASSERT_FALSE(rainForecastHolds(false, 100, 50.0f));   // no weather data
}
void test_freeze_lock() {
  TEST_ASSERT_TRUE(freezeLockActive(true, FREEZE_LOCK_C));
  TEST_ASSERT_TRUE(freezeLockActive(true, FREEZE_LOCK_C - 5.0f));
  TEST_ASSERT_FALSE(freezeLockActive(true, FREEZE_LOCK_C + 0.5f));
  TEST_ASSERT_FALSE(freezeLockActive(false, -20.0f));
}

// ---- evaporation scaling ----------------------------------------------
void test_et_scale() {
  TEST_ASSERT_EQUAL_FLOAT(1.0f, etScale(false, 9.0f));   // no data -> neutral
  TEST_ASSERT_EQUAL_FLOAT(1.0f, etScale(true, 3.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.7f, etScale(true, 0.0f));    // lower clamp
  TEST_ASSERT_EQUAL_FLOAT(1.5f, etScale(true, 10.0f));   // upper clamp
}
void test_check_interval() {
  TEST_ASSERT_EQUAL_UINT32(WATER_CHECK_MS,     waterCheckInterval(true, 3.0f));
  TEST_ASSERT_EQUAL_UINT32(WATER_CHECK_MS / 2, waterCheckInterval(true, 5.0f));
  TEST_ASSERT_EQUAL_UINT32(WATER_CHECK_MS * 2, waterCheckInterval(true, 2.0f));
  TEST_ASSERT_EQUAL_UINT32(WATER_CHECK_MS,     waterCheckInterval(false, 9.0f));
}

// ---- session hysteresis -----------------------------------------------
void test_session_hysteresis() {
  TEST_ASSERT_TRUE(nextSessionActive(false, SOIL_START_PCT - 1));   // too dry: start
  TEST_ASSERT_FALSE(nextSessionActive(false, SOIL_START_PCT));      // not dry enough
  TEST_ASSERT_TRUE(nextSessionActive(true, SOIL_STOP_PCT - 1));     // keep watering
  TEST_ASSERT_FALSE(nextSessionActive(true, SOIL_STOP_PCT));        // wet enough: stop
}

// ---- pulse sizing -----------------------------------------------------
void test_plan_pulse_normal() {
  Pulse p = planPulse(1.0f);
  TEST_ASSERT_EQUAL(PULSE_ML, p.ml);
  TEST_ASSERT_EQUAL_UINT32((unsigned long)PULSE_ML * 1000UL / PUMP_ML_PER_SEC, p.durationMs);
}
void test_plan_pulse_capped() {
  Pulse p = planPulse(100.0f);                  // absurdly large request
  TEST_ASSERT_EQUAL_UINT32(MAX_PUMP_MS, p.durationMs);
  TEST_ASSERT_EQUAL((int)(MAX_PUMP_MS * PUMP_ML_PER_SEC / 1000UL), p.ml);
}

// ---- checkPulse -------------------------------------------------------
static PulseInputs okInputs() {
  PulseInputs in = {};
  in.active = true;
  in.soilPct = SOIL_START_PCT - 10;
  return in;
}
void test_pulse_ok() {
  TEST_ASSERT_EQUAL(PULSE_OK, checkPulse(okInputs()));
}
void test_pulse_not_needed() {
  PulseInputs in = okInputs();
  in.active = false;
  TEST_ASSERT_EQUAL(PULSE_NOT_NEEDED, checkPulse(in));
  in = okInputs();
  in.fault = true;
  TEST_ASSERT_EQUAL(PULSE_NOT_NEEDED, checkPulse(in));
}
void test_pulse_new_fault() {
  PulseInputs in = okInputs();
  in.pulsesInSession = MAX_PULSES_PER_SESSION;
  TEST_ASSERT_EQUAL(PULSE_NEW_FAULT, checkPulse(in));
}
void test_pulse_blocked_by_freeze_and_limit() {
  PulseInputs in = okInputs();
  in.freezeLock = true;
  TEST_ASSERT_EQUAL(PULSE_FREEZE, checkPulse(in));
  in = okInputs();
  in.mlToday = MAX_ML_PER_DAY;
  TEST_ASSERT_EQUAL(PULSE_DAILY_LIMIT, checkPulse(in));
}
void test_pulse_rain_hold_only_above_critical() {
  PulseInputs in = okInputs();
  in.rainLikely = true;
  in.soilPct = SOIL_CRITICAL_PCT;               // still moist enough to wait
  TEST_ASSERT_EQUAL(PULSE_RAIN_HOLD, checkPulse(in));
  in.soilPct = SOIL_CRITICAL_PCT - 1;           // critically dry: water anyway
  TEST_ASSERT_EQUAL(PULSE_OK, checkPulse(in));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_soil_percent_endpoints);
  RUN_TEST(test_soil_percent_clamped);
  RUN_TEST(test_rain_forecast_holds);
  RUN_TEST(test_freeze_lock);
  RUN_TEST(test_et_scale);
  RUN_TEST(test_check_interval);
  RUN_TEST(test_session_hysteresis);
  RUN_TEST(test_plan_pulse_normal);
  RUN_TEST(test_plan_pulse_capped);
  RUN_TEST(test_pulse_ok);
  RUN_TEST(test_pulse_not_needed);
  RUN_TEST(test_pulse_new_fault);
  RUN_TEST(test_pulse_blocked_by_freeze_and_limit);
  RUN_TEST(test_pulse_rain_hold_only_above_critical);
  return UNITY_END();
}
