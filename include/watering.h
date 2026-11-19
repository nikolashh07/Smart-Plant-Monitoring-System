#pragma once
#include <Arduino.h>

enum PumpState : uint8_t { P_IDLE, P_PUMPING, P_SOAKING };

extern PumpState pumpState;
extern unsigned long dayStart;
extern bool session;      // true from "too dry" until "wet enough" (hysteresis)
extern bool fault;        // soil didn't respond to water: pump/reservoir problem
extern bool holdRain;
extern bool freezeLock;
extern int mlToday;
extern int soilRaw;
extern int soilPct;

void pumpOn();
void pumpOff();
int readSoilRaw();
int rawToPercent(int raw);
const __FlashStringHelper* soilLabel();
void waterLogic(unsigned long now);
void midnightReset(unsigned long now);
