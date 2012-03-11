#ifndef qactive_named_h_INCLUDED
#define qactive_named_h_INCLUDED

#include "qpn_port.h"


/**
 * Our own version of a QActive, containing a name.
 *
 * The name can be used in debugging and exception messages.  A cast from
 * QActive* to QActiveNamed* will probably be required to access the name
 * member.
 */
typedef struct QActiveNamed_ {
	QActive super;
	/** Pointer to a human readable name for this active object in flash
	    memory.  Because the active objects are in RAM, the name will have
	    to be set programatically. */
	const char Q_ROM * Q_ROM_VAR name;
} QActiveNamed;


#endif
