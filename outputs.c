#include "outputs.h"
#include "serial.h"


/**
 * Initialise the outputs that drive the display lights.
 *
 * @todo Implement the output lines.
 */
void outputs_init(void)
{

}


void outputs_off(void)
{
	//S("All outputs off\r\n");
}


void output_on(uint8_t output)
{
	switch (output) {
#define C(x) case x: S(#x); break
	C(ONE);
	C(TWO);
	C(THREE);
	C(FOUR);
	C(FIVE);
	C(SIX);
	C(SEVEN);
	C(EIGHT);
	C(NINE);
	C(TEN);
	C(ELEVEN);
	C(TWELVE);
	C(FIVE_MIN);
	C(TEN_MIN);
	C(QUARTER);
	C(TWENTY);
	C(HALF);
	C(PAST);
	C(TO);
	C(OCLOCK);
	default:
		S("<unknown output ");
		serial_send_int(output);
		S(">");
		break;
	}

}


