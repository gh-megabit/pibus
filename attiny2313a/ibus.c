// RPI IBUS interface. Copyright (c) 2012-2013 Peter Zelezny
// All Rights Reserved

// avrdude: safemode: lfuse reads as 7D
// avrdude: safemode: hfuse reads as 9F
// avrdude: safemode: efuse reads as FF
// avrdude: safemode: Fuses OK

#define F_CPU 2457600UL	// 2.4576 MHz

#define UART_BAUD_RATE 9600
#define UART_BAUD_SELECT (F_CPU/(UART_BAUD_RATE*16l)-1)

#if UART_BAUD_SELECT != 15
#error wtf
#endif

// ====== Hardware =========================
//  PD0: UART RX
//  PD1: UART TX
//  PD2: ibus line monitor
//  PD3: RELAY1 - Pi/Camera Video Select
//  PD4: RELAY2 - Camera +12V enable
//  PD5: RELAY3 - Aux
//  PD6: heartbeat LED
//  PB0: Video Trigger (Pin17)
//  PB4: Pi power supply enable (active low)
// =========================================

/* PORTD bits */

#define D_IBUS_MASK		0x04
#define D_RELAY1_MASK		0x08
#define D_RELAY2_MASK		0x10
#define D_RELAY3_MASK		0x20
#define D_LED			0x40

/* PORTB bits */

#define B_VIDEO_TRIG		0x01
#define B_PWR_ENABLE		0x10

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/delay.h>
#include <string.h>

typedef enum
{
	VIDEO_SRC_BMW = 0,
	VIDEO_SRC_PI = 1,
	VIDEO_SRC_CAMERA = 2,
	VIDEO_SRC_LAST = 2
}
videoSource_t;

/* iBUS packet format */

#define SOURCE 0
#define LENGTH 1
#define DEST 2
#define DATA 3

#define LED_RED			0x01
#define LED_RED_BLINK		(0x02 | LED_RED)
#define LED_ORANGE		0x04
#define LED_ORANGE_BLINK	(0x08 | LED_ORANGE)
#define LED_GREEN		0x10
#define LED_GREEN_BLINK 	(0x20 | LED_GREEN)


unsigned char buf[32];
unsigned char bufPos;

uint16_t ledsOnCount;
uint16_t bootCount;
unsigned char relayOnCount;
unsigned char ledQueue;
uint16_t ledQueueTimeout;
uint16_t idleCount;

//unsigned char temperature = 0xff;
//unsigned char coolant = 0xff;

videoSource_t videoSource = VIDEO_SRC_BMW;



unsigned char sendchar(unsigned char c)
{
	while (!(UCSRA & (1<<UDRE))) ;
	UDR = c;
//	while (!(UCSRA & (1<<TXC))) ;
	return c;
}

void set_relays(unsigned char turnoff, unsigned char byte)
{
//	relayOnCount = turnoff;
//	PORTD |= byte;
}

void set_video(videoSource_t src)
{
	switch (src)
	{
		case VIDEO_SRC_BMW:
			PORTD &= ~(D_RELAY1_MASK | D_RELAY2_MASK);
			PORTB &= ~(B_VIDEO_TRIG);
			break;

		case VIDEO_SRC_PI:
			PORTB &= ~(B_PWR_ENABLE);
			PORTD &= ~(D_RELAY1_MASK | D_RELAY2_MASK);
			PORTB |= B_VIDEO_TRIG;
			break;

		case VIDEO_SRC_CAMERA:
			PORTD |= (D_RELAY1_MASK | D_RELAY2_MASK);
			PORTB |= B_VIDEO_TRIG;
			break;
	}
}

void send_text(const char *text, unsigned char length, unsigned char pos)
{
	unsigned char sum;

	sum  = sendchar(0x68);
	sum ^= sendchar(0x06 + length);
	sum ^= sendchar(0x3B);
	sum ^= sendchar(0x21);
	sum ^= sendchar(0x60);
	sum ^= sendchar(0x00);
	sum ^= sendchar(0x40 + pos);
	while (length--)
	{
		sum ^= sendchar((unsigned char)*text++);
	}
	sendchar(sum);
}

void set_leds(uint16_t turnoff, unsigned char byte)
{
	ledsOnCount = turnoff;

	sendchar(0xc8);
	sendchar(0x04);
	sendchar(0xe7);
	sendchar(0x2b);
	sendchar(byte);
	sendchar(byte);
}

void cdc_exit(void)
{
	videoSource = VIDEO_SRC_BMW;
	set_video(videoSource);
}

void check_packet()
{
	/* ============================================ */
	/* =============== Wheel: R/T ================= */
	/* ============================================ */

	if (	bufPos == 5 &&
		buf[0] == 0x50 &&
		/*buf[1] == 0x03 &&*/
		buf[2] == 0xC8 &&
		buf[3] == 0x01 &&
		buf[4] == 0x9A	)
	{
		// enable relay2 for 300ms
		//set_relays(45, RELAY2_MASK);

		// turn on GREEN LED for 3 seconds
		//ledQueue = LED_GREEN;
		//ledQueueTimeout = 450;
	}

	/* ============================================ */
	/* =============== Wheel: Speak =============== */
	/* ============================================ */

	else if (	bufPos == 6 &&
		buf[0] == 0x50 &&
		/*buf[1] == 0x04 &&*/
		buf[2] == 0xC8 &&
		buf[3] == 0x3B &&
		buf[4] == 0x80 &&
		buf[5] == 0x27	)
	{
		// enable relay3 for 300ms
		//set_relays(45, D_RELAY3_MASK);

		// turn on ORANGE LED for 1 second
		//ledQueue = LED_ORANGE;
		//ledQueueTimeout = 150;
	}

	/* ============================================ */
	/* ============= HeadUnit: Phone ============== */
	/* ============================================ */

	else if (	bufPos == 6 &&
		buf[0] == 0xF0 &&
		/*buf[1] == 0x04 &&*/
		buf[2] == 0xFF &&
		buf[3] == 0x48 &&
		buf[4] == 0x08 &&
		buf[5] == 0x4B	)
	{
		// led for 0.5 second
		ledQueueTimeout = 75;

		videoSource++;
		if (videoSource > VIDEO_SRC_LAST)
		{
			videoSource = 0;
		}

		switch (videoSource)
		{
			case VIDEO_SRC_BMW:
				ledQueue = LED_RED;
				break;
			case VIDEO_SRC_PI:
				ledQueue = LED_GREEN;
				break;
			case VIDEO_SRC_CAMERA:
				ledQueue = LED_ORANGE;
				break;
		}

		set_video(videoSource);
	}

	/* ============================================ */
	/* ============= FOB: Boot ==================== */
	/* ============================================ */

	else if (	bufPos == 6 &&
		buf[0] == 0x00 &&
		/*buf[1] == 0x04 &&*/
		buf[2] == 0xBF &&
		buf[3] == 0x72 )
	{
		if ((buf[1] ^ buf[2] ^ buf[3] ^ buf[4]) == buf[5])
		{
			if ((buf[4] & 0x40) == 0x40)
			{
				/* orange LED for 3s */
				ledQueue = LED_ORANGE;
				ledQueueTimeout = 450;
				/* Boot button is pressed - start 9s timer */
				bootCount = 1350;
			}
			else if (bootCount != 0 && bootCount < 900)
			{
				/* Boot button is not pressed but was >3s ago */

				// enable relay3 for 300ms
				set_relays(45, D_RELAY3_MASK);

				// blink all LEDs for 5 seconds
				ledQueue = LED_RED_BLINK | LED_ORANGE_BLINK | LED_GREEN_BLINK;
				ledQueueTimeout = 750;
			}
		}
	}

	/* ============================================ */
	/* ======== IKE Sensor Status ================= */
	/* ============================================ */

	// 80 0A BF 13 XX XX XX XX XX XX XX CS

	else if (bufPos == 12 &&
		buf[0] == 0x80 &&
		buf[2] == 0xBF &&
		buf[3] == 0x13 )
	{
		/*switch (buf[5] >> 4)
		{
			case  1: gear = 'R'; break;
			case  2: gear = '1'; break;
			case  4: gear = '2'; break;
			case  7: gear = 'N'; break;
			case  8: gear = 'D'; break;
			case 11: gear = 'P'; break;
			case 12: gear = '4'; break;
			case 13: gear = '3'; break;
			case 14: gear = '5'; break;
			default: gear = 'X'; break;
		}*/

		switch (buf[5] >> 4)
		{
			case 1:
				set_video(VIDEO_SRC_CAMERA);
				break;
			default:
				set_video(videoSource);
				break;
		}
	}

	/* ============================================ */
	/* ============= Temperature ================== */
	/* ============================================ */

	// 80 06 BF 19 14 5E 00 6A
	// IKE --> GLO : Temperature: Outside 20°C, Coolant 94°C

	/*else if (bufPos == 8 &&
		buf[0] == 0x80 &&
		buf[2] == 0xBF &&
		buf[3] == 0x19 )
	{
		if (temperature != buf[4])
		{
			temperature = buf[4];
		}

		if (coolant != buf[5])
		{
			coolant = buf[5];
		}
	}*/

	/* ============================================ */
	/* ============= Enter CDC Mode  ============== */
	/* ============================================ */

	// "\x68\x12\x3b\x23\x62\x10\x43\x44\x43\x20\x31\x2d\x30\x34\x20\x20\x20\x20\x20\x4c"

	else if (bufPos == 20 &&
		buf[0] == 0x68 &&
		buf[6] == 0x43 &&
		buf[13] == 0x34 &&
		buf[19] == 0x4c)
	{
		videoSource = VIDEO_SRC_PI;
		set_video(videoSource);
	}

	/* ============================================ */
	/* ============= Exit CDC Mode ================ */
	/* ============================================ */

	else if (bufPos == 6)
	{
		if (buf[0] == 0xf0 &&
		    buf[3] == 0x48)	/* buttons */
		{
			switch (buf[4])
			{
				case 0x21:	/* AM */
				case 0x23:	/* mode */
				case 0x31:	/* FM */
				case 0x34:	/* menu */
					cdc_exit();
					break;
			}
		}
		else if (buf[0] == 0x68 &&
			 buf[2] == 0x3b &&
			 buf[3] == 0x46 &&
			 buf[4] == 0x02 &&
			 buf[5] == 0x13)	/* screen: main menu */
		{
			cdc_exit();
		}
	}

	bufPos = 0;
	TCNT0  = 0x00;
}

/*
 * Called every 6.666ms
 */

ISR(TIMER0_OVF_vect)
{
	static uint16_t count = 0;

	count++;
	if (count < 2)
	{
		PORTD |= D_LED;
	}
	else
	{
		PORTD &= ~(D_LED);
	}

	/* If idle for > 6 minutes */
	if (idleCount >= 360)
	{
		idleCount = 360;
		if (count >= 750)	/* 6.666ms * 750 = 5 seconds */
		{
			count = 0;
		}
		PORTB |= B_PWR_ENABLE;
	}
	else
	{
		if (count >= 150)	/* 6.666ms * 150 = 1 seconds */
		{
			count = 0;
			idleCount++;
		}
		PORTB &= ~(B_PWR_ENABLE);
	}

	if (relayOnCount)
	{
		relayOnCount--;
		if (relayOnCount == 0)
		{
			// turn off relay3
			PORTD &= ~(D_RELAY3_MASK);
		}
	}

	if (ledsOnCount == 1 && ((PIND & D_IBUS_MASK) != 0) && bufPos == 0)
	{
		ledsOnCount--;
		// all LEDs off
		set_leds(0, 0);
	}
	else if (ledsOnCount > 1)
	{
		ledsOnCount--;
	}

	if (ledQueue && ((PIND & D_IBUS_MASK) != 0) && bufPos == 0)
	{
		set_leds(ledQueueTimeout, ledQueue);
		ledQueue = 0;
	}

	if (bootCount)
	{
		bootCount--;
	}

	/* Timeout - start a new packet */
	bufPos = 0;
}

ISR(USART_RX_vect)
{
	unsigned char status, resh, resl;

	/* reset the timer */
	TCNT0  = 0x00;

	status = UCSRA;
	resh = UCSRB;
	resl = UDR;

	/* Framing Error, Data OverRun, Parity Error */
	if (status & ((1 << FE) | (1 << DOR) | (1 << UPE)))
	{
		return;
	}

	if (bufPos < sizeof(buf))
	{
		buf[bufPos++] = resl;

		if (bufPos >= 5)
		{
			if (buf[LENGTH] + 2 != bufPos)
			{
				/* not enough bytes, keep reading */
				return;
			}
			check_packet();
			idleCount = 0;
		}
	}
}

int main()
{
	PORTD = 0x00;
	PORTB = 0x00;// | B_PWR_ENABLE;

	/* ================================================================= */
	/* === PORTS ======================================================= */
	/* ================================================================= */

	ACSR |= (1 << ACD);			// power down comparator
	PRR = (1 << PRTIM1) | (1 << PRUSI);	// Shutdown Timer1 & USI
	DDRA = 0b11111111;			// Set PORTA as output
	DDRB = 0b11111111;			// Set PORTB as output
	DDRD = 0b11111010;			// Except PD0,PD2

	/* ================================================================= */
	/* === USART: 9600 baud 8/E/1 ====================================== */
	/* ================================================================= */

	UBRRL = UART_BAUD_SELECT;
	UCSRB = (1 << RXEN) | (1 << RXCIE) | (1 << TXEN);
	UCSRC |= (1 << UPM1);			// even parity

	/* ================================================================= */
	/* === TIMER 0 ===================================================== */
	/* ================================================================= */

	TCCR0B = 0x03;				// 2.4576 MHz/64/256 = 6.666ms
	TIMSK = (1 << TOIE0);			// Timer0 interrupt enable

	/* ================================================================= */
	/* === VARIABLES =================================================== */
	/* ================================================================= */

	bufPos = 0;
	bootCount = 0;
	relayOnCount = 0;
	ledsOnCount = 0;
	ledQueue = 0;
	ledQueueTimeout = 0;
	idleCount = 0;

	/* ================================================================= */
	/* === CLOCK ======================================================= */
	/* ================================================================= */

	CLKPR = 0x80;
	CLKPR = 0x01;			// 4.9152 MHz / 2 = 2.4576 MHz

	sei();

	while (1)
	{
		MCUCR = (1 << PUD) | (1 << SE);
		sleep_cpu();
	}

	return 0;
}