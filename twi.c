#include "twi.h"
#include "twi-status.h"
#include "wordclock-signals.h"
#include "serial.h"

#include "wordclock.h"

#include "cpu-speed.h"
#include <util/delay.h>


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
 * @todo Allow a connection to do a write followed by a read, without releasing
 * the TWI bus.  The typical sequence would be START -> SLA+W -> write internal
 * slave address -> REPEATED START -> SLA+R -> read data from slave -> STOP.
 */


Q_DEFINE_THIS_FILE;


/** The DS1307 is represented by this. */
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


static void start_request(struct TWI *);


void twi_ctor(void)
{
	static const char Q_ROM twiName[] = "<twi>";

	QActive_ctor((QActive*)(&twi), (QStateHandler)&twiInitial);
	twi_init();
	twi.request = 0;
	SERIALSTR("TWI address==");
	serial_send_hex_int((unsigned int)(&twi));
	SERIALSTR(" &name==");
	serial_send_hex_int((unsigned int)(twiName));
	SERIALSTR_DRAIN("\r\n");
	twi.super.name = twiName;
}


/**
 * Set up the TWI bit rate.
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
	struct TWIRequest *request;

	switch (Q_SIG(me)) {

	case Q_ENTRY_SIG:
		return Q_HANDLED();

	case TWI_REQUEST_SIGNAL:
		request = (struct TWIRequest *)Q_PAR(me);
		SERIALSTR("TWI Got TWI_REQUEST_SIGNAL\r\n");
		Q_ASSERT( ! me->request );
		me->request = request;
		return Q_TRAN(twiBusyState);

	case Q_TIMEOUT_SIG:
		SERIALSTR("TWI timeout without outstanding request\r\n");
		return Q_HANDLED();

	}
	return Q_SUPER(&QHsm_top);
}


static QState twiBusyState(struct TWI *me)
{
	switch (Q_SIG(me)) {

	case Q_ENTRY_SIG:
		SERIALSTR_DRAIN("TWI > twiBusyState\r\n");
		start_request(me);
		return Q_HANDLED();

	case Q_EXIT_SIG:
		SERIALSTR_DRAIN("TWI < twiBusyState\r\n");
		return Q_HANDLED();

	case TWI_REQUEST_SIGNAL:
		me->request->status = TWI_QUEUE_FULL;
		SERIALSTR_DRAIN("TWI got excess TWI_REQUEST_SIGNAL\r\n");
		fff(me->request->qactive);
		QActive_post(me->request->qactive, me->request->signal,
			     (QParam)me->request);
		return Q_HANDLED();

	case TWI_REPLY_SIGNAL:
		SERIALSTR_DRAIN("TWI got TWI_REPLY_SIGNAL\r\n");
		fff(me->request->qactive);
		QActive_post(me->request->qactive, TWI_REPLY_SIGNAL,
			     (QParam)me->request);
		me->request = 0;
		return Q_TRAN(twiState);

	}
	return Q_SUPER(twiState);
}


static void start_request(struct TWI *me)
{
	SERIALSTR("TWI addr=");
	serial_send_hex_int(me->request->address & 0xfe);
	if (me->request->address & 0b1) {
		SERIALSTR("(r)");
	} else {
		SERIALSTR("(w)");
	}
	SERIALSTR(" nbytes=");
	serial_send_int(me->request->nbytes);
	SERIALSTR_DRAIN("\r\n");
	me->request->count = 0;
	send_start(me);
}


SIGNAL(TWI_vect)
{
	static uint8_t counter = 0;

	counter ++;
	if (0 == counter)
		SERIALSTR(",");
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
	SERIALSTR("<TWI>");
	/* Disable the TWI.  We need to set TWINT in order to reset the
	   internal value of TWINT. */
	TWCR = (1 << TWINT);
}


static void twi_error(struct TWI *me, uint8_t status)
{
	twint = twint_null;
	/* Transmit a STOP. */
	TWCR =  (1 << TWINT) |
		(1 << TWSTO) |
		(1 << TWEN );
	me->request->status = status;
	fff(me);
	QActive_postISR((QActive*)me, TWI_REPLY_SIGNAL, 0);
}


/**
 * Called when we expect to have sent a start condition and need to next send
 * the TWI bus address (SLA+W).
 */
static void twint_start_sent(struct TWI *me)
{
	uint8_t status;

	SERIALSTR("{A");
	status = TWSR & 0xf8;
	switch (status) {
	case TWI_08_START_SENT:
	case TWI_10_REPEATED_START_SENT:
		if (me->request->address & 0b1) {
			SERIALSTR("r");
			twint = twint_MR_address_sent;
		} else {
			SERIALSTR("t");
			twint = twint_MT_address_sent;
		}
		TWDR = me->request->address; /* Address includes R/W */
		TWCR =  (1 << TWINT) |
			(1 << TWEN ) |
			(1 << TWIE );
		break;
	default:
		Q_ASSERT(0);
		twi_error(me, status);
		break;
	}
	SERIALSTR("}");
}


/**
 * Called in MT mode, when we are sending data.
 *
 * @todo separate this into two functions, one for MR mode and one for MT mode.
 */
static void twint_MT_address_sent(struct TWI *me)
{
	uint8_t status;

	SERIALSTR("{B");
	status = TWSR & 0xf8;
	switch (status) {
	case TWI_18_MT_SLA_W_TX_ACK_RX:
		SERIALSTR("a");
		/* We've sent an address or previous data, and got an ACK.  If
		   there is data to send, send the first byte.  If not,
		   finish. */
		if (me->request->nbytes) {
			uint8_t data = me->request->bytes[0];
			me->request->count ++;
			TWDR = data;
			twint = twint_MT_data_sent;
			TWCR =  (1 << TWINT) |
				(1 << TWEN ) |
				(1 << TWIE );
		} else {
			SERIALSTR("n");
			/* No more data. */
			twint = twint_null;
			TWCR =  (1 << TWINT) |
				(1 << TWSTO) |
				(1 << TWEN ) |
				(1 << TWIE );
		}
		break;

	case TWI_20_MT_SLA_W_TX_NACK_RX:
		SERIALSTR("n");
		/* We've sent an address or previous data, and got a NACK. */
		me->request->status = status;
		twint = twint_null;
		TWCR =  (1 << TWINT) |
			(1 << TWSTO) |
			(1 << TWEN ) |
			(1 << TWIE );
		break;
	default:
		Q_ASSERT(0);
		twi_error(me, status);
		break;
	}
	SERIALSTR("}");
}


static void twint_MT_data_sent(struct TWI *me)
{
	uint8_t status;
	uint8_t data;

	SERIALSTR("{C");

	status = TWSR & 0xf8;
	switch (status) {

	case TWI_28_MT_DATA_TX_ACK_RX:
		if (me->request->count >= me->request->nbytes) {
			/* finished */
			SERIALSTR("l");
			twint = twint_null;
			TWCR =  (1 << TWINT) |
				(1 << TWSTO) |
				(1 << TWEN ) |
				(1 << TWIE );
			fff(me);
			QActive_postISR((QActive*)me, TWI_REPLY_SIGNAL, 0);
		} else {
			data = me->request->bytes[me->request->count];
			me->request->count ++;
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
		Q_ASSERT(0);
		/* Ah nu */
		break;
	}
	SERIALSTR("}");
}


static void twint_MR_address_sent(struct TWI *me)
{
	uint8_t status;

	SERIALSTR("{D");

	status = TWSR & 0xf8;
	switch (status) {

	case TWI_40_MR_SLA_R_TX_ACK_RX:
		switch (me->request->nbytes) {
		case 0:
			/* No data to receive, so stop now. */
			SERIALSTR("0");
			twint = twint_null;
			TWCR =  (1 << TWINT) |
				(1 << TWEN ) |
				(1 << TWSTO);
			break;

		case 1:
			/* We only want one byte, so make sure we NACK this
			   first byte. */
			SERIALSTR("1");
			twint = twint_MR_data_received;
			TWCR =  (1 << TWINT) |
				(1 << TWEN ) |
				(1 << TWIE );
			break;

		default:
			/* We want more than one byte, so we have to ACK this
			   one to convince the slave to continue. */
			SERIALSTR("+");
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

	SERIALSTR("}");
}


static void twint_MR_data_received(struct TWI *me)
{
	/* FIXME */
	uint8_t status;
	uint8_t data;

	SERIALSTR("{E");

	status = TWSR & 0xf8;
	switch (status) {

	case TWI_50_MR_DATA_RX_ACK_TX:
		SERIALSTR("a");
		data = TWDR;
		me->request->bytes[me->request->count] = data;
		me->request->count ++;
		if (me->request->count == me->request->nbytes - 1) {
			/* Only one more byte required, so NACK that byte. */
			SERIALSTR("l");
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
		SERIALSTR("n");
		data = TWDR;
		me->request->bytes[me->request->count] = data;
		me->request->count ++;
		twint = twint_null;
		TWCR =  (1 << TWINT) |
			(1 << TWEN ) |
			(1 << TWSTO);
		fff(me);
		QActive_postISR((QActive*)me, TWI_REPLY_SIGNAL, 0);
		break;
	}

	SERIALSTR("}");
}
