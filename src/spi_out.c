// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tadashi Kadowaki
// Part of the CH559 USB mouse host project — a combined work derived from
// atc1441/CH559sdccUSBHost (GPLv3); distributed under GPLv3.
#include "CH559.h"
#include "spi_out.h"

/*
 * SPI0 slave output, read on demand by an SPI master (nRF52840 / ZMK).
 *
 * Movement (dx/dy/wheel/hwheel) is accumulated (saturating) until read so no
 * motion is lost between reads. Buttons report the latest held state OR'd with
 * any press that occurred since the last frame, so a press+release that
 * completes between reads is still delivered (as a press frame followed by a
 * release frame). A DATA-READY line (P1.3, active LOW) is asserted while a
 * frame is pending.
 *
 * The SPI0 byte-complete ISR is kept minimal (no function calls) so it refills
 * the TX FIFO within one SPI byte time even at MHz clocks: it just streams the
 * prepared txframe and, after a full 10-byte frame, deasserts DRDY and flags
 * completion. The heavy work (snapshotting accumulators into a frame, clearing
 * deltas, re-arming) runs in spiService() on the main loop, where there is no
 * per-byte deadline because the master pauses between DRDY-gated reads.
 */

SBIT(DRDY, 0x90, 3);   /* P1.3: data-ready to master, active LOW */
#define bDRDY  0x08    /* P1.3 mask */

/* State shared between the main loop and the SPI0 ISR. */
static volatile __xdata int           acc_dx, acc_dy;       /* signed 16-bit deltas */
static volatile __xdata signed char   acc_wheel, acc_hwheel;
static volatile __xdata unsigned int  cur_buttons;          /* latest button state */
static volatile __xdata unsigned int  acc_btn_pressed;      /* OR of buttons pressed since last frame */
static volatile __xdata unsigned char pending;              /* unread data accumulated */
static volatile __xdata unsigned char armed;                /* a frame is loaded/streaming */
static volatile __xdata unsigned char frame_sent;           /* ISR: a full frame was clocked */
static volatile __xdata unsigned char txframe[SPI_FRAME_LEN];
static volatile __xdata unsigned char txidx;                /* next byte index to load */

static int sat16(long v)
{
	if (v > 32767)
		return 32767;
	if (v < -32768)
		return -32768;
	return (int)v;
}

static signed char sat8(long v)
{
	if (v > 127)
		return 127;
	if (v < -128)
		return -128;
	return (signed char)v;
}

/* Snapshot the accumulators into txframe (with XOR) and clear the deltas. */
static void build_frame(void)
{
	unsigned char x;
	/* Fold every button pressed since the last frame into the reported state so
	 * a press+release that completes between master reads is not lost (the same
	 * "accumulate until read" rule used for motion). */
	unsigned int btn = cur_buttons | acc_btn_pressed;
	txframe[0] = SPI_FRAME_SYNC;
	txframe[1] = (unsigned char)(btn & 0xFF);
	txframe[2] = (unsigned char)((btn >> 8) & 0xFF);
	txframe[3] = (unsigned char)(acc_dx & 0xFF);
	txframe[4] = (unsigned char)(((unsigned int)acc_dx >> 8) & 0xFF);
	txframe[5] = (unsigned char)(acc_dy & 0xFF);
	txframe[6] = (unsigned char)(((unsigned int)acc_dy >> 8) & 0xFF);
	txframe[7] = (unsigned char)acc_wheel;
	txframe[8] = (unsigned char)acc_hwheel;
	x = txframe[0] ^ txframe[1] ^ txframe[2] ^ txframe[3] ^ txframe[4] ^
	    txframe[5] ^ txframe[6] ^ txframe[7] ^ txframe[8];
	txframe[9] = x;
	acc_dx = 0;
	acc_dy = 0;
	acc_wheel = 0;
	acc_hwheel = 0;
	acc_btn_pressed = 0;
	/* If transient presses were folded in, this frame shows a button that is no
	 * longer held; keep `pending` so the next frame delivers the settled (now
	 * released) state and the master never sticks in a held state. */
	pending = (btn != cur_buttons) ? 1 : 0;
}

/* Flush the TX FIFO and preload txframe[0] so the next chip-select read starts
 * cleanly at the frame sync byte (removes any stale/queued FIFO content). */
static void load_frame(void)
{
	SPI0_CTRL |= bS0_CLR_ALL;
	SPI0_CTRL &= ~bS0_CLR_ALL;
	SPI0_S_PRE = SPI_FRAME_SYNC; /* first byte after CS = sync (refresh each arm) */
	txidx = 0;
	SPI0_DATA = txframe[0];
}

void spiSlaveInit(void)
{
	/* SPI0 slave pins floating inputs; MISO auto-driven when chip-selected. */
	PORT_CFG &= ~bP1_OC;
	P1_DIR   &= ~(bSCK | bMOSI | bMISO | bSCS);
	P1_PU    &= ~(bSCK | bMOSI | bMISO | bSCS);

	/* DATA-READY on P1.3: push-pull output, idle high (active low). */
	P1_DIR |= bDRDY;
	DRDY = 1;

	/* slave mode + enable the per-byte interrupt (bS0_IE_BYTE); without it the
	 * SPI peripheral never raises the SPI0 interrupt and the ISR never refills
	 * the FIFO. bS0_BIT_ORDER=0 keeps MSB-first. */
	SPI0_SETUP = bS0_MODE_SLV | bS0_IE_BYTE;
	SPI0_CTRL  = bS0_MISO_OE | bS0_AUTO_IF;  /* MISO out; FIFO access auto-clears IF */
	SPI0_CTRL |= bS0_CLR_ALL;                /* flush FIFO and counters */
	SPI0_CTRL &= ~bS0_CLR_ALL;

	/* In slave mode the FIRST byte clocked out after each chip-select comes from
	 * the preset register SPI0_S_PRE (by design, for a status/handshake byte),
	 * NOT from the FIFO. Set it to our sync byte so byte 0 is always 0xAA and
	 * the frame is byte-aligned to CS (no garbled first byte to resync past). */
	SPI0_S_PRE = SPI_FRAME_SYNC;

	acc_dx = 0; acc_dy = 0; acc_wheel = 0; acc_hwheel = 0;
	cur_buttons = 0; acc_btn_pressed = 0; pending = 0; armed = 0; frame_sent = 0;
	build_frame();              /* idle (zero) frame */
	load_frame();               /* clean FIFO, preload txframe[0] */

	S0_IF_FIRST = 0;
	S0_IF_BYTE = 0;
	IE_SPI0 = 1;                /* enable SPI0 byte-complete interrupt */
	EA = 1;                     /* enable global interrupts */
}

/* Accumulate one decoded mouse report (non-blocking, called per USB report). */
void spiUpdateMouse(const MouseState *st)
{
	EA = 0;
	acc_dx = sat16((long)acc_dx + st->dx);
	acc_dy = sat16((long)acc_dy + st->dy);
	acc_wheel = sat8((long)acc_wheel + st->wheel);
	acc_hwheel = sat8((long)acc_hwheel + st->hwheel);
	acc_btn_pressed |= st->buttons;   /* latch press edges (cleared on frame build) */
	cur_buttons = st->buttons;
	pending = 1;
	EA = 1;
}

/* Main-loop service: arm a fresh frame when data is pending and the previous
 * one has been read. Not time-critical (runs between DRDY-gated master reads). */
void spiService(void)
{
	/* Treat the frame as consumed only once the master has RELEASED chip-select
	 * (SCS high), not merely when the 10th byte was clocked. This defers
	 * re-arming until the read transaction is complete, so a master that clocks
	 * more than one frame in a single CS read always sees the SAME frame
	 * (no mid-read re-arm / frame change). */
	if (frame_sent && SCS) {
		frame_sent = 0;
		armed = 0;              /* previous frame consumed; DRDY raised by ISR */
	}
	if (!armed && pending) {
		EA = 0;
		build_frame();          /* snapshot + clear deltas */
		load_frame();           /* flush FIFO, preload txframe[0] (CS-aligned) */
		armed = 1;
		DRDY = 0;               /* assert: a frame is ready */
		EA = 1;
	}
}

/* SPI0 byte-complete ISR: minimal, no function calls. A byte was just clocked
 * out; load the next. After the last frame byte, deassert DRDY and flag the
 * main loop to re-arm. (bS0_AUTO_IF clears the interrupt flag on FIFO write.) */
void spi0_isr(void) __interrupt(SPI0_INT_NO)
{
	unsigned char i;
	if (S0_IF_FIRST) {
		/* First byte of a new chip-select transaction was just clocked out;
		 * treat it as frame[0] and continue from frame[1] (CS realignment). */
		S0_IF_FIRST = 0;
		i = 0;
	} else {
		i = txidx;
	}
	i++;
	if (i >= SPI_FRAME_LEN) {
		i = 0;
		DRDY = 1;          /* frame fully clocked: deassert (single-bit write) */
		frame_sent = 1;
	}
	SPI0_DATA = txframe[i];
	txidx = i;
}
