// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tadashi Kadowaki
// Part of the CH559 USB mouse host project — a combined work derived from
// atc1441/CH559sdccUSBHost (GPLv3); distributed under GPLv3.
/*
 * CH559 USB mouse host: enumerate a USB mouse, poll its interrupt-IN endpoint,
 * decode the HID report (buttons/X/Y/wheel/hwheel) and output the state on SPI0
 * (slave) while logging a human-readable line on UART0.
 *
 * Enumeration, polling and peripheral setup are reused from
 * atc1441/CH559sdccUSBHost; the mouse decoding (hid_parser) and SPI output
 * (spi_out) are the project-specific additions. The mouse decode + SPI send is
 * wired into pollHIDdevice() in USBHost.c.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "CH559.h"
#include "util.h"
#include "USBHost.h"
#include "uart.h"
#include "spi_out.h"

/* UART debug output. Enabled by default; `make RELEASE=1` compiles it out so the
 * per-report printf does not block the loop in production. */
#ifdef DEBUG_UART
#define DBG(...) printf(__VA_ARGS__)
#else
#define DBG(...) ((void)0)
#endif

/* Build-variant tag for the boot banner. */
#if defined(DEBUG_RAW)
#define BUILD_VARIANT "RAW"
#elif defined(DEBUG_UART)
#define BUILD_VARIANT "DEBUG"
#else
#define BUILD_VARIANT "RELEASE"
#endif

/* One-time boot banner, printed on UART0 in EVERY build (including RELEASE). It
 * is emitted once at startup, so it has none of the per-report printf's
 * main-loop cost, and it confirms the device is alive, the expected baud, and
 * which build is flashed. Uses putchar directly (no printf) so the formatted-
 * output code is not pulled into the RELEASE binary; the date/time/variant are
 * compile-time string literals, so no formatting is needed. */
static const __code char BANNER[] =
	"\n=== CH559 USB Mouse Host ===\n"
	"build " __DATE__ " " __TIME__ " [" BUILD_VARIANT "]"
	"  uart 115200 8N1  SPI0 slave 10B\n";

static void uartPuts(const char __code *s)
{
	while (*s)
		putchar(*s++);
}

/* The SPI0 byte-complete ISR is defined in spi_out.c and the Timer0 1 ms tick
 * ISR in util.c; declare them here so SDCC emits their interrupt vectors in the
 * module that contains main(). */
void spi0_isr(void) __interrupt(SPI0_INT_NO);
void timer0_isr(void) __interrupt(INT_NO_TMR0);

void main()
{
	unsigned char s;
	initClock();
	initUART0(115200, 1);
	initClockGating();  /* power: gate clocks of unused peripherals */
	spiSlaveInit();
	initTimer0Tick();   /* 1 ms time base: paces USB polling to the bInterval */
	uartPuts(BANNER);   /* boot banner: always on, even in RELEASE */
	resetHubDevices(0);
	resetHubDevices(1); /* scan both physical root-hub ports */
	initUSB_Host();
	uartPuts("ready\n");
	sendProtocolMSG(MSG_TYPE_STARTUP, 0, 0x00, 0x00, 0x00, 0);
	while (1)
	{
		if (!(P4_IN & (1 << 6)))
			runBootloader();
		processUart();
		s = checkRootHubConnections();
		(void)s;
		pollHIDdevice();
		spiService();   /* re-arm SPI frame after a master read (non-blocking) */
	}
}
