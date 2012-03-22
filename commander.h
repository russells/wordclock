#ifndef commander_h_INCLUDED
#define commander_h_INCLUDED

#include "qpn_port.h"
#include "qactive-named.h"


#define COMMANDER_BUFLEN 50


struct Commander {
	QActiveNamed super;
	char buf[COMMANDER_BUFLEN];
	uint8_t len;
};


extern struct Commander commander;


void commander_ctor(void);

#endif
