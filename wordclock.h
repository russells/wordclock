#ifndef wordclock_h_INCLUDED
#define wordclock_h_INCLUDED

#include "qpn_port.h"
#include "qactive-named.h"
#include "twi.h"

/* Testing - stop the app and busy loop. */
#define FOREVER for(;;)


/**
 * Create the word clock.
 */
void wordclock_ctor(void);


/**
 */
struct Wordclock {
	QActiveNamed super;
	uint8_t tick20counter;
	struct TWIRequest twiRequest1;
	struct TWIRequest twiRequest2;
	/** This contains the addresses of one or both of the TWIRequests
	    above.  When we do consecutive TWI operations (which means keeping
	    control of the bus between the operations and only receiving a
	    result after both have finished) we fill in both pointers.  For a
	    single operation, only fill in the first pointer. */
	struct TWIRequest *twiRequestAddresses[2];
	/** Buffer for data to or from a TWI device.  This is only a single
	    byte since it will only be used for sending the register address to
	    the DS1307. */
	uint8_t twiBuffer1[1];
	/** Buffer for data to or from a TWI device.  This large enough to send
	    the register address and the complete register set to the
	    DS1307. */
	uint8_t twiBuffer2[9];
};

extern struct Wordclock wordclock;


/**
 * Call this just before calling QActive_post() or QActive_postISR().
 *
 * It checks that there is room in the event queue of the receiving state
 * machine.  QP-nano does this check itself anyway, but the assertion from
 * QP-nano will always appear at the same line in the same file, so we won't
 * know which state machine's queue is full.  If this check is done in user
 * code instead of library code we can tell them apart.
 */
#define fff(o)								\
	do {								\
		QActive *_me = (QActive *)(o);				\
		QActiveNamed *_men = (QActiveNamed *)(o);		\
		QActiveCB const Q_ROM *_ao = &QF_active[_me->prio];	\
		if(_me->nUsed >= Q_ROM_BYTE(_ao->end)) {		\
			S("\r\nfff( _me=");				\
			serial_send_hex_int((unsigned int)_me);		\
			S(",  name=");					\
			serial_send_hex_int((unsigned int)(_men->name)); \
			S(", ");					\
			serial_send_rom(_men->name);			\
			SD(")\r\n");					\
		}							\
		Q_ASSERT(_me->nUsed < Q_ROM_BYTE(_ao->end));		\
	} while (0)

#endif
