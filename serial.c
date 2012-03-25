#include "wordclock.h"
#include "serial.h"
#include "commander.h"
#include "wordclock-signals.h"
#include <avr/wdt.h>
#include "cpu-speed.h"
#include <util/delay.h>


Q_DEFINE_THIS_FILE;


#ifdef WORDCLOCK_TRACING
uint8_t trace = 1;
#else
uint8_t trace = 0;
#endif


void traceon(void) { trace = 1; }
void traceoff(void) { trace = 0; }
uint8_t tracing(void) { return trace; }

void
serial_init(void)
{
	cli();

	/* Set the baud rate.  ClockIO = 3.6864MHz, baud = 38400.
	   doc8161.pdf, p179 and p203. */
	UBRRH = 0;
	UBRRL = 5;

	/* Ensure that U2X=0. */
	UCSRA = 0;

	UCSRB = (1<<RXCIE) |	/* No rx interrupts (yet) */
		(0<<TXCIE) |
		(0<<UDRIE) |	/* Enable tx interrupts when we have data */
		(1<<RXEN ) |	/* No rx (yet) */
		(1<<TXEN ) |	/* Tx on */
		(0<<UCSZ2) |	/* 8 bits */
		(0<<RXB8 ) |
		(0<<TXB8 );
	/* N81 */
	UCSRC = (1<<URSEL) |	/* Write UCSRC */
		(0<<UMSEL) |	/* Async */
		(0<<UPM1 ) |	/* No parity */
		(0<<UPM0 ) |
		(0<<USBS ) |	/* 1 stop bit */
		(1<<UCSZ1) |	/* 8 bits */
		(1<<UCSZ0) |	/* 8 bits */
		(0<<UCPOL);	/* Async */

	DDRD |= ( 1 << 1 );	/* Make TXD output. */

	sei();
}


/**
 * @brief Send a string from data memory out the serial port.
 *
 * If the serial send buffer is close to being overrun, we send a '!' character
 * and stop.  The '!' is not included in the character count.
 *
 * Note that the '!' character is reserved for "buffer nearly overrun" and
 * should not be sent otherwise.
 *
 * @return the number of characters that are sent.
 */
int serial_send(const char *s)
{
	int sent = 0;

	while (*s) {
		if (serial_send_char(*s++)) {
			sent++;
		} else {
			break;
		}
	}
	return sent;
}


int serial_trace(const char *s) {
	if (trace) return serial_send(s);
	else return 0;
}


/**
 * @brief Send a string from program memory out the serial port.
 *
 * @see serial_send()
 *
 * @return the number of characters that are sent.
 */
int serial_send_rom(char const Q_ROM * const Q_ROM_VAR s)
{
	char c;
	int i = 0;
	int sent = 0;

	while (1) {
		c = Q_ROM_BYTE(s[i++]);
		if (!c) {
			break;
		}
		if (serial_send_char(c)) {
			sent++;
		} else {
			break;
		}
	}
	return sent;
}


int serial_trace_rom(char const Q_ROM * const Q_ROM_VAR s) {
	if (trace) return serial_send_rom(s);
	else return 0;
}


/**
 * @brief The number of bytes that can be queued for sending.
 *
 * This buffer needs to be a reasonable size, since during development we send
 * output once per second.  If the buffer is too small, we will lose data.
 * (Lost data is indicated by the '!' character - see serial_send_char().)
 *
 * Ideally, make this at least the maximum number of bytes we will ever send
 * inside one second.
 *
 * @note The number of bytes that can actually be queued is one less than this
 * value, due to the way that the ring buffer works.
 */
#define SEND_BUFFER_SIZE 120

static char sendbuffer[SEND_BUFFER_SIZE];
static volatile uint8_t sendhead = 0;
static volatile uint8_t sendtail = 0;


static uint8_t
sendbuffer_space(void)
{
	if (sendhead == sendtail) {
		return SEND_BUFFER_SIZE - 1;
	} else if (sendhead > sendtail) {
		return SEND_BUFFER_SIZE - 1 - (sendhead - sendtail);
	} else {
		/* sendhead < sendtail */
		return sendtail - sendhead - 1;
	}
}


/**
 * @brief Put one character into the serial send buffer.
 *
 * We assume that the buffer has already been checked for space.
 *
 * Interrupts should be off when calling this function.
 */
static void
put_into_buffer(char c)
{
	sendbuffer[sendhead] = c;
	sendhead++;
	if (sendhead >= SEND_BUFFER_SIZE)
		sendhead = 0;
	UCSRB |= (1 << UDRIE);
}


/**
 * @brief Send a single character out the serial port.
 *
 * If there is no space in the send buffer, do nothing.  If there is only one
 * character's space, send a '!' character.  Otherwise send the given
 * character.  We never busy wait for buffer space, since that can lead to
 * QP-nano event queues filling up as we wouldn't be handling events while busy
 * waiting.  (This has happened more than once during development.)
 *
 * The '!' character is reserved for indicating that the buffer was nearly
 * overrun, and shouldn't be sent otherwise.  If you think you need the
 * exclamation mark for emphasis, you're wrong.
 *
 * @return 1 if the given character was put into the buffer, 0 otherwise.  Note
 * that we also return 0 if '!' was put into the buffer, to indicate that we've
 * nearly overrun the buffer.
 */
int serial_send_char(char c)
{
	int available;
	int sent;
	uint8_t sreg;

	sreg = SREG;
	cli();
	available = sendbuffer_space();
	if (available >= 1) {
		if (available == 1) {
			put_into_buffer('!');
			sent = 0;
		} else {
			put_into_buffer(c);
			sent = 1;
		}
	} else {
		sent = 0;
	}
	SREG = sreg;
	return sent;
}


SIGNAL(USART_UDRE_vect)
{
	char c;

	//TOGGLE_ON();

	if (sendhead == sendtail) {
		UCSRB &= ~ (1 << UDRIE);
	} else {
		c = sendbuffer[sendtail];
		sendtail++;
		if (sendtail >= SEND_BUFFER_SIZE)
			sendtail = 0;
		UDR = c;
	}
}


static void
serial_send_noint(uint8_t byte)
{
	while ( !( UCSRA & (1<<UDRE)) );
	UDR = byte;
	while ( !( UCSRA & (1<<UDRE)) );
}


void serial_assert(char const Q_ROM * const Q_ROM_VAR file, int line)
{
	int i;
	char number[10];

	/* Turn off everything. */
	cli();
	wdt_reset();
	wdt_disable();

	/* Rashly assume that the UART is configured. */
	serial_send_noint('\r');
	serial_send_noint('\n');
	serial_send_noint('A');
	serial_send_noint('S');
	serial_send_noint('S');
	serial_send_noint('E');
	serial_send_noint('R');
	serial_send_noint('T');
	serial_send_noint(' ');

	i = 0;
	while (1) {
		char c = Q_ROM_BYTE(file[i++]);
		if (!c)
			break;
		serial_send_noint(c);
	}
	serial_send_noint(' ');

	if (line) {
		char *cp = number+9;
		*cp = '\0';
		while (line) {
			int n = line % 10;
			*--cp = (char)(n+'0');
			line /= 10;
		}
		while (*cp) {
			serial_send_noint(*cp++);
		}
	} else {
		serial_send_noint('0');
	}
	serial_send_noint('\r');
	serial_send_noint('\n');

	while (1) {
		_delay_ms(10);
	}
}


int serial_send_int(unsigned int n)
{
	char buf[10];
	char *bufp;

	bufp = buf + 9;
	*bufp = '\0';
	if (0 == n) {
		bufp--;
		*bufp = '0';
	} else {
		while (n) {
			int nn = n % 10;
			bufp--;
			*bufp = (char)(nn + '0');
			n /= 10;
		}
	}
	return serial_send(bufp);
}


int serial_trace_int(unsigned int n) {
	if (trace) return serial_send_int(n);
	else return 0;
}


int serial_send_hex_int(unsigned int x)
{
	char buf[10];
	char *bufp;

	static PROGMEM char hexchars[] = "0123456789ABCDEF";

	bufp = buf + 9;
	*bufp = '\0';
	if (0 == x) {
		bufp--;
		*bufp = '0';
	} else {
		while (x) {
			int xx = x & 0x0f;
			char c = pgm_read_byte_near(&(hexchars[xx]));
			bufp--;
			*bufp = c;
			x >>= 4;
		}
	}
	return serial_send(bufp);
}


int serial_trace_hex_int(unsigned int x) {
	if (trace) return serial_send_hex_int(x);
	else return 0;
}


void serial_drain(void)
{
	while (sendhead != sendtail)
		;
}


SIGNAL(USART_RXC_vect)
{
	uint8_t data;

	data = UDR;
	fff(&commander);
	QActive_postISR((QActive*)(&commander), CHAR_SIGNAL, data);
}
