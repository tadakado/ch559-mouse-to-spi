# CH559 USB Mouse Host — Spec / Integration Guide

**English** · [日本語](SPEC.ja.md)

Everything you need to **flash, wire, and consume the SPI output** on the master
side (nRF52840 / ZMK, etc.). For an overview see the [README](README.md).

## 1. Hardware

- **MCU**: WCH CH559 (8051-family, 60 KiB code flash, 1 KiB data EEPROM, BTVER 2.40)
- **Clock**: 48 MHz (internal 12 MHz OSC + PLL)
- **USB**: one controller + two physical ports (HUB0/HUB1), a dual root hub. Only
  devices **plugged directly into the two physical ports** are enumerated (no
  external USB hub).
- **Debug UART**: UART0 alternate pins (TXD=P0.2, RXD=P0.3, `initUART0(baud, 1)`),
  115200 baud.
- **SPI output**: SPI0 slave (SCK=P1.7, MISO=P1.6, MOSI=P1.5, SCS=P1.4, DRDY=P1.3).

### Pinout

| Function | CH559 pin | Dir | Notes |
|---|---|---|---|
| UART0 TXD | **P0.2** | out | 115200 baud (alternate pin) |
| UART0 RXD | **P0.3** | in | Receiving `kb\n` enters ISP |
| SPI0 SCK | **P1.7** | in | Clock from the master |
| SPI0 MISO | **P1.6** | out | Frame output (driven while chip-selected) |
| SPI0 MOSI | **P1.5** | in | Unused in this application |
| SPI0 SCS / CS | **P1.4** | in | Chip select (active low) |
| DRDY | **P1.3** | out | Data ready (active low) |
| DOWNLOAD / boot | **P4.6** | in | Low enters the bootloader (ISP) |
| USB host | 2 root ports | — | Built-in PHY, direct connection only |

## 2. Entering ISP (bootloader) and flashing

Enter ISP one of these ways, then flash with `wchisp flash build/ch559mouse.hex`:

- **(a) Reset while DOWNLOAD (P4.6) is held low** — the chip boots into the
  bootloader (app-independent). Use this for the first flash / recovery.
- **(b) Send `kb\n` on UART** — the running firmware calls `runBootloader()`.
  After that you can re-enter ISP from the PC and automate flashing.
- **(c) Pull P4.6 low while running** — the main loop detects it and calls
  `runBootloader()`.

`wchisp` talks over the direct USB connection (VID:PID 4348:55e0), a separate
path from the serial port, so it can be used alongside `screen` etc.

## 3. Toolchain (macOS)

- **SDCC** 4.5.0 (`brew install sdcc`, `mcs51`)
- **wchisp** 0.3.0 (flashes `.hex` directly; no `.bin` / objcopy needed)
- **uv** (`uv run --with pyserial tools/serial_monitor.py`)
- **GNU Make**

For the build commands (`make` / `make RELEASE=1` / `make RAW=1` / `make test` /
`make flash`) see the [README](README.md).

## 4. SPI output (master = nRF52840 / ZMK)

**10-byte frame:**
```
[0xAA][btnL][btnH][dxL][dxH][dyL][dyH][wheel][hwheel][XOR]
```
- `buttons`, `dx`, `dy`: 16-bit little-endian (`dx`/`dy` signed, full resolution; up to 16 buttons)
- `wheel`, `hwheel`: signed 8-bit
- `XOR` = `byte0 ^ … ^ byte8`

**Characteristics:**
- **Interrupt-driven, non-blocking** (`bS0_IE_BYTE`). SPI0 slave, mode 0 / MSB first.
- **Delta accumulation**: `dx`/`dy`/`wheel`/`hwheel` accumulate (saturating) until
  the master reads them. `buttons` ONTO the latest state ORs in "any bit pressed
  since the last frame" to latch edges, so a complete single click that happens
  between reads isn't lost (delivered as a press frame followed by a release frame).
- **DRDY (P1.3, active low)**: asserted while an unread frame exists. It re-arms
  only **after the master raises CS**, so a frame never changes mid-read.
- **Sync byte**: the first byte after CS comes from the preset register
  `SPI0_S_PRE` (= 0xAA), so the frame is always aligned (no discard / re-sync
  needed on the master).

**Master-side protocol:**
1. Detect DRDY low → pull CS low.
2. **Read 10 bytes** → check `byte0 == 0xAA` and the XOR → decode.
3. Raise CS. If new data exists, DRDY goes low again.

## 5. Supported mice / HID

The HID report descriptor is parsed to extract the bit positions and Report ID of
buttons / X (0x30) / Y (0x31) / wheel (0x38) / horizontal wheel (Consumer AC Pan
0x0238). Report IDs are handled. Standard descriptor-compliant mice are supported
(no fallback for boot-protocol-only mice).

Verified: Logitech receiver VID:0x046D / PID:0xC548 (Report ID 2, 9 bytes/report):
```
[ID=2][buttons16(byte1-2)][X16(byte3-4)][Y16(byte5-6)][wheel8(byte7)][hwheel8(byte8)]
```

## 6. Verified setups / wiring

Verified with an nRF52840-class board (same chip as ZMK) as the SPI master, 3.3 V
direct, common ground.

| Signal | CH559 | Arduino Nano 33 BLE | Seeed XIAO nRF52840 Plus |
|---|---|---|---|
| SCK | P1.7 | D13 | D17 (P1.03) |
| MISO | P1.6 | D12 | D18 (P1.05) |
| MOSI | P1.5 | D11 | D19 (P1.07) |
| SCS | P1.4 | D10 | D15 (P0.10, NFC2) |
| DRDY | P1.3 | D7 | D14 (P0.09, NFC1) |
| GND | GND | GND | GND |

SPI receive-check sketches: `tools/ch559_spi_check/` (Nano 33 BLE) /
`tools/ch559_spi_check_xiao/` (XIAO nRF52840 Plus), flashed with `arduino-cli`.

**XIAO caveat (NFC pins):** SCS (D15 = P0.10) and DRDY (D14 = P0.09) are the
nRF52840 NFC antenna pins. The Seeed core leaves them in NFC mode by default, so
`digitalRead` doesn't return the real level and the DRDY gate fails. Building with
`-DCONFIG_NFCT_PINS_AS_GPIOS` makes `UICR.NFCPINS` be rewritten once (persistent),
turning them into normal GPIOs. With `arduino-cli`, pass it via
`--build-property compiler.c.extra_flags=-DCONFIG_NFCT_PINS_AS_GPIOS` and flash
with `compile --upload` (`upload` doesn't accept `--build-property`). The XIAO's
custom SPI is `SPIClass(NRF_SPIM2, MISO, SCK, MOSI)` (the Seeed core's argument
order).

## 7. Known limitations

- No external USB hub (direct connection to the two physical ports only).
- No fallback for boot-protocol-only mice (descriptor-compliant mice work).
- SPI output resolution: `dx`/`dy` and `buttons` are 16-bit; `wheel`/`hwheel` are 8-bit.

## 8. Power

While acting as a USB host, the CH559 keeps its USB PHY and 48 MHz core running, so
its power floor is high. Two practical reductions are applied, neither of which
changes behavior or feel:

- **`bInterval` polling pacing**: issue IN only at each endpoint's `bInterval` (ms)
  instead of every loop, stopping the IN/NAK spam (bus and device power waste)
  during the long stretches with no report.
- **Clock gating**: stop the clocks of unused peripherals (ADC / UART1 / PWM1 ·
  SPI1 / Timer3 / LED) via `SLEEP_CTRL` (USB / SPI0 / Timer0,1 / UART0 / XRAM stay on).

PD sleep has no practical use in this design: in host mode the PHY stops, so the
device can't be woken by plugging a mouse in.
