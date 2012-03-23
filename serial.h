#ifndef serial_h_INCLUDED
#define serial_h_INCLUDED

#include "qpn_port.h"
#include <stdint.h>

#define SERIAL_BUFFER_SIZE 100

/**
 * Data structure used for serial reception.
 */
struct SerialLine {
	volatile uint8_t locked;
	uint8_t len;
	/**
	 * @brief Serial data is read into this buffer.
	 *
	 * After the serial interrupt routine recognises the end of a line,
	 * this buffer will be null terminated.  The end of a line is a
	 * carriage return, a line feed, or more than the buffer size.
	 */
	char data[SERIAL_BUFFER_SIZE];
};

void serial_init(void);

int  serial_send(const char *s);
int  serial_send_rom(char const Q_ROM * const Q_ROM_VAR s);
int  serial_send_int(unsigned int n);
int  serial_send_hex_int(unsigned int x);
int  serial_send_char(char c);

int  serial_trace(const char *s);
int  serial_trace_rom(char const Q_ROM * const Q_ROM_VAR s);
int  serial_trace_int(unsigned int n);
int  serial_trace_hex_int(unsigned int x);
int  serial_trace_char(char c);

void serial_drain(void);
void serial_assert(char const Q_ROM * const Q_ROM_VAR file, int line);

uint8_t tracing(void);
void traceon(void);
void traceoff(void);


/**
 * Send a constant string (stored in ROM).
 *
 * This macro takes care of the housekeeping required to send a ROM string.  It
 * creates a scope, stores the string in ROM, accessible only inside that
 * scope, and calls serialstr() to output the string.
 */
#define S(s)							\
	do {							\
		static const char PROGMEM ss[] = s;		\
		serial_send_rom(ss);				\
	} while (0)

#define SD(s)					\
	do {					\
		S(s);				\
		serial_drain();			\
	} while (0)

#define ST(s)							\
	do {							\
		static const char PROGMEM ss[] = s;		\
		serial_trace_rom(ss);				\
	} while (0)

#define STD(s)					\
	do {					\
		ST(s);				\
		serial_drain();			\
	} while (0)

#endif
