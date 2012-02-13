#include "bsp.h"
#include "wordclock.h"
#include "serial.h"



void QF_onStartup(void)
{

}

void QF_onIdle(void)
{

}

void Q_onAssert(char const Q_ROM * const Q_ROM_VAR file, int line)
{
	serial_assert(file, line);
}


void BSP_watchdog(struct Wordclock *me)
{

}


void BSP_startmain(void)
{

}


void BSP_init(void)
{

}

