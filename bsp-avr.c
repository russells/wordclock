#include "bsp.h"
#include "wordclock.h"
#include "serial.h"
#include "wordclock-signals.h"

#include <avr/wdt.h>


Q_DEFINE_THIS_FILE;


static void start_tick_timer(void);
static void enable_rtc_sqw_interrupts(void);


void QF_onStartup(void)
{

}

void QF_onIdle(void)
{
	/* TODO: sleep. */
	sei();
}

void Q_onAssert(char const Q_ROM * const Q_ROM_VAR file, int line)
{
	serial_assert(file, line);
}


void BSP_watchdog(struct Wordclock *me)
{
	wdt_reset();
}


void BSP_startmain(void)
{

}


void BSP_init(void)
{
	/* PORT A pin 1 used to flash a LED, as a test. */
	DDRA |= (1 << 1);
	PINA |= (1 << 1);

	start_tick_timer();

	enable_1hz_interrupts(0);
	enable_rtc_sqw_interrupts();

	sei();

	wdt_enable(WDTO_2S);
}


/**
 * Use Timer 0 to generate periodic interrupts at 20Hz.
 */
static void
start_tick_timer(void)
{
	/*
	  WGM0[1:0] = 10, CTC mode
	  COM0[1:0] = 00, OC0 disconnected
	  CS0[2:0] = 101, CLKio/1024 = 3.6864e6/1024 = 3600
	 */
	TCCR0 = (0 << WGM00) |
		(1 << WGM01) |
		(0 << COM01) |
		(0 << COM00) |
		(0b101 << CS00);
	/*
	  3600/180 = 20.
	 */
	OCR0 = 180;
	/* Enable the output compare interrupt. */
	TIMSK |= (1 << OCIE0);
}


void BSP_ledOn(void)
{
	ST("LED on\r\n");
	PORTA |= (1 << 1);
}


void BSP_ledOff(void)
{
	ST("LED off\r\n");
	PORTA &= ~ (1 << 1);
}


SIGNAL(TIMER0_COMP_vect)
{
	static volatile uint8_t counter = 0;

	QF_tick();
	counter++;
	if (counter >= 17) {
		fff(&wordclock);
		QActive_postISR((QActive*)(&wordclock), WATCHDOG_SIGNAL, 0);
		counter = 0;
	}
	fff(&wordclock);
	QActive_postISR((QActive*)(&wordclock), TICK_20TH_SIGNAL, 0);
}


static uint8_t send_1hz_interrupts = 0;


void enable_1hz_interrupts(uint8_t onoff)
{
	send_1hz_interrupts = onoff;
}


SIGNAL(INT2_vect)
{
	if (send_1hz_interrupts) {
		fff(&wordclock);
		QActive_postISR((QActive*)(&wordclock), TICK_1S_SIGNAL, 0);
	}
}


/**
 * Enable the CPI interrupts for the RTC square wave.
 *
 * This disables and then reenables interrupts, so we assume it's called from
 * main line code, with interrupts on.
 */
static void enable_rtc_sqw_interrupts(void)
{
	cli();
	GICR &= ~(1 << INT2);	 /* Disable INT2 interrupts */
	DDRB &= ~ (1 << 2);	 /* Make INT2 an input */
	PORTB |= (1 << 2);	 /* Enable INT2 pullup */
	MCUCSR &= ~ (1 << ISC2); /* INT2 in falling edge */
	GICR |= (1 << INT2);	 /* Enable INT2 interrupts */
	sei();
}
