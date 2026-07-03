// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tadashi Kadowaki
// Part of the CH559 USB mouse host project — a combined work derived from
// atc1441/CH559sdccUSBHost (GPLv3); distributed under GPLv3.
#ifndef __HID_PARSER_H__
#define __HID_PARSER_H__

/*
 * Hardware-independent HID Report Descriptor parser for mice.
 * Extracts the bit positions of buttons, X, Y, wheel and horizontal wheel
 * (Consumer AC Pan) so the live interrupt-IN report can be decoded.
 *
 * Pointers are plain `const unsigned char *`. Under SDCC --model-large the
 * default generic pointer can address __xdata, so the firmware may pass an
 * __xdata report buffer directly. The same code compiles on the host with
 * -DHOST_TEST for unit testing.
 */

/* One extracted field. bitOffset/bitSize are measured from the start of the
 * report, including the 1-byte Report ID when present. For `buttons`, bitSize
 * holds the total number of button bits in the block. */
typedef struct
{
	unsigned char valid;
	unsigned short bitOffset;
	unsigned char bitSize;
	unsigned char reportId;
} HidField;

typedef struct
{
	unsigned char hasReportId;
	unsigned char mouseReportId; /* report id carrying X/Y (0 if none) */
	HidField buttons;
	HidField x;
	HidField y;
	HidField wheel;
	HidField hwheel;
} MouseLayout;

/* Decoded mouse movement for one report.
 * buttons and X/Y are kept at full resolution; wheel/hwheel are clamped to 8-bit. */
typedef struct
{
	unsigned short buttons; /* up to 16 buttons (bit0 = button 1) */
	signed short dx;
	signed short dy;
	signed char wheel;
	signed char hwheel;
} MouseState;

/* Parse a HID Report Descriptor into a MouseLayout. Always clears `out` first.
 * out->x.valid indicates a usable mouse layout was found. */
void hid_parse_mouse(const unsigned char *desc, unsigned short len, MouseLayout *out);

/* Decode one interrupt-IN report using a parsed layout.
 * Returns 1 and fills `st` if the report matches the mouse layout, else 0. */
unsigned char hid_extract_mouse(const MouseLayout *layout,
				const unsigned char *report, unsigned short len,
				MouseState *st);

#endif
