/**
 * @file
 *
 * Connect to a device via TWI and transfer data.
 *
 * We run two state machines here.  The QP-nano state machine is a very simple
 * QHsm.  In addition, the interrupt handler is implemented as an informal FSM
 * indexed by function pointer.  The real interrupt handler calls through that
 * function pointer to the handler, and the handler sets the next state by
 * changing the function pointer.
 *
 * @todo Handle the errors that can be produced by the TWI at each stage of a
 * transaction.  Ensure we handle all the cases mentioned in the ATmega32
 * documentation.
 */


#include "twi.h"
#include "twi-status.h"
#include "wordclock-signals.h"
#include "serial.h"

#include "wordclock.h"

#include "cpu-speed.h"
#include <util/delay.h>


Q_DEFINE_THIS_FILE;


/**
 * Interface to a TWI slave.
 */
struct TWI twi;


static QState twiInitial        (struct TWI *me);
static QState twiState          (struct TWI *me);
static QState twiBusyState      (struct TWI *me);

typedef void (*TWIInterruptHandler)(struct TWI *me);

static void twi_init(void);


/**
 * This function gets called from the TWI interrupt handler.
 *
 * This function pointer implements the interrupt state machine, and is set to
 * different handlers depending on what state we expect the TWI bus to be in at
 * the next interrupt.  Each handler is expected to check for its own errors,
 * reenable the bus for the next part of the transaction, and then set the
 * handler to the correct function for the next TWI bus state.
 */
volatile TWIInterruptHandler twint;

/**
 * Atomically set the interrupt handler state function pointer.
 *
 * Only call this when setting the function pointer with interrupts on, as it
 * will ensure interrupts are off before changing the function pointer.  If you
 * want to set the function pointer inside an interrupt handler (or inside one
 * of the interrupt state machine functions) don't bother calling this - just
 * set it.
 *
 * @param handler the interrupt state machine function pointer
 *
 * @param twcr if @e set_twcr is non-zero, also write @e twcr into TWCR, the
 * TWI Control Register
 *
 * @param set_twcr if non-zero, write @e twcr into TWCR
 */
static void set_twint(TWIInterruptHandler handler,
		      uint8_t twcr, uint8_t set_twcr);

static void send_start(struct TWI *me);

static void twint_null(struct TWI *me);
static void twint_start_sent(struct TWI *me);
static void twint_MT_address_sent(struct TWI *me);
static void twint_MR_address_sent(struct TWI *me);
static void twint_MT_data_sent(struct TWI *me);
static void twint_MR_data_received(struct TWI *me);

static void twi_int_error(struct TWI *me, uint8_t status);

static void start_request(struct TWI *);


void twi_ctor(void)
{
	static const char Q_ROM twiName[] = "<twi>";

	QActive_ctor((QActive*)(&twi), (QStateHandler)&twiInitial);
	twi_init();
	twi.requests[0] = 0;
	twi.requests[1] = 0;
	twi.requestIndex = 0;
	ST("TWI address==");
	serial_trace_hex_int((unsigned int)(&twi));
	ST(" &name==");
	serial_trace_hex_int((unsigned int)(twiName));
	STD("\r\n");
	twi.super.name = twiName;
}


/**
 * Set up the TWI bit rate and default interrupt function.
 */
static void twi_init(void)
{
	set_twint(twint_null, 0, 0);
	TWCR = 0;
	TWSR = 0;		/* Prescaler = 4^0 = 1 */
	TWBR=10;		/* Approx 100kbits/s SCL */
}


static QState twiInitial(struct TWI *me)
{
	return Q_TRAN(twiState);
}


static QState twiState(struct TWI *me)
{
	struct TWIRequest **requestp;
	struct TWIRequest *request;

	switch (Q_SIG(me)) {

	case Q_ENTRY_SIG:
		return Q_HANDLED();

	case TWI_REQUEST_SIGNAL:
		requestp = (struct TWIRequest **)Q_PAR(me);
		Q_ASSERT( requestp );
		request = *requestp;
		ST("TWI Got TWI_REQUEST_SIGNAL\r\n");
		Q_ASSERT( request );
		Q_ASSERT( ! me->requests[0] );
		me->requests[0] = request;
		requestp++;
		request = *requestp;
		Q_ASSERT( ! me->requests[1] );
		me->requests[1] = request;
		me->requestIndex = 0;
		return Q_TRAN(twiBusyState);

	case Q_TIMEOUT_SIG:
		ST("TWI timeout without outstanding request\r\n");
		return Q_HANDLED();

	}
	return Q_SUPER(&QHsm_top);
}


/**
 * Wait here until the interrupt state machine tells us it's finished the
 * TWI requests.  Reject any further TWI requests.
 */
static QState twiBusyState(struct TWI *me)
{
	uint8_t r;
	uint8_t sreg;
	struct TWIRequest **requestp;

	switch (Q_SIG(me)) {

	case Q_ENTRY_SIG:
		STD("TWI > twiBusyState\r\n");
		start_request(me);
		return Q_HANDLED();

	case Q_EXIT_SIG:
		STD("TWI < twiBusyState\r\n");
		sreg = SREG;
		cli();
		me->requests[0] = 0;
		me->requests[1] = 0;
		me->requestIndex = 0;
		SREG = sreg;
		return Q_HANDLED();

	case TWI_REQUEST_SIGNAL:
		STD("TWI got excess TWI_REQUEST_SIGNAL\r\n");
		requestp = (struct TWIRequest **)Q_PAR(me);
		if (requestp[0]) {
			requestp[0]->status = TWI_QUEUE_FULL;
			fff(requestp[0]->qactive);
			QActive_post(requestp[0]->qactive, requestp[0]->signal,
				     (QParam)requestp[0]);
		}
		if (requestp[1]) {
			requestp[1]->status = TWI_QUEUE_FULL;
			fff(requestp[1]->qactive);
			QActive_post(requestp[1]->qactive, requestp[1]->signal,
				     (QParam)requestp[1]);
		}
		return Q_HANDLED();

	case TWI_REPLY_SIGNAL:
		STD("TWI got TWI_REPLY_SIGNAL\r\n");
		r = (uint8_t) Q_PAR(me);
		fff(me->requests[r]->qactive);
		QActive_post(me->requests[r]->qactive, me->requests[r]->signal,
			     (QParam)me->requests[r]);
		return Q_HANDLED();

	case TWI_FINISHED_SIGNAL:
		return Q_TRAN(twiState);

	}
	return Q_SUPER(twiState);
}


/**
 * Called at the very start of a request.
 *
 * The request can be either a single request, or a chain of two.  The chaining
 * of requests (with a REPEATED START) is handled later in the interrupt
 * handler.
 */
static void start_request(struct TWI *me)
{
	Q_ASSERT( ! me->requestIndex );
	ST("TWI addr=");
	serial_trace_hex_int(me->requests[0]->address & 0xfe);
	if (me->requests[0]->address & 0b1) {
		ST("(r)");
	} else {
		ST("(w)");
	}
	ST(" nbytes=");
	serial_trace_int(me->requests[0]->nbytes);
	STD("\r\n");
	me->requests[0]->count = 0;
	send_start(me);
}


/**
 * Interrupt handler for the TWI.  Not much work is done by this function -
 * it's all done by calling the interrupt state function.
 */
SIGNAL(TWI_vect)
{
	static uint8_t counter = 0;

	counter ++;
	if (0 == counter)
		ST(",");
	if (! twi.requests[twi.requestIndex]) {
		twint = twint_null;
	}
	(*twint)(&twi);
}


static void set_twint(TWIInterruptHandler handler,
		      uint8_t twcr, uint8_t set_twcr)
{
	uint8_t sreg;

	sreg = SREG;
	cli();
	twint = handler;
	if (set_twcr) {
		TWCR = twcr;
	}
	SREG = sreg;
}


static void send_start(struct TWI *me)
{
	set_twint(twint_start_sent,
		  (1 << TWINT) |
		  (1 << TWSTA) |
		  (1 << TWEN ) |
		  (1 << TWIE ),
		  1);
}


/**
 * Default interrupt handler that disables the TWI.
 */
static void twint_null(struct TWI *me)
{
	/* Notify that we have been called.  This should never happen. */
	ST("<TWI>");
	/* Disable the TWI.  We need to set TWINT in order to reset the
	   internal value of TWINT. */
	TWCR = (1 << TWINT);
}


/**
 * Handle an error detected during the interrupt handler.
 */
static void twi_int_error(struct TWI *me, uint8_t status)
{
	ST("<E>");
	twint = twint_null;
	/* Transmit a STOP. */
	TWCR =  (1 << TWINT) |
		(1 << TWSTO) |
		(1 << TWEN );
	me->requests[me->requestIndex]->status = status;
	fff(me);
	QActive_postISR((QActive*)me, TWI_REPLY_SIGNAL, me->requestIndex);
}


/**
 * Called when we expect to have sent a start condition and need to next send
 * the TWI bus address (SLA+R/W).
 */
static void twint_start_sent(struct TWI *me)
{
	uint8_t status;

	status = TWSR & 0xf8;
	switch (status) {
	case TWI_08_START_SENT:
	case TWI_10_REPEATED_START_SENT:
		if (me->requests[me->requestIndex]->address & 0b1) {
			twint = twint_MR_address_sent;
		} else {
			twint = twint_MT_address_sent;
		}
		/* Address includes R/W */
		TWDR = me->requests[me->requestIndex]->address;
		TWCR =  (1 << TWINT) |
			(1 << TWEN ) |
			(1 << TWIE );
		break;
	default:
		Q_ASSERT(0);
		twi_int_error(me, status);
		break;
	}
}


/**
 * Called in MT mode, when we are sending data.
 */
static void twint_MT_address_sent(struct TWI *me)
{
	uint8_t status;

	status = TWSR & 0xf8;
	switch (status) {
	case TWI_18_MT_SLA_W_TX_ACK_RX:
		/* We've sent an address or previous data, and got an ACK.  If
		   there is data to send, send the first byte.  If not,
		   finish. */
		if (me->requests[me->requestIndex]->nbytes) {
			uint8_t data = me->requests[me->requestIndex]->bytes[0];
			me->requests[me->requestIndex]->count ++;
			TWDR = data;
			twint = twint_MT_data_sent;
			TWCR =  (1 << TWINT) |
				(1 << TWEN ) |
				(1 << TWIE );
		} else {
			/* No more data. */
			twint = twint_null;
			TWCR =  (1 << TWINT) |
				(1 << TWSTO) |
				(1 << TWEN ) |
				(1 << TWIE );
		}
		break;

	case TWI_20_MT_SLA_W_TX_NACK_RX:
		/* We've sent an address or previous data, and got a NACK. */
		me->requests[me->requestIndex]->status = status;
		twint = twint_null;
		TWCR =  (1 << TWINT) |
			(1 << TWSTO) |
			(1 << TWEN ) |
			(1 << TWIE );
		break;
	default:
		Q_ASSERT(0);
		twi_int_error(me, status);
		break;
	}
}


/**
 * Called in master transmitter mode, after data has been sent.
 */
static void twint_MT_data_sent(struct TWI *me)
{
	uint8_t status;
	uint8_t data;
	volatile struct TWIRequest *request;

	status = TWSR & 0xf8;
	switch (status) {

	case TWI_28_MT_DATA_TX_ACK_RX:
		request = me->requests[me->requestIndex];
		if (request->count >= request->nbytes) {
			/* finished */
			fff(me);
			QActive_postISR((QActive*)me, TWI_REPLY_SIGNAL,
					me->requestIndex);

			if ((0 == me->requestIndex) && me->requests[1]) {
				me->requestIndex ++;
				Q_ASSERT( me->requestIndex == 1 );
				twint = twint_start_sent;
				TWCR =  (1 << TWINT) |
					(1 << TWEN ) |
					(1 << TWIE ) |
					(1 << TWSTA);
			} else {
				fff(me);
				QActive_postISR((QActive*)me, TWI_FINISHED_SIGNAL,
						0);
				twint = twint_null;
				TWCR =  (1 << TWINT) |
					(1 << TWEN ) |
					(1 << TWSTO);
			}

		} else {
			data = request->bytes[request->count];
			request->count ++;
			TWDR = data;
			/* All good, keep going */
			TWCR =  (1 << TWINT) |
				(1 << TWEN ) |
				(1 << TWIE );
		}
		break;

	case TWI_30_MT_DATA_TX_NACK_RX:
		Q_ASSERT(0);
		/* Ah nu */
		break;

	default:
		serial_send_hex_int(status);
		_delay_ms(100);
		Q_ASSERT(0);
		/* Ah nu */
		break;
	}
}


/**
 * Called in master receiver mode, after the address has been sent.
 */
static void twint_MR_address_sent(struct TWI *me)
{
	uint8_t status;

	status = TWSR & 0xf8;
	switch (status) {

	case TWI_40_MR_SLA_R_TX_ACK_RX:
		switch (me->requests[me->requestIndex]->nbytes) {
		case 0:
			/* No data to receive, so stop now. */
			twint = twint_null;
			TWCR =  (1 << TWINT) |
				(1 << TWEN ) |
				(1 << TWSTO);
			break;

		case 1:
			/* We only want one byte, so make sure we NACK this
			   first byte. */
			twint = twint_MR_data_received;
			TWCR =  (1 << TWINT) |
				(1 << TWEN ) |
				(1 << TWEA ) |
				(1 << TWIE );
			break;

		default:
			/* We want more than one byte, so we have to ACK this
			   first byte to convince the slave to continue. */
			twint = twint_MR_data_received;
			TWCR =  (1 << TWINT) |
				(1 << TWEN ) |
				(1 << TWEA ) |
				(1 << TWIE );
			break;
		}
		break;

	case TWI_48_MR_SLA_R_TX_NACK_RX:
		_delay_ms(500);
		Q_ASSERT(0);
		break;

	default:
		_delay_ms(500);
		Q_ASSERT(0);
		break;
	}
}


/**
 * Called in master receiver mode, after data has been received.
 */
static void twint_MR_data_received(struct TWI *me)
{
	/* FIXME */
	uint8_t status;
	uint8_t data;
	volatile struct TWIRequest *request;

	status = TWSR & 0xf8;
	switch (status) {

	case TWI_50_MR_DATA_RX_ACK_TX:
		request = me->requests[me->requestIndex];
		data = TWDR;
		request->bytes[request->count] = data;
		request->count ++;
		if (request->count == request->nbytes - 1) {
			/* Only one more byte required, so NACK that byte. */
			TWCR =  (1 << TWINT) |
				(1 << TWEN ) |
				(1 << TWIE );
		} else {
			TWCR =  (1 << TWINT) |
				(1 << TWEN ) |
				(1 << TWEA ) |
				(1 << TWIE );
		}
		break;

	case TWI_58_MR_DATA_RX_NACK_TX:
		data = TWDR;
		request = me->requests[me->requestIndex];
		request->bytes[request->count] = data;
		request->count ++;
		/* Tell the state machine we've finished this (sub-)request. */
		fff(me);
		QActive_postISR((QActive*)me, TWI_REPLY_SIGNAL, me->requestIndex);
		/* Now check for the next request. */
		if ((0 == me->requestIndex) && me->requests[1]) {
			me->requestIndex ++;
			twint = twint_start_sent;
			TWCR =  (1 << TWINT) |
				(1 << TWEN ) |
				(1 << TWIE ) |
				(1 << TWSTA);
		} else {
			fff(me);
			QActive_postISR((QActive*)me, TWI_FINISHED_SIGNAL, 0);
			twint = twint_null;
			TWCR =  (1 << TWINT) |
				(1 << TWEN ) |
				(1 << TWSTO);
		}
		break;
	}
}
