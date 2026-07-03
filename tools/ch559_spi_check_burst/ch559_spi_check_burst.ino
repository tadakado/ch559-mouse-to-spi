// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tadashi Kadowaki
// Part of the CH559 USB mouse host project (distributed under GPLv3).
//
// Arduino Nano 33 BLE (nRF52840) SPI-master *burst / click-latch* check for the
// CH559 mouse-host slave. Same wiring and frame format as ch559_spi_check, but
// the master reads SLOWLY (once per READ_INTERVAL_MS) on purpose: that is the
// only way to reproduce the condition the button-edge latch fixes, namely a
// press+release that completes between two master reads.
//
// With fast (every-loop) reads, a hand click always lands in its own frame, so
// the latch is invisible. Throttling reads forces several USB reports to
// accumulate between reads; only with the firmware latch does a click that
// began and ended inside one read interval still reach the master.
//
// What it checks:
//   (A) Single clicks are delivered: click N times slowly (leave >2 read
//       intervals between clicks). pressEdges should reach N. Each click shows
//       as a press frame (b!=0) followed by a release frame (b=0).
//   (B) Collapse limit: tap the SAME button 2-3 times within one READ_INTERVAL_MS.
//       pressEdges increments by only 1 (the OR-latch folds repeats into one
//       press edge) — the documented limitation.
//
// Frame (10 bytes): [0xAA][btnL][btnH][dxL][dxH][dyL][dyH][wheel][hwheel][XOR]
//   buttons/dx/dy are 16-bit little-endian; wheel/hwheel signed 8-bit.
//
// Voltage: the Nano 33 BLE is 3.3V, same as the CH559 SPI pins — connect directly.
//
// Wiring (Nano 33 BLE -> CH559, common GND, all 3.3V):
//   SCK   D13  --> CH559 P1.7
//   MISO  D12  <-- CH559 P1.6
//   MOSI  D11  --> CH559 P1.5
//   CS    D10  --> CH559 P1.4 (SCS)
//   DRDY  D7   <-- CH559 P1.3 (active low)
//   GND   GND  <-> CH559 GND
//
// Build / flash / monitor (arduino-cli; Apple Silicon needs Rosetta 2 for the
// mbed_nano toolchain). The Nano 33 BLE serial port number can change across
// resets — find it with `arduino-cli board list`.
//   arduino-cli compile --fqbn arduino:mbed_nano:nano33ble tools/ch559_spi_check_burst
//   PORT=$(arduino-cli board list | awk '/nano33ble/{print $1; exit}')
//   arduino-cli upload  -p "$PORT" --fqbn arduino:mbed_nano:nano33ble tools/ch559_spi_check_burst
//   arduino-cli monitor -p "$PORT" -c baudrate=115200   # then click the mouse

#include <SPI.h>

const uint8_t PIN_CS = 10;
const uint8_t PIN_DRDY = 7;
const uint8_t FRAME_LEN = 10;
const uint8_t SYNC = 0xAA;
const uint32_t SPI_HZ = 1000000;        // 1 MHz (3.3V direct)
const uint32_t READ_INTERVAL_MS = 150;  // throttle reads so a click can complete between them

SPISettings spiSettings(SPI_HZ, MSBFIRST, SPI_MODE0);
uint8_t buf[FRAME_LEN];

uint32_t lastRead = 0;
uint16_t prevButtons = 0;
uint32_t pressEdges = 0;                 // running count of 0->1 button transitions

void setup() {
  Serial.begin(115200);
  while (!Serial) { ; }                  // wait for the USB CDC port
  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  pinMode(PIN_DRDY, INPUT);              // CH559 drives DRDY (active low)
  SPI.begin();
  Serial.print("CH559 burst/click-latch check (Nano 33 BLE). Reading 1x / ");
  Serial.print(READ_INTERVAL_MS);
  Serial.println(" ms. Click to test.");
}

void loop() {
  // Deliberately slow the master: at most one read per interval. This makes the
  // press+release-between-reads case reproducible by hand.
  if (millis() - lastRead < READ_INTERVAL_MS)
    return;
  if (digitalRead(PIN_DRDY) != LOW)
    return;                              // no frame pending
  lastRead = millis();

  // Read exactly one 10-byte frame. The CH559 slave outputs its sync byte
  // (0xAA) first via the preset register SPI0_S_PRE, so byte 0 is aligned.
  SPI.beginTransaction(spiSettings);
  digitalWrite(PIN_CS, LOW);
  for (uint8_t i = 0; i < FRAME_LEN; i++)
    buf[i] = SPI.transfer(0x00);
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();

  if (buf[0] != SYNC)
    return;                              // not aligned (glitch) — try again next DRDY
  uint8_t x = 0;
  for (uint8_t k = 0; k < FRAME_LEN - 1; k++)
    x ^= buf[k];
  if (x != buf[FRAME_LEN - 1])
    return;                              // bad checksum

  uint16_t buttons = buf[1] | ((uint16_t)buf[2] << 8);
  int16_t dx = (int16_t)(buf[3] | ((uint16_t)buf[4] << 8));
  int16_t dy = (int16_t)(buf[5] | ((uint16_t)buf[6] << 8));
  int8_t wheel = (int8_t)buf[7];
  int8_t hwheel = (int8_t)buf[8];

  // Count press edges: button bits that went 0 -> 1 since the previous read.
  // (A) slow distinct clicks -> pressEdges tracks the click count.
  // (B) same-button taps within one interval -> only +1 (OR-latch collapse).
  uint16_t newlyPressed = buttons & ~prevButtons;
  if (newlyPressed)
    pressEdges += __builtin_popcount(newlyPressed);
  prevButtons = buttons;

  // Print every read so the press frame (b!=0) and the following release frame
  // (b=0) are both visible, alongside the running press-edge count.
  char line[88];
  snprintf(line, sizeof(line), "b=%04X dx=%d dy=%d w=%d hw=%d pressEdges=%lu",
           buttons, dx, dy, wheel, hwheel, (unsigned long)pressEdges);
  Serial.println(line);
}
