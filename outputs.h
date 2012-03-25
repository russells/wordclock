#ifndef outputs_h_INCLUDED
#define outputs_h_INCLUDED

#include <stdint.h>

void outputs_init(void);
void outputs_off(void);
void output_on(uint8_t output);

#define ONE       1
#define TWO       2
#define THREE     3
#define FOUR      4
#define FIVE      5
#define SIX       6
#define SEVEN     7
#define EIGHT     8
#define NINE      9
#define TEN      10
#define ELEVEN   11
#define TWELVE   12
#define FIVE_MIN 13
#define TEN_MIN  14
#define QUARTER  15
#define TWENTY   16
#define HALF     17
#define PAST     18
#define TO       19
#define OCLOCK   20

#endif
