// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tadashi Kadowaki
// Part of the CH559 USB mouse host project (distributed under GPLv3).
//
// Arduino Nano 33 BLE (nRF52840) SPI-master connection check for the CH559
// mouse-host slave. When DRDY (active LOW) is asserted, reads a frame over SPI,
// validates the 0xAA sync + XOR, decodes it, and prints over USB serial
// (Serial @115200). The Nano 33 BLE is the same nRF52840 used by the ZMK
// target, so this also doubles as a protocol reference.
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
//   arduino-cli compile --fqbn arduino:mbed_nano:nano33ble tools/ch559_spi_check
//   PORT=$(arduino-cli board list | awk '/nano33ble/{print $1; exit}')
//   arduino-cli upload  -p "$PORT" --fqbn arduino:mbed_nano:nano33ble tools/ch559_spi_check
//   arduino-cli monitor -p "$PORT" -c baudrate=115200   # move the mouse

#include <SPI.h>

const uint8_t PIN_CS = 10;
const uint8_t PIN_DRDY = 7;
const uint8_t FRAME_LEN = 10;
const uint8_t SYNC = 0xAA;
const uint32_t SPI_HZ = 1000000;    // 1 MHz (3.3V direct)

SPISettings spiSettings(SPI_HZ, MSBFIRST, SPI_MODE0);
uint8_t buf[FRAME_LEN];

void setup() {
  Serial.begin(115200);
  while (!Serial) { ; }             // wait for the USB CDC port
  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  pinMode(PIN_DRDY, INPUT);         // CH559 drives DRDY (active low)
  SPI.begin();
  Serial.println("CH559 SPI check ready (Nano 33 BLE). DRDY-gated; move the mouse.");
}

void loop() {
  if (digitalRead(PIN_DRDY) != LOW)
    return;                         // no frame pending

  // Read exactly one 10-byte frame. The CH559 slave outputs its sync byte
  // (0xAA) first via the preset register SPI0_S_PRE, so byte 0 is aligned and
  // no skip/resync is needed.
  SPI.beginTransaction(spiSettings);
  digitalWrite(PIN_CS, LOW);
  for (uint8_t i = 0; i < FRAME_LEN; i++)
    buf[i] = SPI.transfer(0x00);
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();

  if (buf[0] != SYNC)
    return;                         // not aligned (glitch) — try again next DRDY
  uint8_t x = 0;
  for (uint8_t k = 0; k < FRAME_LEN - 1; k++)
    x ^= buf[k];
  if (x != buf[FRAME_LEN - 1])
    return;                         // bad checksum

  uint16_t buttons = buf[1] | ((uint16_t)buf[2] << 8);
  int16_t dx = (int16_t)(buf[3] | ((uint16_t)buf[4] << 8));
  int16_t dy = (int16_t)(buf[5] | ((uint16_t)buf[6] << 8));
  int8_t wheel = (int8_t)buf[7];
  int8_t hwheel = (int8_t)buf[8];

  // Print on any motion (incl. wheel/hwheel) or a button change; quiet when idle.
  static uint16_t lastB = 0;
  if (dx || dy || wheel || hwheel || buttons != lastB) {
    char line[72];
    snprintf(line, sizeof(line), "b=%04X dx=%d dy=%d w=%d hw=%d",
             buttons, dx, dy, wheel, hwheel);
    Serial.println(line);
    lastB = buttons;
  }
}
