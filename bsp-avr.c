#include "bsp.h"
#include "wordclock.h"
#include "serial.h"


static void start_tick_timer(void);


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

	sei();
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
	SERIALSTR("LED on\r\n");
	PORTA |= (1 << 1);
}


void BSP_ledOff(void)
{
	SERIALSTR("LED off\r\n");
	PORTA &= ~ (1 << 1);
}


SIGNAL(TIMER0_COMP_vect)
{
	QF_tick();
}
