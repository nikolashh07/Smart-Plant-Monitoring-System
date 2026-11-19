#pragma once
#include <Arduino.h>

// Weather icon for a WMO weather code, drawn at runtime.
uint8_t iconFor(int weatherCode);
// size = 16 or 24 px square; occupies size/8 pages starting at `page`.
void oledIcon(uint8_t type, uint8_t size, uint8_t col, uint8_t page);
