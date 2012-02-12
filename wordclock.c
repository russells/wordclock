/**
 * @file
 *
 */

#include "wordclock.h"
#include "bsp.h"
#include "qpn_port.h"
#include "cpu-speed.h"

#include <util/delay.h>


/** The only active Wordclock. */
struct Wordclock wordclock;


Q_DEFINE_THIS_FILE;

static QState wordclockInitial        (struct Wordclock *me);
static QState wordclockState          (struct Wordclock *me);


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

	DDRA |= (1 << 1);
	PINA |= (1 << 1);
	while (1) {
		PORTA |= (1 << 1);
		_delay_ms(1500);
		PORTA &= ~ (1 << 1);
		_delay_ms(1500);
	}

	BSP_startmain();
	wordclock_ctor();
	BSP_init(); /* initialize the Board Support Package */

	QF_run();

	goto startmain;
}

void wordclock_ctor(void)
{
	QActive_ctor((QActive *)(&wordclock), (QStateHandler)&wordclockInitial);
}


static QState wordclockInitial(struct Wordclock *me)
{
	return Q_TRAN(&wordclockState);
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
