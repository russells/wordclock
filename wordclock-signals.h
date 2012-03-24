#ifndef wordclock_signals_h_INCLUDED
#define wordclock_signals_h_INCLUDED

#include "qpn_port.h"

enum WordclockSignals {
	/**
	 * Sent for timing, and so we can confirm that the event loop is
	 * running.
	 */
	WATCHDOG_SIGNAL = Q_USER_SIG,
	TWI_REQUEST_SIGNAL,
	TWI_REPLY_SIGNAL,
	TWI_FINISHED_SIGNAL,
	TWI_REPLY_1_SIGNAL,
	TWI_REPLY_2_SIGNAL,
	CHAR_SIGNAL,
	/** Sent 20 times a second. */
	TICK_20TH_SIGNAL,
	/** Sent by the Wordclock to itself, once per second. */
	TICK_1S_SIGNAL,
	/** Sent when we need to set the time.  Parameter is a pointer to (at
	    least) three bytes in DS1307 format. */
	SET_TIME_SIGNAL,
	MAX_PUB_SIG,
	MAX_SIG,
};

#endif
