// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tadashi Kadowaki
// Part of the CH559 USB mouse host project — a combined work derived from
// atc1441/CH559sdccUSBHost (GPLv3); distributed under GPLv3.
#ifndef __SPI_OUT_H__
#define __SPI_OUT_H__

#include "hid_parser.h"

/*
 * SPI0 slave output of the mouse state, read on demand by an SPI master
 * (nRF52840 / ZMK). Interrupt-driven and non-blocking.
 *
 * Frame (10 bytes): [0xAA][btnL][btnH][dxL][dxH][dyL][dyH][wheel][hwheel][XOR]
 *   buttons and dx, dy are 16-bit little-endian; wheel, hwheel are signed 8-bit.
 *   XOR = byte0 ^ byte1 ^ ... ^ byte8
 *
 * Pins: SCK=P1.7, MISO=P1.6, MOSI=P1.5, SCS=P1.4 (SPI0 slave);
 *       DRDY=P1.3 output, ACTIVE LOW (asserted while a frame is pending).
 *
 * Protocol for the master: when DRDY is asserted (low), read a 10-byte frame
 * (8-bit words, mode 0, MSB first), check byte0 == 0xAA and the XOR, then
 * release CS. byte0 is the slave's SPI0_S_PRE preset (sync), so frames are
 * CS-aligned — no skip/resync needed. The slave re-arms after CS goes high.
 * dx/dy/wheel/hwheel accumulate (saturating) between reads so motion is not
 * lost; buttons carry the latest held state OR'd with any press seen since the
 * last frame, so a click completing between reads is delivered as a press frame
 * then a release frame.
 */

#define SPI_FRAME_SYNC  0xAA
#define SPI_FRAME_LEN   10
#define SPI0_INT_NO     6      /* CH559 INT_NO_SPI0 interrupt vector */

void spiSlaveInit(void);

/* Accumulate one decoded mouse report into the pending frame (non-blocking). */
void spiUpdateMouse(const MouseState *st);

/* Main-loop service: re-arm the next frame after a read. Call every loop. */
void spiService(void);

#endif
