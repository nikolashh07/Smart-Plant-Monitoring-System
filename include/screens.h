#pragma once
#include <Arduino.h>

#define SCREEN_COUNT 3

// `full` clears the display first; otherwise only the live parts are redrawn.
void drawScreen(uint8_t screen, bool full);
void printSerialStatus();
