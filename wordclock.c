/**
 * @file
 *
 */

#include "wordclock.h"
#include "wordclock-signals.h"
#include "bsp.h"
#include "qpn_port.h"
#include "qactive-named.h"
#include "serial.h"
#include "twi.h"
#include "twi-status.h"
#include "ds1307.h"
#include "cpu-speed.h"
#include <util/delay.h>


/** The only active Wordclock. */
struct Wordclock wordclock;


Q_DEFINE_THIS_FILE;

static QState wordclockInitial        (struct Wordclock *me);
static QState wordclockState          (struct Wordclock *me);
static QState wordclockSetClockState  (struct Wordclock *me);
static QState wordclockLEDOnState     (struct Wordclock *me);
static QState wordclockLEDOffState    (struct Wordclock *me);


static QEvent wordclockQueue[5];
static QEvent twiQueue[4];

QActiveCB const Q_ROM Q_ROM_VAR QF_active[] = {
	{ (QActive *)0            , (QEvent *)0    , 0                      },
	{ (QActive *)(&wordclock) , wordclockQueue , Q_DIM(wordclockQueue)  },
	{ (QActive *)(&twi )      , twiQueue       , Q_DIM(twiQueue)        },
};
/* If QF_MAX_ACTIVE is incorrectly defined, the compiler says something like:
   wordclock.c:68: error: size of array ‘Q_assert_compile’ is negative
 */
Q_ASSERT_COMPILE(QF_MAX_ACTIVE == Q_DIM(QF_active) - 1);


int main(int argc, char **argv)
{
	uint8_t mcucsr;

 startmain:

	mcucsr = MCUCSR;
	MCUCSR = 0;

	serial_init();
	SERIALSTR_DRAIN("\r\n\r\n\r\n\r\n*** Word Clock ***\r\nStarting\r\n");
	SERIALSTR("Reset:");
	if (mcucsr & 0b1000)
		SERIALSTR(" watchdog");
	if (mcucsr & 0b0100)
		SERIALSTR(" brownout");
	if (mcucsr & 0b0010)
		SERIALSTR(" external");
	if (mcucsr & 0b0001)
		SERIALSTR(" poweron");
	SERIALSTR_DRAIN("\r\n\r\n");

	BSP_startmain();
	/* Initialise the TWI first, as the wordclock sends a signal to the twi
	   as part of its entry action.  @todo Send the first signal to twi
	   after a short pause. */
	twi_ctor();
	wordclock_ctor();
	BSP_init(); /* initialize the Board Support Package */

	//Q_ASSERT(0);
	QF_run();

	goto startmain;
}

void wordclock_ctor(void)
{
	static const char Q_ROM wordclockName[] = "<wordclock>";

	QActive_ctor((QActive *)(&wordclock), (QStateHandler)&wordclockInitial);
	SERIALSTR("WC address==");
	serial_send_hex_int((unsigned int)(&wordclock));
	SERIALSTR(" &name==");
	serial_send_hex_int((unsigned int)(wordclockName));
	SERIALSTR_DRAIN("\r\n");
	wordclock.super.name = wordclockName;
}


static QState wordclockInitial(struct Wordclock *me)
{
	return Q_TRAN(&wordclockSetClockState);
}


static QState wordclockState(struct Wordclock *me)
{
	switch (Q_SIG(me)) {
	case WATCHDOG_SIGNAL:
		BSP_watchdog(me);
		return Q_HANDLED();
	case TWI_REPLY_SIGNAL:
		SERIALSTR("WC Got TWI_REPLY_SIGNAL in workclockState: status=");
		serial_send_int(me->twiRequest.status);
		SERIALSTR(" (");
		serial_send_rom(twi_status_string(me->twiRequest.status));
		SERIALSTR_DRAIN(")\r\n");
		return Q_HANDLED();
	}
	return Q_SUPER(&QHsm_top);
}


static QState wordclockSetClockState(struct Wordclock *me)
{
	switch (Q_SIG(me)) {

	case Q_ENTRY_SIG:
		SERIALSTR_DRAIN("WC setting clock\r\n");
		me->twiRequest.qactive = (QActive*)me;
		me->twiRequest.signal = TWI_REPLY_SIGNAL;
		me->twiRequest.address = DS1307_ADDRMASK | 0b0;
		me->twiRequest.bytes = me->twiBuffer;

		me->twiBuffer[0] = 0;	 /* register address */
		me->twiBuffer[1] = 0x50; /* CH=0, seconds = 50 */
		me->twiBuffer[2] = 0x59; /* 28 minutes */
		me->twiBuffer[3] = 0x65; /* 12hr, 5pm */
		me->twiBuffer[4] = 0x07; /* Sunday */
		me->twiBuffer[5] = 0x01; /* 1st */
		me->twiBuffer[6] = 0x01; /* January */
		me->twiBuffer[7] = 0x01; /* 2001 */
		me->twiBuffer[8] = 0x00; /* No square wave */

		me->twiRequest.nbytes = 9;
		fff(&twi);
		QActive_post((QActive*)(&twi), TWI_REQUEST_SIGNAL,
			     (QParam)(&me->twiRequest));
		return Q_HANDLED();

	case TWI_REPLY_SIGNAL:
		SERIALSTR("WC Got TWI_REPLY_SIGNAL in set: status=");
		serial_send_int(me->twiRequest.status);
		SERIALSTR_DRAIN("\r\n");
		return Q_TRAN(wordclockLEDOnState);

	}
	return Q_SUPER(wordclockState);
}


static QState wordclockLEDOnState(struct Wordclock *me)
{
	switch (Q_SIG(me)) {

	case Q_ENTRY_SIG:
		BSP_ledOn();
		me->twiRequest.qactive = (QActive*)me;
		me->twiRequest.signal = TWI_REPLY_SIGNAL;
		me->twiRequest.address = DS1307_ADDRMASK | 0b0;
		me->twiRequest.bytes = me->twiBuffer;
		me->twiBuffer[0] = 0;
		me->twiRequest.nbytes = 1;
		fff(&twi);
		QActive_post((QActive*)(&twi), TWI_REQUEST_SIGNAL,
			     (QParam)(&me->twiRequest));
		QActive_arm((QActive*)me, 30);
		return Q_HANDLED();

	case TWI_REPLY_SIGNAL:
		SERIALSTR("WC Got TWI_REPLY_SIGNAL in on: status=");
		serial_send_int(me->twiRequest.status);
		SERIALSTR("\r\n");
		return Q_HANDLED();

	case Q_TIMEOUT_SIG:
		return Q_TRAN(wordclockLEDOffState);
	}
	return Q_SUPER(wordclockState);
}


static void print_time(uint8_t *bytes)
{
	uint8_t hoursbyte;
	uint8_t minutesbyte;
	uint8_t secondsbyte;
	uint8_t hours;
	uint8_t minutes;
	uint8_t seconds;

	secondsbyte = bytes[0];
	minutesbyte = bytes[1];
	hoursbyte = bytes[2];

	if (hoursbyte & 0x40) {
		/* 12 hour mode */
		hours = (hoursbyte & 0x0f) + ((hoursbyte & 0x10) >> 4) * 10;
	} else {
		hours = (hoursbyte & 0x0f) + ((hoursbyte & 0x30) >> 4)* 10;
	}
	minutes = (minutesbyte & 0x0f) + ((minutesbyte & 0x70) >> 4) * 10;
	seconds = (secondsbyte & 0x0f) + ((secondsbyte & 0x70) >> 4) * 10;

	serial_send_int(hours);
	SERIALSTR(":");
	if (minutes <= 9)
		SERIALSTR("0");
	serial_send_int(minutes);
	SERIALSTR(":");
	if (seconds <= 9)
		SERIALSTR("0");
	serial_send_int(seconds);
	if (hoursbyte & 0x40) {
		if (hoursbyte & 0x20)
			SERIALSTR(" PM");
		else
			SERIALSTR(" AM");
	} else {
		SERIALSTR(" (24)");
	}
}


static QState wordclockLEDOffState(struct Wordclock *me)
{
	switch (Q_SIG(me)) {

	case Q_ENTRY_SIG:
		BSP_ledOff();
		me->twiRequest.qactive = (QActive*)me;
		me->twiRequest.signal = TWI_REPLY_SIGNAL;
		me->twiRequest.address = DS1307_ADDRMASK | 0b1;
		me->twiRequest.bytes = me->twiBuffer;
		me->twiBuffer[0] = me->twiBuffer[1] = me->twiBuffer[2] = 0;
		me->twiRequest.nbytes = 3;
		fff(&twi);
		QActive_post((QActive*)(&twi), TWI_REQUEST_SIGNAL,
			     (QParam)(&me->twiRequest));
		QActive_arm((QActive*)me, 30);
		return Q_HANDLED();

	case TWI_REPLY_SIGNAL:
		SERIALSTR("WC Got TWI_REPLY_SIGNAL in off: status=");
		serial_send_int(me->twiRequest.status);
		SERIALSTR(" ");
		if (! me->twiRequest.status) {
			for (uint8_t i=0; i<3; i++) {
				if (i) {
					SERIALSTR(",");
				}
				serial_send_hex_int(me->twiRequest.bytes[i]);
			}

			/* Now convert to a time. */
			if (me->twiBuffer[0] & 0x80) {
				SERIALSTR(" clock disabled");
			} else {
				SERIALSTR(" time=");
				print_time(me->twiBuffer);
			}
		}
		SERIALSTR("\r\n");

		return Q_HANDLED();

	case Q_TIMEOUT_SIG:
		return Q_TRAN(wordclockLEDOnState);
	}
	return Q_SUPER(wordclockState);
}
