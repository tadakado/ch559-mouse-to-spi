// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tadashi Kadowaki
// Part of the CH559 USB mouse host project (distributed under GPLv3).
//
// Seeed XIAO nRF52840 Plus SPI-master check for the CH559
// mouse-host slave. When DRDY (active LOW) is asserted, reads a frame over SPI,
// validates the 0xAA sync + XOR, decodes it, and prints over USB serial
// (Serial @115200). 
//
// Frame (10 bytes): [0xAA][btnL][btnH][dxL][dxH][dyL][dyH][wheel][hwheel][XOR]
//   buttons/dx/dy are 16-bit little-endian; wheel/hwheel signed 8-bit.
//
// Voltage: XIAO nRF52840 is 3.3V, same as the CH559 SPI pins — connect directly.
//
// Wiring (XIAO -> CH559, common GND, all 3.3V; nRF GPIO in parentheses):
//   SCK   D17 (P1.03) --> CH559 P1.7
//   MISO  D18 (P1.05) <-- CH559 P1.6
//   MOSI  D19 (P1.07) --> CH559 P1.5
//   CS    D15 (P0.10) --> CH559 P1.4 (SCS)
//   DRDY  D14 (P0.09) <-- CH559 P1.3 (active low)
//   GND   GND         <-> CH559 GND
//
// IMPORTANT: both CS (D15 = P0.10, NFC2) and DRDY (D14 = P0.09, NFC1) are
// nRF52840 NFC antenna pins. The Seeed nRF52 core (1.1.13) does NOT define
// CONFIG_NFCT_PINS_AS_GPIOS, so P0.09/P0.10 stay in NFC mode and digitalRead()
// does not return the real pin level — the DRDY gate fails and the master
// free-runs, re-reading the slave's stale frame (same dx/dy repeats; pressing a
// button briefly clears it). Fix: build with -DCONFIG_NFCT_PINS_AS_GPIOS so
// SystemInit writes UICR.NFCPINS once (persistent, auto-resets on first boot) and
// P0.09/P0.10 become normal GPIOs. The flag is added via compiler.c.extra_flags
// because boards.txt already populates build.extra_flags.
//
// Build / flash / monitor (arduino-cli; the Seeed nRF52 build invokes `python`,
// so on systems with only python3 expose a shim:
//   mkdir -p /tmp/pyshim && ln -sf /opt/homebrew/bin/python3 /tmp/pyshim/python
//   export PATH="/tmp/pyshim:$PATH"
//   NFC='--build-property compiler.c.extra_flags=-DCONFIG_NFCT_PINS_AS_GPIOS'
//   arduino-cli compile --fqbn Seeeduino:nrf52:xiaonRF52840Plus $NFC --clean tools/ch559_spi_check_xiao
//   PORT=$(arduino-cli board list | awk '/xiaonRF52840/{print $1; exit}')
//   arduino-cli compile --fqbn Seeeduino:nrf52:xiaonRF52840Plus $NFC --upload -p "$PORT" tools/ch559_spi_check_xiao
//   arduino-cli monitor -p "$PORT" -c baudrate=115200   # move the mouse
// (`upload` does not accept --build-property, so flash via `compile --upload`.)

#include <SPI.h>

const uint8_t PIN_SCK = D17;
const uint8_t PIN_MISO = D18;
const uint8_t PIN_MOSI = D19;
const uint8_t PIN_CS = D15;
const uint8_t PIN_DRDY = D14;
const uint8_t FRAME_LEN = 10;
const uint8_t SYNC = 0xAA;
const uint32_t SPI_HZ = 1000000;    // 1 MHz (3.3V direct)

// Custom SPI bus on the requested pins.
// NOTE: the Seeed nRF52 core's constructor order is (spim, MISO, SCK, MOSI) —
// SCK before MOSI (differs from some other cores). See <SPI/SPI.h>.
SPIClass spiBus(NRF_SPIM2, PIN_MISO, PIN_SCK, PIN_MOSI);
SPISettings spiSettings(SPI_HZ, MSBFIRST, SPI_MODE0);
uint8_t buf[FRAME_LEN];

void setup() {
  Serial.begin(115200);
  while (!Serial) { ; }             // wait for the USB CDC port
  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  pinMode(PIN_DRDY, INPUT);         // CH559 drives DRDY (active low)
  spiBus.begin();
  Serial.println("CH559 SPI check ready (XIAO nRF52840 Plus). DRDY-gated; move the mouse.");
}

void loop() {
  if (digitalRead(PIN_DRDY) != LOW)
    return;                         // no frame pending

  // Read exactly one 10-byte frame. The CH559 slave outputs its sync byte
  // (0xAA) first via the preset register SPI0_S_PRE, so byte 0 is aligned and
  // no skip/resync is needed.
  spiBus.beginTransaction(spiSettings);
  digitalWrite(PIN_CS, LOW);
  for (uint8_t i = 0; i < FRAME_LEN; i++)
    buf[i] = spiBus.transfer(0x00);
  digitalWrite(PIN_CS, HIGH);
  spiBus.endTransaction();

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
