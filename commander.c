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
#define C(name,len)							\
	else if ((!strncasecmp_P(me->buf, s_##name, len)) &&		\
		 ((me->buf[len] == '\0') || (me->buf[len] == ' ')))	\
		do { fn_##name(me->buf); } while (0)
	C(TRON,4);
	C(TROFF,5);
	C(SET,3);
	C(GET,3);
	C(RESET,5);
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


/**
 * Set the time by telling the Wordclock state machine to do so.
 *
 * The format of the time is very specific:
 *
 * "set hh:mm:ss A"
 *
 * "set" can be upper or lower case, the time must contain six digits and two
 * colons on that order, and "A" can be "P" for PM, and must be upper case.
 * Only one space between "set" and the time is allowed, and leading or
 * trailing space is not allowed.
 *
 * But it works.
 */
static void fn_SET(const char *line)
{
	static uint8_t bytes[3];

#define SP1 3
#define Hr1 4
#define Hr2 5
#define MiC 6
#define Mi1 7
#define Mi2 8
#define SeC 9
#define Se1 10
#define Se2 11
#define SP2 12
#define AoP 13
#define END 14

	if (
	    /* Check that the line matches the syntax */
	    line[SP1] != ' ' ||
	    line[Hr1]  < '0' || line[Hr1]  > '1' ||
	    line[Hr2]  < '0' || line[Hr2]  > '9' || line[MiC]  != ':' ||
	    line[Mi1]  < '0' || line[Mi1]  > '5' ||
	    line[Mi2]  < '0' || line[Mi2]  > '9' || line[SeC]  != ':' ||
	    line[Se1]  < '0' || line[Se1]  > '5' ||
	    line[Se2]  < '0' || line[Se2]  > '9' || line[SP2] != ' ' ||
	    (line[AoP] != 'A' && line[AoP] != 'P') ||
	    line[END] != '\0'
	    ||
	    /* Check for some extra validity in the hours */
	    (line[Hr1] == '0' && line[Hr2] == '0') ||
	    (line[Hr1] == '1' && line[Hr2]  > '2')
	    ) {
		S("time invalid\r\n");
		return;
	}

	S("Setting time to ");
	serial_send(line + 4);
	S("\r\n");

	/* seconds */
	bytes[0] = ((line[10] - '0') << 4) + (line[11] - '0');
	/* minutes */
	bytes[1] = ((line[7]  - '0') << 4) + (line[8]  - '0');
	/* hours, plus AM/PM flag, plus 12/24 hour flag */
	bytes[2] = (((line[4]  - '0') << 4) + (line[5]  - '0'))
		| (line[13]=='P' ? 0x20 : 0x00)
		| 0x40;

	S("bytes= ");
	serial_send_hex_int(bytes[0]);
	S(":");
	serial_send_hex_int(bytes[1]);
	S(":");
	serial_send_hex_int(bytes[2]);
	S("\r\n");
	fff(&wordclock);
	QActive_post((QActive*)(&wordclock), SET_TIME_SIGNAL, (QParam)bytes);
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
