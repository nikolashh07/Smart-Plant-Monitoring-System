#include "config.h"
#include "esp.h"

SoftwareSerial esp(ESP_RX_PIN, ESP_TX_PIN);

void espWriteP(const char* p) {
  char c;
  while ((c = pgm_read_byte(p++))) esp.write(c);
}

bool espWaitFor(const __FlashStringHelper* target, uint16_t timeoutMs) {
  char buf[16];
  uint8_t len = strlen_P((const char*)target);
  if (len >= sizeof(buf)) return false;
  strcpy_P(buf, (const char*)target);
  esp.setTimeout(timeoutMs);
  return esp.find(buf);
}

void espFlush() { while (esp.available()) esp.read(); }

bool espCmd(const __FlashStringHelper* cmd, const __FlashStringHelper* expect, uint16_t timeoutMs) {
  espFlush();
  esp.println(cmd);
  return espWaitFor(expect, timeoutMs);
}

bool wifiConnect() {
  if (!espCmd(F("AT"), F("OK"), 1000)) return false;
  if (!espCmd(F("AT+CWMODE=1"), F("OK"), 2000)) return false;
  espFlush();
  esp.print(F("AT+CWJAP=\""));
  esp.print(F(WIFI_SSID));
  esp.print(F("\",\""));
  esp.print(F(WIFI_PASS));
  esp.println(F("\""));
  return espWaitFor(F("OK"), 15000);
}
