/**
 * @file
 *
 */

#include "wordclock.h"
#include "bsp.h"
#include "qpn_port.h"
#include "serial.h"
#include "cpu-speed.h"
#include <util/delay.h>


/** The only active Wordclock. */
struct Wordclock wordclock;


Q_DEFINE_THIS_FILE;

static QState wordclockInitial        (struct Wordclock *me);
static QState wordclockState          (struct Wordclock *me);
static QState wordclockLEDOnState     (struct Wordclock *me);
static QState wordclockLEDOffState    (struct Wordclock *me);


static QEvent wordclockQueue[4];

QActiveCB const Q_ROM Q_ROM_VAR QF_active[] = {
	{ (QActive *)0            , (QEvent *)0    , 0                      },
	{ (QActive *)(&wordclock) , wordclockQueue , Q_DIM(wordclockQueue)  },
};
/* If QF_MAX_ACTIVE is incorrectly defined, the compiler says something like:
   wordclock.c:68: error: size of array ‘Q_assert_compile’ is negative
 */
Q_ASSERT_COMPILE(QF_MAX_ACTIVE == Q_DIM(QF_active) - 1);


int main(int argc, char **argv)
{
 startmain:

	serial_init();
	SERIALSTR_DRAIN("\r\n*** Word Clock ***\r\nStarting\r\n");

	BSP_startmain();
	wordclock_ctor();
	BSP_init(); /* initialize the Board Support Package */

	//Q_ASSERT(0);
	QF_run();

	goto startmain;
}

void wordclock_ctor(void)
{
	QActive_ctor((QActive *)(&wordclock), (QStateHandler)&wordclockInitial);
}


static QState wordclockInitial(struct Wordclock *me)
{
	return Q_TRAN(&wordclockLEDOnState);
}


static QState wordclockState(struct Wordclock *me)
{
	switch (Q_SIG(me)) {
	case WATCHDOG_SIGNAL:
		BSP_watchdog(me);
		return Q_HANDLED();
	}
	return Q_SUPER(&QHsm_top);
}


static QState wordclockLEDOnState(struct Wordclock *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		BSP_ledOn();
		QActive_arm((QActive*)me, 30);
		return Q_HANDLED();
	case Q_TIMEOUT_SIG:
		return Q_TRAN(wordclockLEDOffState);
	}
	return Q_SUPER(wordclockState);
}


static QState wordclockLEDOffState(struct Wordclock *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		BSP_ledOff();
		QActive_arm((QActive*)me, 30);
		return Q_HANDLED();
	case Q_TIMEOUT_SIG:
		return Q_TRAN(wordclockLEDOnState);
	}
	return Q_SUPER(wordclockState);
}
