// SPDX-License-Identifier: GPL-3.0-only
// Derived from atc1441/CH559sdccUSBHost (GPLv3).
// https://github.com/atc1441/CH559sdccUSBHost
#ifndef __UTIL_H__
#define __UTIL_H__
#include <stdio.h>
#if 0
#define DEBUG_OUT(...) printf(__VA_ARGS__);
#else
#define DEBUG_OUT(...) (void)0;
#endif

void initClock();
void delayUs(unsigned short n);
void delay(unsigned short n);
void initUART0(unsigned long baud, int alt);
unsigned char UART0Receive();
void UART0Send(unsigned char b);

/* Gate off clocks of unused peripherals (ADC/UART1/PWM1-SPI1/Timer3/LED). */
void initClockGating(void);

/* 1 ms time base on Timer0 for pacing USB polling to the endpoint bInterval. */
void initTimer0Tick(void);
unsigned char timerMs(void);   /* free-running 1 ms counter, wraps every 256 ms */

#define PIN_MODE_INPUT 0
#define PIN_MODE_INPUT_PULLUP 1
#define PIN_MODE_OUTPUT 2
#define PIN_MODE_OUTPUT_OPEN_DRAIN 3
#define PIN_MODE_OUTPUT_OPEN_DRAIN_2CLK 4
#define PIN_MODE_INPUT_OUTPUT_PULLUP 5
#define PIN_MODE_INPUT_OUTPUT_PULLUP_2CLK 6
void pinMode(unsigned char port, unsigned char pin, unsigned char mode);

typedef void(* __data FunctionReference)();
extern FunctionReference runBootloader;

#endif