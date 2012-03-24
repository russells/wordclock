/**
 * @file
 *
 * Word clock main.
 *
 * @todo Remove the LED on/LED off stuff, and make this read and print the time
 * occasionally, perhaps every five seconds.  Also, put in a timer counter that
 * sends a signal periodically.  This timer counter will run off the internal
 * clock, and get synchronised to the RTC (somehow.)
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
static QState wordclockRunningState   (struct Wordclock *me);

static void print_time(uint8_t *bytes);
static uint8_t is_5min(uint8_t *bytes);
static int8_t near_5s_diff(struct Wordclock *me, uint8_t *bytes);
static void setTick1Scounter(struct Wordclock *me, int8_t diff);


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
	wordclock.tick20counter = 0;
	wordclock.tick1Scounter = 0;
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

	case TICK_20TH_SIGNAL:
		/**
		 * @todo When we have the UI that handles button press
		 * interrupts, move the TICK_20TH_SIGNAL handler there.  It's
		 * not needed in Wordclock now that we have interrupts from the
		 * RTC square wave output.
		 */
		me->tick20counter ++;
		if (20 == me->tick20counter) {
			fff(me);
			me->tick20counter = 0;
		}
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
		me->twiBuffer1[8] = (1<<7) | (1<<4); /* 1Hz square wave */

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
		return Q_TRAN(wordclockRunningState);

	case Q_EXIT_SIG:
		me->tick1Scounter = 5;
		return Q_HANDLED();

	}
	return Q_SUPER(wordclockState);
}


static QState wordclockRunningState(struct Wordclock *me)
{
	uint8_t status;

	switch (Q_SIG(me)) {

	case Q_ENTRY_SIG:
		STD("Running...");
		enable_1hz_interrupts(1);
		STD(" RTC SQW interrupts on\r\n");
		return Q_HANDLED();

	case TICK_1S_SIGNAL:

		ST("WC 1S\r\n");

		me->interval_5min ++;

		me->tick1Scounter --;
		if (me->tick1Scounter) {
			return Q_HANDLED();
		}

		me->twiRequest1.qactive = (QActive*)me;
		me->twiRequest1.signal = TWI_REPLY_1_SIGNAL;
		me->twiRequest1.address = DS1307_ADDRMASK | 0b0;
		me->twiRequest1.bytes = me->twiBuffer1;
		me->twiBuffer1[0] = 0;
		me->twiRequest1.nbytes = 1;
		me->twiRequest1.count = 0;

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
			status = me->twiRequest1.status;
			ST("WC Got TWI_REPLY_1_SIGNAL in running: status=");
			serial_trace_int(me->twiRequest1.status);
			if (status) {
				ST(": ");
				serial_send_rom(twi_status_string(status));
			}
			ST("\r\n");
		}
		return Q_HANDLED();

	case TWI_REPLY_2_SIGNAL:
		if (tracing()) {
			status = me->twiRequest2.status;
			ST("WC Got TWI_REPLY_2_SIGNAL in running: status=");
			serial_trace_int(status);
			ST(" ");
			if (! status) {
				for (uint8_t i=0; i<3; i++) {
					if (i) {
						ST(",");
					}
					serial_trace_hex_int
						(me->twiRequest2.bytes[i]);
				}

				/* Now convert to a time. */
				if (me->twiBuffer2[0] & 0x80) {
					ST(" clock disabled");
				} else {
					ST(" time=");
					print_time(me->twiBuffer2);
				}
			} else {
				ST(": ");
				serial_trace_rom(twi_status_string(status));
				ST("\r\n");
			}
			ST("\r\n");
		} else if (is_5min(me->twiBuffer2)) {
			me->interval_5min = 0;
			S("time=");
			print_time(me->twiBuffer2);
			S("\r\n");
		}
		setTick1Scounter(me, near_5s_diff(me, me->twiRequest2.bytes));
		return Q_HANDLED();

	}
	return Q_SUPER(wordclockState);
}


/**
 * Tell us if we are on an hour boundary.
 */
static uint8_t is_5min(uint8_t *bytes)
{
	return ((bytes[0]==0)      &&
		( ((bytes[1] & 0x0f) == 0x00) ||
		  ((bytes[1] & 0x0f) == 0x05)));
}


/**
 * Tell us which way we are from a five second boundary.
 *
 * @param bytes pointer to a byte read from the first DS1307.
 *
 * @return 0 if the time is on a five second boundary; -1 or -2 if it's before
 * a five second boundary; +1 or +2 if it's after a five second boundary.
 */
static int8_t near_5s_diff(struct Wordclock *me, uint8_t *bytes)
{
	uint8_t sec;
	int8_t diff = 99;

	sec = (*bytes) & 0x0f;
	if (sec >= 10) {
		sec -= 10;
	}
	if (sec >= 5) {
		sec -= 5;
	}
	switch (sec) {
	case 4: diff = -1; break;
	case 3: diff = -2; break;
	case 2: diff =  2; break;
	case 1: diff =  1; break;
	case 0: diff =  0; break;
	}
	if (diff) {
		S("-- diff = ");
		serial_send_int(diff);
		S(" at ");
		print_time(bytes);
		S(" interval = ");
		serial_send_int(me->interval_5min);
		S("\r\n");
	}
	return diff;
}


static void setTick1Scounter(struct Wordclock *me, int8_t diff)
{
	Q_ASSERT( diff < 3 );
	Q_ASSERT( diff > -3 );
	me->tick1Scounter = 5 - diff;
}


/**
 * Print a time represented by raw DS1307 data.
 *
 * We print the time without reference to the tracing state.  It's up to the
 * caller to check that first, if required.
 *
 * @param bytes pointer to at least the first three bytes of DS1307 register
 * data, in the order read from the device.
 */
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
