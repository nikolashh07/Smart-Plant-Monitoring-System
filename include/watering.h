#pragma once
#include <Arduino.h>

enum PumpState : uint8_t { PUMP_IDLE, PUMP_RUNNING, PUMP_SOAKING };

struct Soil {
  int raw = 0;                // averaged sensor reading
  int pct = 0;                // 0 = dry mix, 100 = field capacity
};

struct Watering {
  PumpState pump = PUMP_IDLE;
  bool active = false;        // true from "too dry" until "wet enough" (hysteresis)
  bool fault = false;         // soil didn't respond to water: pump/reservoir problem
  bool rainHold = false;      // holding off because rain is forecast
  bool freezeLock = false;    // pump locked out by a freezing forecast
  int  mlToday = 0;           // water pumped since the last midnight reset
  unsigned long dayStartMs = 0;   // when the daily counters last reset
};

extern Soil soil;
extern Watering watering;

void pumpOn();
void pumpOff();
int readSoilRaw();
int rawToPercent(int raw);
const __FlashStringHelper* soilLabel();
void waterLogic(unsigned long now);
void midnightReset(unsigned long now);
