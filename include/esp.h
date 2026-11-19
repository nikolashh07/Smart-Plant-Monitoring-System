#pragma once
#include <Arduino.h>
#include <SoftwareSerial.h>

// ESP-01 (AT firmware) link.
extern SoftwareSerial esp;

void espWriteP(const char* p);   // send a PROGMEM string
bool espWaitFor(const __FlashStringHelper* target, uint16_t timeoutMs);
void espFlush();
bool espCmd(const __FlashStringHelper* cmd, const __FlashStringHelper* expect, uint16_t timeoutMs);
bool wifiConnect();
