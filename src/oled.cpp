#include <Wire.h>
#include "config.h"
#include "font.h"
#include "oled.h"

// =====================================================================
// OLED driver
// =====================================================================
void oledCommand(uint8_t c) {
  Wire.beginTransmission(OLED_ADDR);
  Wire.write(0x00);
  Wire.write(c);
  Wire.endTransmission();
}

void oledSetCursor(uint8_t col, uint8_t page) {
  oledCommand(0xB0 + page);
  oledCommand(0x00 + (col & 0x0F));
  oledCommand(0x10 + ((col >> 4) & 0x0F));
}

void oledClear() {
  for (uint8_t page = 0; page < 8; page++) {
    oledSetCursor(0, page);
    for (uint8_t col = 0; col < 128; col += 16) {
      Wire.beginTransmission(OLED_ADDR);
      Wire.write(0x40);
      for (uint8_t i = 0; i < 16; i++) { Wire.write(0x00); }
      Wire.endTransmission();
    }
  }
}

void oledInit() {
  Wire.begin();
  Wire.setWireTimeout(5000, true);   // never hang if the OLED/bus misbehaves
  uint8_t initSeq[] = {
    0xAE, 0x20, 0x00, 0x21, 0x00, 0x7F, 0x22, 0x00, 0x07, 0x40,
    0xA1, 0xC8, 0xA8, 0x3F, 0xD3, 0x00, 0xD5, 0x80, 0xD9, 0xF1,
    0xDA, 0x12, 0xDB, 0x40, 0x8D, 0x14, 0xA4, 0xA6, 0xAF
  };
  for (uint8_t i = 0; i < sizeof(initSeq); i++) {
    oledCommand(initSeq[i]);
  }
}

void oledPrintChar(char c) {
  if (c < 32 || c > 122) return;
  Wire.beginTransmission(OLED_ADDR);
  Wire.write(0x40);
  for (uint8_t i = 0; i < 5; i++) {
    Wire.write(pgm_read_byte(&font5x8[c - 32][i]));
  }
  Wire.write(0x00);
  Wire.endTransmission();
}

// Print a RAM string padded to 21 chars (overwrites old text, no flicker).
void oledPrintLine(const char* str, uint8_t page) {
  oledSetCursor(0, page);
  uint8_t n = 0;
  while (*str && n < 21) { oledPrintChar(*str++); n++; }
  while (n < 21) { oledPrintChar(' '); n++; }
}

// Same, but for F("...") strings so they don't eat the Uno's 2KB of RAM.
void oledPrintLine(const __FlashStringHelper* fs, uint8_t page) {
  const char* p = (const char*)fs;
  oledSetCursor(0, page);
  uint8_t n = 0;
  char c;
  while ((c = pgm_read_byte(p++)) && n < 21) { oledPrintChar(c); n++; }
  while (n < 21) { oledPrintChar(' '); n++; }
}

static uint8_t expandNibble(uint8_t nib) {
  uint8_t r = 0;
  for (uint8_t i = 0; i < 4; i++) if (nib & (1 << i)) r |= 3 << (2 * i);
  return r;
}

// 2x-scaled text, occupies `page` and `page + 1`.
void oledPrintBig(const char* str, uint8_t col, uint8_t page) {
  for (uint8_t half = 0; half < 2; half++) {
    oledSetCursor(col, page + half);
    for (const char* s = str; *s; s++) {
      if (*s < 32 || *s > 122) continue;
      Wire.beginTransmission(OLED_ADDR);
      Wire.write(0x40);
      for (uint8_t i = 0; i < 5; i++) {
        uint8_t b = pgm_read_byte(&font5x8[*s - 32][i]);
        uint8_t v = expandNibble(half ? (b >> 4) : (b & 0x0F));
        Wire.write(v); Wire.write(v);
      }
      Wire.write(0x00); Wire.write(0x00);
      Wire.endTransmission();
    }
  }
}

// ---- Text helpers -----------------------------------------------------
// Text at (col, page), padded with spaces to `width` chars (6 px each).
void oledTextAt(const char* s, uint8_t col, uint8_t page, uint8_t width) {
  oledSetCursor(col, page);
  uint8_t n = 0;
  while (*s && n < width) { oledPrintChar(*s++); n++; }
  while (n < width) { oledPrintChar(' '); n++; }
}

void oledTextAt(const __FlashStringHelper* fs, uint8_t col, uint8_t page, uint8_t width) {
  const char* p = (const char*)fs;
  oledSetCursor(col, page);
  uint8_t n = 0;
  char c;
  while ((c = pgm_read_byte(p++)) && n < width) { oledPrintChar(c); n++; }
  while (n < width) { oledPrintChar(' '); n++; }
}

// Centre text on pixel column cx.
void oledCenter(const char* s, uint8_t cx, uint8_t page) {
  uint8_t half = strlen(s) * 3;
  oledSetCursor(cx > half ? cx - half : 0, page);
  while (*s) oledPrintChar(*s++);
}

// "Label ............ value" across the full 21-char width.
void oledRow(const __FlashStringHelper* label, const char* value, uint8_t page) {
  char line[22];
  memset(line, ' ', 21);
  line[21] = 0;
  strncpy_P(line, (const char*)label, 21);
  for (uint8_t i = 0; i < 21; i++) if (line[i] == 0) line[i] = ' ';
  uint8_t vl = strlen(value);
  if (vl > 21) vl = 21;
  memcpy(line + 21 - vl, value, vl);
  oledPrintLine(line, page);
}

void oledDivider(uint8_t page) {
  oledSetCursor(0, page);
  for (uint8_t c = 0; c < 128; c += 16) {
    Wire.beginTransmission(OLED_ADDR);
    Wire.write(0x40);
    for (uint8_t i = 0; i < 16; i++) Wire.write(0x08); // 1px line mid-page
    Wire.endTransmission();
  }
}

// Horizontal bar, 102 px wide, one page tall. pct 0-100.
void oledBar(uint8_t page, int pct) {
  pct = constrain(pct, 0, 100);
  oledSetCursor(0, page);
  for (uint8_t c0 = 0; c0 < 102; c0 += 17) {
    Wire.beginTransmission(OLED_ADDR);
    Wire.write(0x40);
    for (uint8_t c = c0; c < c0 + 17; c++) {
      uint8_t v = (c == 0 || c == 101 || c <= pct) ? 0x7E : 0x42;
      Wire.write(v);
    }
    Wire.endTransmission();
  }
}
