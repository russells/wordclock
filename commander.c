/**
 * @file
 *
 * @brief Implement a command interpreter for the word clock.
 *
 * This is fairly simple.  It merely reads the the line and calls the
 * appropriate function for that line.  We currently sort of assume that the
 * line is a one word command.
 *
 * This is a QP-nano HSM, but only because that is an easy way to get character
 * events to it from an ISR (using QActive_postISR()).
 *
 * @todo Actually separate the first word from the line, and call the
 * appropriate function for that word, allowing for command arguments.
 *
 * @todo Implement backspacing. (Or is that going too far?)
 */


#include "commander.h"
#include "wordclock.h"
#include "wordclock-signals.h"
#include "serial.h"

#include <avr/pgmspace.h>


Q_DEFINE_THIS_FILE;


struct Commander commander;


static void add_to_buffer(struct Commander *me, char c);
static void process_buffer(struct Commander *me);
static void clear_buffer(struct Commander *me);


static QState commanderInitial(struct Commander *me);
static QState commanderState(struct Commander *me);

static void fn_TRON(const char *line);
static void fn_TROFF(const char *line);
static void fn_SET(const char *line);
static void fn_GET(const char *line);
static void fn_RESET(const char *line);

typedef void (*command_fn)(const char*);

static PROGMEM const char s_TROFF[] = "TROFF";
static PROGMEM const char s_TRON[] = "TRON";
static PROGMEM const char s_SET[] = "SET";
static PROGMEM const char s_GET[] = "GET";
static PROGMEM const char s_RESET[] = "RESET";


void commander_ctor(void)
{
	QActive_ctor((QActive*)(&commander), (QStateHandler)&commanderInitial);
}


static QState commanderInitial(struct Commander *me)
{
	return Q_TRAN(commanderState);
	for (uint8_t i=0; i<COMMANDER_BUFLEN; i++) {
		me->buf[i] = 0;
	}
	me->len - 0;
}


static QState commanderState(struct Commander *me)
{
	char c;

	switch (Q_SIG(me)) {

	case Q_ENTRY_SIG:
		S("commander!\r\n");
		return Q_HANDLED();

	case CHAR_SIGNAL:
		c = (char) Q_PAR(me);
		if ('\r' == c || '\n' == c || '\0' == c) {
			process_buffer(me);
		} else if ('\x1b' == c) {
			clear_buffer(me);
		} else {
			add_to_buffer(me, c);
		}
		return Q_HANDLED();
	}
	return Q_SUPER(&QHsm_top);
}


static void add_to_buffer(struct Commander *me, char c)
{
	Q_ASSERT( me->len < COMMANDER_BUFLEN );
	if (me->len >= COMMANDER_BUFLEN) {
		return;
	}
	me->buf[me->len++] = c;
	if (me->len == COMMANDER_BUFLEN-1) {
		process_buffer(me);
	}
}


static void process_buffer(struct Commander *me)
{
	if (! me->len) {
		return;
	}
	me->buf[me->len] = '\0';
	S("Processing: \"");
	serial_send(me->buf);
	S("\"\r\n");
	if (0) { }
#define C(name) else if (!strcasecmp_P(me->buf, s_##name)) do { fn_##name(me->buf); } while (0)
	C(TRON);
	C(TROFF);
	C(SET);
	C(GET);
	C(RESET);
	else { SD("unknown command\r\n"); }
	clear_buffer(me);
}


static void clear_buffer(struct Commander *me)
{
	me->buf[0] = '\0';
	me->len = 0;
}


static void fn_TRON(const char *line)
{
	S("Turning tracing on\r\n");
	traceon();
}


static void fn_TROFF(const char *line)
{
	S("Turning tracing off\r\n");
	traceoff();
}


static void fn_SET(const char *line)
{
	S("Set time...\r\n");
}


static void fn_GET(const char *line)
{
	S("Get time...\r\n");
}


static void fn_RESET(const char *line)
{
	SD("Reset via watchdog - turning off interrupts...\r\n");
	cli();
	while (1)
		;
}
