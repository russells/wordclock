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
#include "commander.h"
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

static void print_time(uint8_t *bytes);


static QEvent wordclockQueue[5];
static QEvent twiQueue[4];
static QEvent commanderQueue[4];

QActiveCB const Q_ROM Q_ROM_VAR QF_active[] = {
	{ (QActive *)0            , (QEvent *)0    , 0                      },
	{ (QActive *)(&wordclock) , wordclockQueue , Q_DIM(wordclockQueue)  },
	{ (QActive *)(&twi )      , twiQueue       , Q_DIM(twiQueue)        },
	{ (QActive *)(&commander) , commanderQueue , Q_DIM(commanderQueue)  },
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
	SD("***\r\n");
	SD("\r\n\r\n\r\n*** Word Clock ***\r\nStarting\r\n");
	S("Reset:");
	if (mcucsr & 0b1000)
		S(" watchdog");
	if (mcucsr & 0b0100)
		S(" brownout");
	if (mcucsr & 0b0010)
		S(" external");
	if (mcucsr & 0b0001)
		S(" poweron");
	SD("\r\n\r\n");

	BSP_startmain();
	/* Initialise the TWI first, as the wordclock sends a signal to the twi
	   as part of its entry action.  @todo Send the first signal to twi
	   after a short pause. */
	twi_ctor();
	commander_ctor();
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
	ST("WC address==");
	serial_trace_hex_int((unsigned int)(&wordclock));
	ST(" &name==");
	serial_trace_hex_int((unsigned int)(wordclockName));
	STD("\r\n");
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
	case TWI_REPLY_1_SIGNAL:
	case TWI_REPLY_2_SIGNAL:
		S("WC WTF? I got a ");
		switch (Q_SIG(me)) {
		case TWI_REPLY_SIGNAL:
			S("TWI_REPLY_SIGNAL");
			break;
		case TWI_REPLY_1_SIGNAL:
			S("TWI_REPLY_1_SIGNAL");
			break;
		case TWI_REPLY_2_SIGNAL:
			S("TWI_REPLY_2_SIGNAL");
			break;
		default:
			S("(not a TWI reply signal)");
			break;
		}
		S(" in workclockState\r\n");
		return Q_HANDLED();
	}
	return Q_SUPER(&QHsm_top);
}


static QState wordclockSetClockState(struct Wordclock *me)
{
	switch (Q_SIG(me)) {

	case Q_ENTRY_SIG:
		STD("WC setting clock\r\n");
		me->twiRequest1.qactive = (QActive*)me;
		me->twiRequest1.signal = TWI_REPLY_1_SIGNAL;
		me->twiRequest1.address = DS1307_ADDRMASK | 0b0;
		me->twiRequest1.bytes = me->twiBuffer1;

		me->twiBuffer1[0] = 0;	 /* register address */
		me->twiBuffer1[1] = 0x50; /* CH=0, seconds = 50 */
		me->twiBuffer1[2] = 0x59; /* 59 minutes */
		me->twiBuffer1[3] = 0x65; /* 12hr, 5pm */
		me->twiBuffer1[4] = 0x07; /* Sunday */
		me->twiBuffer1[5] = 0x01; /* 1st */
		me->twiBuffer1[6] = 0x01; /* January */
		me->twiBuffer1[7] = 0x01; /* 2001 */
		me->twiBuffer1[8] = 0x00; /* No square wave */

		me->twiRequest1.nbytes = 9;
		me->twiRequest1.count = 0;
		fff(&twi);
		me->twiRequestAddresses[0] = &(me->twiRequest1);
		me->twiRequestAddresses[1] = 0;
		QActive_post((QActive*)(&twi), TWI_REQUEST_SIGNAL,
			     (QParam)(me->twiRequestAddresses));
		return Q_HANDLED();

	case TWI_REPLY_1_SIGNAL:
		ST("WC Got TWI_REPLY_1_SIGNAL in set: status=");
		serial_trace_int(me->twiRequest1.status);
		STD("\r\n");
		return Q_TRAN(wordclockLEDOnState);

	}
	return Q_SUPER(wordclockState);
}


static QState wordclockLEDOnState(struct Wordclock *me)
{
	switch (Q_SIG(me)) {

	case Q_ENTRY_SIG:
		BSP_ledOn();

		me->twiRequest1.qactive = (QActive*)me;
		me->twiRequest1.signal = TWI_REPLY_1_SIGNAL;
		me->twiRequest1.address = DS1307_ADDRMASK | 0b0;
		me->twiRequest1.bytes = me->twiBuffer1;
		me->twiBuffer1[0] = 0;
		me->twiRequest1.nbytes = 1;
		me->twiRequest2.count = 0;

		me->twiRequest2.qactive = (QActive*)me;
		me->twiRequest2.signal = TWI_REPLY_2_SIGNAL;
		me->twiRequest2.address = DS1307_ADDRMASK | 0b1;
		me->twiRequest2.bytes = me->twiBuffer2;
		me->twiRequest2.nbytes = 3;
		me->twiRequest2.count = 0;

		me->twiRequestAddresses[0] = &(me->twiRequest1);
		me->twiRequestAddresses[1] = &(me->twiRequest2);

		fff(&twi);
		QActive_post((QActive*)(&twi), TWI_REQUEST_SIGNAL,
			     (QParam)(&me->twiRequestAddresses));
		QActive_arm((QActive*)me, 30);
		return Q_HANDLED();

	case TWI_REPLY_1_SIGNAL:
		if (tracing()) {
			ST("WC Got TWI_REPLY_1_SIGNAL in on: status=");
			serial_trace_int(me->twiRequest1.status);
			ST("\r\n");
		}
		return Q_HANDLED();

	case TWI_REPLY_2_SIGNAL:
		if (tracing()) {
			ST("WC Got TWI_REPLY_2_SIGNAL in on: status=");
			serial_trace_int(me->twiRequest2.status);
			ST(" ");
			if (! me->twiRequest2.status) {
				for (uint8_t i=0; i<3; i++) {
					if (i) {
						ST(",");
					}
					serial_trace_hex_int(me->twiRequest2.bytes[i]);
				}

				/* Now convert to a time. */
				if (me->twiBuffer2[0] & 0x80) {
					ST(" clock disabled");
				} else {
					ST(" time=");
					print_time(me->twiBuffer2);
				}
			}
			ST("\r\n");
		}
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
	S(":");
	if (minutes <= 9)
		S("0");
	serial_send_int(minutes);
	S(":");
	if (seconds <= 9)
		S("0");
	serial_send_int(seconds);
	if (hoursbyte & 0x40) {
		if (hoursbyte & 0x20)
			S(" PM");
		else
			S(" AM");
	} else {
		S(" (24)");
	}
}


static QState wordclockLEDOffState(struct Wordclock *me)
{
	switch (Q_SIG(me)) {

	case Q_ENTRY_SIG:
		BSP_ledOff();
		QActive_arm((QActive*)me, 30);
		return Q_HANDLED();

	case TWI_REPLY_1_SIGNAL:
		S("WC Got TWI_REPLY_1_SIGNAL in off: status=");
		serial_send_int(me->twiRequest1.status);
		S("\r\n");
		return Q_HANDLED();

	case TWI_REPLY_2_SIGNAL:
		S("WC got TWI_REPLY_2_SIGNAL in off: status=");
		serial_send_int(me->twiRequest2.status);
		S("\r\n");
		return Q_HANDLED();

	case Q_TIMEOUT_SIG:
		return Q_TRAN(wordclockLEDOnState);
	}
	return Q_SUPER(wordclockState);
}
