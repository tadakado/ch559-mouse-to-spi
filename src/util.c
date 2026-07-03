// SPDX-License-Identifier: GPL-3.0-only
// Derived from atc1441/CH559sdccUSBHost (GPLv3).
// https://github.com/atc1441/CH559sdccUSBHost
#include "CH559.h"
#include "util.h"

FunctionReference runBootloader = (FunctionReference)0xF400;

#ifndef FREQ_SYS
#define	FREQ_SYS	48000000
#endif 

void initClock()
{
    SAFE_MOD = 0x55;
    SAFE_MOD = 0xAA;

	CLOCK_CFG &= ~MASK_SYS_CK_DIV;
	CLOCK_CFG |= 6; 															  
	PLL_CFG = ((24 << 0) | (6 << 5)) & 255;

    SAFE_MOD = 0xFF;

	delay(7);
}

/**
 * Initialize UART0 port with given boud rate
 * pins: tx = P3.1 rx = P3.0
 * alt != 0 pins: tx = P0.2 rx = P0.3
 */

void initUART0(unsigned long baud, int alt)
{
	unsigned long x;
	if(alt)
	{
		PORT_CFG |= bP0_OC;
		P0_DIR |= bTXD_;
		P0_PU |= bTXD_ | bRXD_;
		PIN_FUNC |= bUART0_PIN_X;
	}

 	SM0 = 0;
	SM1 = 1;
	SM2 = 0;
	REN = 1;
   //RCLK = 0;
    //TCLK = 0;
    PCON |= SMOD;
    x = (((unsigned long)FREQ_SYS / 8) / baud + 1) / 2;

    TMOD = TMOD & ~ bT1_GATE & ~ bT1_CT & ~ MASK_T1_MOD | bT1_M1;
    T2MOD = T2MOD | bTMR_CLK | bT1_CLK;
    TH1 = (256 - x) & 255;
    TR1 = 1;
	TI = 1;
}

unsigned char UART0Receive()
{
    while(RI == 0);
    RI = 0;
    return SBUF;
}

void UART0Send(unsigned char b)
{
	SBUF = b;
	while(TI == 0);
	TI = 1;
}

/**
* #define PIN_MODE_INPUT 0
* #define PIN_MODE_INPUT_PULLUP 1
* #define PIN_MODE_OUTPUT 2
* #define PIN_MODE_OUTPUT_OPEN_DRAIN 3
* #define PIN_MODE_OUTPUT_OPEN_DRAIN_2CLK 4
* #define PIN_MODE_INPUT_OUTPUT_PULLUP 5
* #define PIN_MODE_INPUT_OUTPUT_PULLUP_2CLK 6
 */
void pinMode(unsigned char port, unsigned char pin, unsigned char mode)
{
	volatile unsigned char *dir[] = {&P0_DIR, &P1_DIR, &P2_DIR, &P3_DIR};
	volatile unsigned char *pu[] = {&P0_PU, &P1_PU, &P2_PU, &P3_PU};
	switch (mode)
	{
	case PIN_MODE_INPUT: //Input only, no pull up
		PORT_CFG &= ~(bP0_OC << port);
		*dir[port] &= ~(1 << pin);
		*pu[port] &= ~(1 << pin);
		break;
	case PIN_MODE_INPUT_PULLUP: //Input only, pull up
		PORT_CFG &= ~(bP0_OC << port);
		*dir[port] &= ~(1 << pin);
		*pu[port] |= 1 << pin;
		break;
	case PIN_MODE_OUTPUT: //Push-pull output, high and low level strong drive
		PORT_CFG &= ~(bP0_OC << port);
		*dir[port] |= ~(1 << pin);
		break;
	case PIN_MODE_OUTPUT_OPEN_DRAIN: //Open drain output, no pull-up, support input
		PORT_CFG |= (bP0_OC << port);
		*dir[port] &= ~(1 << pin);
		*pu[port] &= ~(1 << pin);
		break;
	case PIN_MODE_OUTPUT_OPEN_DRAIN_2CLK: //Open-drain output, no pull-up, only drives 2 clocks high when the transition output goes from low to high
		PORT_CFG |= (bP0_OC << port);
		*dir[port] |= 1 << pin;
		*pu[port] &= ~(1 << pin);
		break;
	case PIN_MODE_INPUT_OUTPUT_PULLUP: //Weakly bidirectional (standard 51 mode), open drain output, with pull-up
		PORT_CFG |= (bP0_OC << port);
		*dir[port] &= ~(1 << pin);
		*pu[port] |= 1 << pin;
		break;
	case PIN_MODE_INPUT_OUTPUT_PULLUP_2CLK: //Quasi-bidirectional (standard 51 mode), open-drain output, with pull-up, when the transition output is low to high, only drives 2 clocks high
		PORT_CFG |= (bP0_OC << port);
		*dir[port] |= 1 << pin;
		*pu[port] |= 1 << pin;
		break;
	default:
		break;
	}
}
/*
unsigned char getPortAddress(unsigned char port)
{
	const unsigned char portAddr[] = {0x80, 0x90, 0xA0, 0xB0, 0xC0};
	return portAddr[port];
}

unsigned char digitalRead(unsigned char port, unsigned char pin)
{

}
*/
/**
 * stdio printf directed to UART0 using putchar and getchar
 */

int putchar(int c)
{
    while (!TI);
    TI = 0;
    SBUF = c & 0xFF;
    return c;
}

int getchar() 
{
    while(!RI);
    RI = 0;
    return SBUF;
}

/*******************************************************************************
* Function Name  : delayUs(UNIT16 n)
* Description    : us
* Input          : UNIT16 n
* Output         : None
* Return         : None
*******************************************************************************/ 
void	delayUs(unsigned short n)
{
	while (n) 
	{  // total = 12~13 Fsys cycles, 1uS @Fsys=12MHz
		++ SAFE_MOD;  // 2 Fsys cycles, for higher Fsys, add operation here
#ifdef	FREQ_SYS
#if		FREQ_SYS >= 14000000
		++ SAFE_MOD;
#endif
#if		FREQ_SYS >= 16000000
		++ SAFE_MOD;
#endif
#if		FREQ_SYS >= 18000000
		++ SAFE_MOD;
#endif
#if		FREQ_SYS >= 20000000
		++ SAFE_MOD;
#endif
#if		FREQ_SYS >= 22000000
		++ SAFE_MOD;
#endif
#if		FREQ_SYS >= 24000000
		++ SAFE_MOD;
#endif
#if		FREQ_SYS >= 26000000
		++ SAFE_MOD;
#endif
#if		FREQ_SYS >= 28000000
		++ SAFE_MOD;
#endif
#if		FREQ_SYS >= 30000000
		++ SAFE_MOD;
#endif
#if		FREQ_SYS >= 32000000
		++ SAFE_MOD;
#endif
#if		FREQ_SYS >= 34000000
		++ SAFE_MOD;
#endif
#if		FREQ_SYS >= 36000000
		++ SAFE_MOD;
#endif
#if		FREQ_SYS >= 38000000
		++ SAFE_MOD;
#endif
#if		FREQ_SYS >= 40000000
		++ SAFE_MOD;
#endif
#if		FREQ_SYS >= 42000000
		++ SAFE_MOD;
#endif
#if		FREQ_SYS >= 44000000
		++ SAFE_MOD;
#endif
#if		FREQ_SYS >= 46000000
		++ SAFE_MOD;
#endif
#if		FREQ_SYS >= 48000000
		++ SAFE_MOD;
#endif
#if		FREQ_SYS >= 50000000
		++ SAFE_MOD;
#endif
#if		FREQ_SYS >= 52000000
		++ SAFE_MOD;
#endif
#if		FREQ_SYS >= 54000000
		++ SAFE_MOD;
#endif
#if		FREQ_SYS >= 56000000
		++ SAFE_MOD;
#endif
#endif
		--n;
	}
}

/*******************************************************************************
* Function Name  : delay(UNIT16 n)
* Description    : ms
* Input          : UNIT16 n
* Output         : None
* Return         : None
*******************************************************************************/
void delay(unsigned short n)
{
	while (n)
	{
		delayUs(1000);
		--n;
	}
}

/* 1 ms time base on Timer0, used to pace USB polling to each endpoint's
 * bInterval (instead of issuing IN tokens every main-loop iteration, which
 * floods the bus with NAKed transfers). Timer1 is taken by the UART0 baud
 * generator, so Timer0 is used here. Timer0 runs at the standard Fsys/12 =
 * 4 MHz (bT0_CLK in T2MOD left clear), so 1 ms = 4000 counts; mode 1 (16-bit)
 * has no auto-reload, so the ISR reloads 65536 - 4000 = 61536 = 0xF060. */
#define T0_RELOAD_H  0xF0
#define T0_RELOAD_L  0x60

static volatile unsigned char gMsTick;   /* free-running 1 ms counter, wraps every 256 ms */

/* Gate off the clocks of peripherals this firmware never uses (ADC, UART1,
 * PWM1/SPI1, Timer3, the LED controller block) to trim power. The peripherals
 * we rely on are kept running by leaving their SLEEP_CTRL bits clear:
 *   - bSLP_OFF_USB  (USB host)      - bSLP_OFF_SPI0 (SPI0 slave output)
 *   - bSLP_OFF_XRAM (xdata RAM)     -> gating XRAM would corrupt all __xdata state
 * Timer0/1 and UART0 have no SLEEP_CTRL bit (their clocks track the core), so
 * they are unaffected. SLEEP_CTRL is write-protected, so it is written inside
 * the SAFE_MOD unlock window, same as initClock(). */
void initClockGating(void)
{
	SAFE_MOD = 0x55;
	SAFE_MOD = 0xAA;
	SLEEP_CTRL = bSLP_OFF_ADC | bSLP_OFF_UART1 | bSLP_OFF_P1S1 |
	             bSLP_OFF_TMR3 | bSLP_OFF_LED;
	SAFE_MOD = 0xFF;
}

void initTimer0Tick(void)
{
	/* Timer0 = mode 1 (16-bit), internal clock, gate off; preserve Timer1 bits
	 * (UART0 baud) in the high nibble of TMOD. */
	TMOD = (TMOD & ~(bT0_GATE | bT0_CT | MASK_T0_MOD)) | bT0_M0;
	TH0 = T0_RELOAD_H;
	TL0 = T0_RELOAD_L;
	gMsTick = 0;
	TF0 = 0;
	ET0 = 1;     /* enable Timer0 interrupt (EA is enabled in spiSlaveInit) */
	TR0 = 1;     /* start Timer0 */
}

unsigned char timerMs(void)
{
	return gMsTick;   /* single-byte read is atomic on the 8051 core */
}

/* Timer0 overflow: reload for the next 1 ms and advance the tick. Kept tiny so
 * it adds negligible overhead at 1 kHz and does not perturb USB host timing. */
void timer0_isr(void) __interrupt(INT_NO_TMR0)
{
	TH0 = T0_RELOAD_H;
	TL0 = T0_RELOAD_L;
	gMsTick++;
}