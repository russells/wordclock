#ifndef twi_h_INCLUDED
#define twi_h_INCLUDED

#include "qpn_port.h"
#include "qactive-named.h"


/**
 * Request to read from or write to the clock.
 */
struct TWIRequest {
	QActive *qactive;	/**< Where to send the result. */
	int signal;		/**< Signal to use when finished. */
	uint8_t *bytes;		/**< Where to get or put the data. */
	uint8_t address;	/**< I2C address. */
	uint8_t nbytes;		/**< Number of bytes to read or write. */
	uint8_t count;		/**< Number of bytes done. */
	uint8_t status;		/**< Return status to caller. */
};


struct TWI {
	QActiveNamed super;
	/** Pointer to the current request.  This must be volatile as it's used
	    by the TWI interrupt handler. */
	struct TWIRequest volatile *request;
	/** Pointer to the second request.  Used when we are doing consecutive
	    TWI operations. */
	struct TWIRequest volatile *request2;
};


enum TWICodes {
	TWI_OK = 0,		/**< Everything went ok. */
	TWI_QUEUE_FULL,		/**< Too many requests. */
	TWI_NACK,		/**< Some part of the transaction NACKEd. */
};


extern struct TWI twi;


void twi_ctor(void);


#endif
