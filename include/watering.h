#pragma once
#include <Arduino.h>

enum PumpState : uint8_t { PUMP_IDLE, PUMP_RUNNING, PUMP_SOAKING };

extern PumpState pumpState;
extern unsigned long dayStartMs;
extern bool wateringActive;      // true from "too dry" until "wet enough" (hysteresis)
extern bool pumpFault;        // soil didn't respond to water: pump/reservoir problem
extern bool rainHold;
extern bool freezeLock;
extern int mlPumpedToday;
extern int soilRaw;
extern int soilPct;

void pumpOn();
void pumpOff();
int readSoilRaw();
int rawToPercent(int raw);
const __FlashStringHelper* soilLabel();
void waterLogic(unsigned long now);
void midnightReset(unsigned long now);
