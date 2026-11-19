#include <Wire.h>
#include "config.h"
#include "oled.h"
#include "icons.h"

// ---- Weather icons, drawn at runtime (no bitmap tables) ---------------
// Shapes are defined on a 24x24 grid and sampled at 16 or 24 px.
enum : uint8_t { ICON_SUN, ICON_PART, ICON_CLOUD, ICON_FOG, ICON_RAIN, ICON_SNOW, ICON_STORM };

static bool inCircle(float x, float y, float cx, float cy, float r) {
  float dx = x - cx, dy = y - cy;
  return dx * dx + dy * dy <= r * r;
}

static bool segHit(float x, float y, float x1, float y1, float x2, float y2, float w2) {
  float vx = x2 - x1, vy = y2 - y1, wx = x - x1, wy = y - y1;
  float t = (wx * vx + wy * vy) / (vx * vx + vy * vy);
  t = constrain(t, 0.0f, 1.0f);
  float dx = wx - t * vx, dy = wy - t * vy;
  return dx * dx + dy * dy <= w2;
}

static bool cloudHit(float x, float y, float oy, float pad) {
  y -= oy;
  if (inCircle(x, y, 8, 14, 5 + pad))      return true;
  if (inCircle(x, y, 13, 10.5, 6.5 + pad)) return true;
  if (inCircle(x, y, 18.5, 14.5, 4.5 + pad)) return true;
  return x >= 8 && x <= 18.5 && y >= 14 && y <= 19 + pad;
}

static bool sunHit(float x, float y, float cx, float cy, float r, float rin, float rout) {
  float dx = x - cx, dy = y - cy, d2 = dx * dx + dy * dy;
  if (d2 <= r * r) return true;
  if (d2 < rin * rin || d2 > rout * rout) return false;
  if (fabsf(dx) < 1.0f || fabsf(dy) < 1.0f) return true;         // N/S/E/W rays
  return fabsf(dx - dy) < 1.5f || fabsf(dx + dy) < 1.5f;         // diagonal rays
}

static bool iconPixel(uint8_t t, float x, float y) {
  switch (t) {
    case ICON_SUN:   return sunHit(x, y, 12, 12, 5, 7.5, 11);
    case ICON_PART:  return cloudHit(x, y, 3, 0) ||
                          (sunHit(x, y, 8, 8, 4, 6, 8.5) && !cloudHit(x, y, 3, 1.6));
    case ICON_CLOUD: return cloudHit(x, y, 0.5, 0);
    case ICON_FOG:   return x >= 3 && x <= 21 &&
                          (fabsf(y - 6) < 1 || fabsf(y - 11) < 1 || fabsf(y - 16) < 1 || fabsf(y - 21) < 1);
    case ICON_RAIN:  return cloudHit(x, y, -2, 0) ||
                          segHit(x, y, 8, 19, 7, 23, 0.9) || segHit(x, y, 13, 19, 12, 23, 0.9) ||
                          segHit(x, y, 18, 19, 17, 23, 0.9);
    case ICON_SNOW:  return cloudHit(x, y, -2, 0) ||
                          inCircle(x, y, 7, 20, 1.4) || inCircle(x, y, 12, 22.5, 1.4) ||
                          inCircle(x, y, 17, 20, 1.4);
    case ICON_STORM: return cloudHit(x, y, -2, 0) ||
                          segHit(x, y, 14, 18, 11, 21, 1.0) || segHit(x, y, 11, 21, 15, 21, 1.0) ||
                          segHit(x, y, 15, 21, 12, 24, 1.0);
  }
  return false;
}

uint8_t iconFor(int c) {
  if (c <= 1)  return ICON_SUN;
  if (c == 2)  return ICON_PART;
  if (c == 3)  return ICON_CLOUD;
  if (c <= 48) return ICON_FOG;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return ICON_SNOW;
  if (c >= 95) return ICON_STORM;
  return ICON_RAIN;
}

// size = 16 or 24 px square; occupies size/8 pages starting at `page`.
void oledIcon(uint8_t type, uint8_t size, uint8_t col, uint8_t page) {
  uint8_t buf[72];
  memset(buf, 0, sizeof(buf));
  for (uint8_t tx = 0; tx < size; tx++) {
    float x = (tx + 0.5f) * 24.0f / size;
    for (uint8_t ty = 0; ty < size; ty++) {
      float y = (ty + 0.5f) * 24.0f / size;
      if (iconPixel(type, x, y)) buf[(ty >> 3) * size + tx] |= 1 << (ty & 7);
    }
  }
  for (uint8_t p = 0; p < size / 8; p++) {
    oledSetCursor(col, page + p);
    for (uint8_t c = 0; c < size; c += 8) {
      Wire.beginTransmission(OLED_ADDR);
      Wire.write(0x40);
      for (uint8_t i = 0; i < 8; i++) Wire.write(buf[p * size + c + i]);
      Wire.endTransmission();
    }
  }
}
