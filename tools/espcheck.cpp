// ESP-01 diagnostic. Not part of the main sketch (see [env:espcheck]).
//
// What it does, in order:
//   1. Finds the baud rate the ESP-01 answers at and sets it to 9600 if needed
//   2. Prints the firmware version
//   3. Scans and lists the WiFi networks the ESP-01 can see (2.4 GHz only)
//   4. Tries to join your network and tells you why it failed, if it did
//   5. Becomes a pass-through: type AT commands in the Serial Monitor
//
// Serial Monitor: 9600 baud.
#include <Arduino.h>
#include <SoftwareSerial.h>

// Same as the main sketch. Edit if your network details change.
#define TEST_SSID "YOUR_WIFI_SSID"
#define TEST_PASS "YOUR_WIFI_PASSWORD"

#define ESP_RX_PIN 2   // ESP TX -> D2
#define ESP_TX_PIN 3   // ESP RX -> D3 (through the divider)

SoftwareSerial esp(ESP_RX_PIN, ESP_TX_PIN);

uint16_t lastRxCount = 0;

// Send a command and stream the reply to the PC. Returns true if "OK" was seen.
// Stops early once OK/ERROR/FAIL has arrived and the module has gone quiet.
bool sendAndShow(const __FlashStringHelper* cmd, uint16_t waitMs, bool show = true) {
  while (esp.available()) esp.read();
  esp.print(cmd);
  esp.print(F("\r\n"));

  uint32_t start = millis(), lastRx = millis();
  bool ok = false, done = false;
  char prev = 0;
  lastRxCount = 0;

  while (millis() - start < waitMs) {
    while (esp.available()) {
      char c = esp.read();
      lastRx = millis();
      lastRxCount++;
      if (show) Serial.write(c);
      if (prev == 'O' && c == 'K') { ok = true; done = true; }
      prev = c;
    }
    if (done && millis() - lastRx > 200) break;
  }
  return ok;
}

bool replyAT(uint32_t baud) {
  esp.end();
  esp.begin(baud);
  delay(100);
  for (uint8_t i = 0; i < 2; i++) {
    if (sendAndShow(F("AT"), 800, false)) return true;
  }
  return false;
}

void setup() {
  Serial.begin(9600);
  delay(300);
  Serial.println(F("\n=== ESP-01 check ==="));

  // ---- 0. Listen for the module's boot noise ----
  // On power-up an ESP-01 spits out a burst of data. Seeing ANY bytes proves
  // the wire ESP UTXD -> Uno pin 2 works, even if they look like garbage.
  esp.begin(9600);
  Serial.println(F("\nListening for 8 s. NOW pull the ESP-01's VCC wire out and plug it back in."));
  uint32_t t0 = millis();
  uint16_t heard = 0;
  while (millis() - t0 < 8000) {
    if (esp.available()) { esp.read(); heard++; }
  }
  if (heard) {
    Serial.print(F("Heard "));
    Serial.print(heard);
    Serial.println(F(" bytes: UTXD -> pin 2 wire WORKS. If AT still fails, the problem is the pin 3 -> URXD side (divider)."));
  } else {
    Serial.println(F("Heard NOTHING: the module isn't sending, or UTXD isn't reaching pin 2 (check power/CH_PD/that wire)."));
  }

  // ---- 1. Find the baud rate ----
  const uint32_t bauds[] = {9600, 115200, 57600, 38400, 19200, 4800};
  uint32_t found = 0;
  bool sawGarbage = false;
  for (uint8_t i = 0; i < sizeof(bauds) / sizeof(bauds[0]); i++) {
    Serial.print(F("Trying "));
    Serial.print(bauds[i]);
    Serial.print(F(" baud... "));
    if (replyAT(bauds[i])) {
      Serial.println(F("OK"));
      found = bauds[i];
      break;
    }
    if (lastRxCount > 0) { Serial.println(F("got data but no 'OK' (wrong baud?)")); sawGarbage = true; }
    else                 { Serial.println(F("no reply")); }
  }

  if (!found) {
    Serial.println(F("\nRESULT: the ESP-01 never answered 'AT' with 'OK'."));
    if (sawGarbage) {
      Serial.println(F("It IS sending data, so wiring is probably fine, but the speed or"));
      Serial.println(F("firmware is unusual. Try AT+UART_DEF=9600,8,1,0,0 in pass-through below."));
    } else {
      Serial.println(F("It sent NOTHING. Check, in this order:"));
      Serial.println(F("  1. VCC AND CH_PD both on 3.3V (not 5V!), GND to Uno GND"));
      Serial.println(F("  2. ESP TX -> Uno D2, ESP RX -> Uno D3 (not swapped)"));
      Serial.println(F("  3. Power: red LED on the ESP lit? Try a 100uF cap across VCC/GND"));
      Serial.println(F("  4. Wire from Uno pin 3 goes through the 1k/2k divider to ESP RX"));
    }
    Serial.println(F("\nPass-through mode below (type AT commands).\n"));
    esp.end();
    esp.begin(9600);
    return;
  }

  if (found != 9600) {
    Serial.print(F("\nESP-01 is at "));
    Serial.print(found);
    Serial.println(F(" baud but the main sketch needs 9600. Switching it...."));
    if (!sendAndShow(F("AT+UART_DEF=9600,8,1,0,0"), 1500)) {
      sendAndShow(F("AT+CIOBAUD=9600"), 1500);   // older firmware
    }
    esp.end();
    esp.begin(9600);
    delay(300);
    if (replyAT(9600)) Serial.println(F("\nBaud rate is now 9600 and saved."));
    else Serial.println(F("\nCould not switch to 9600. Try AT+UART_DEF=9600,8,1,0,0 manually."));
  } else {
    Serial.println(F("\nBaud rate is 9600: matches the main sketch."));
  }

  // ---- 2. Firmware ----
  Serial.println(F("\n--- Firmware ---"));
  sendAndShow(F("AT+GMR"), 1500);

  // ---- 3. Scan for networks ----
  Serial.println(F("\n--- Networks the ESP-01 can see (2.4 GHz) ---"));
  sendAndShow(F("AT+CWMODE=1"), 1500, false);
  Serial.println(F("Scanning (up to 10 s)..."));
  sendAndShow(F("AT+CWLAP"), 10000);
  Serial.println(F("\nIs your network's name in that list, spelled EXACTLY the same?"));
  Serial.println(F("If it is missing, it's 5 GHz-only or out of range."));

  // ---- 4. Join test ----
  Serial.println(F("\n--- Joining \"" TEST_SSID "\" ---"));
  while (esp.available()) esp.read();
  esp.print(F("AT+CWJAP=\"" TEST_SSID "\",\"" TEST_PASS "\"\r\n"));
  uint32_t start = millis(), lastRx = millis();
  bool finished = false;
  char tail[6] = {0};
  while (millis() - start < 25000) {
    while (esp.available()) {
      char c = esp.read();
      Serial.write(c);
      lastRx = millis();
      memmove(tail, tail + 1, 4);
      tail[4] = c;
      if (strstr(tail, "OK") || strstr(tail, "FAIL")) finished = true;
    }
    if (finished && millis() - lastRx > 300) break;
  }
  Serial.println();
  Serial.println(F("How to read '+CWJAP:n':  1 = timeout, 2 = WRONG PASSWORD,"));
  Serial.println(F("                         3 = network NOT FOUND (name wrong / 5 GHz), 4 = connect failed"));
  Serial.println(F("'WIFI GOT IP' + 'OK' = it joined: the main sketch's WiFi settings are good."));

  Serial.println(F("\nPass-through mode: type AT commands below (e.g. AT, AT+CWLAP)\n"));
}

void loop() {
  static bool lastWasEol = true;
  while (esp.available()) Serial.write(esp.read());
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (!lastWasEol) esp.print(F("\r\n"));
      lastWasEol = true;
    } else {
      esp.write(c);
      lastWasEol = false;
    }
  }
}
