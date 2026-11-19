#pragma once
#include <Arduino.h>

// SSD1306 128x64 OLED driver and text/shape helpers (pages are 8 px tall rows).
void oledInit();
void oledClear();
void oledSetCursor(uint8_t col, uint8_t page);

void oledPrintLine(const char* str, uint8_t page);
void oledPrintLine(const __FlashStringHelper* fs, uint8_t page);
void oledPrintBig(const char* str, uint8_t col, uint8_t page);

void oledTextAt(const char* s, uint8_t col, uint8_t page, uint8_t width);
void oledTextAt(const __FlashStringHelper* fs, uint8_t col, uint8_t page, uint8_t width);
void oledCenter(const char* s, uint8_t cx, uint8_t page);
void oledRow(const __FlashStringHelper* label, const char* value, uint8_t page);
void oledDivider(uint8_t page);
void oledBar(uint8_t page, int pct);
