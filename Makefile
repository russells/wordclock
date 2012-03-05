

RM = rm -f
RM_RF = rm -rf

DEPDEPS = Makefile

%.d: %.c $(DEPDEPS)
	@echo DEP: $<
	@rm -f $@ $(@:.d=.u)
	@$(CC) -E -M $(CFLAGS) $< > /dev/null


CC     = avr-gcc
LINK   = avr-gcc
OBJCOPY = avr-objcopy
APPNAME = wordclock
PROGRAM = $(APPNAME).elf
PROGRAMMAPFILE = $(APPNAME).map
HEXPROGRAM = $(APPNAME).hex
BINPROGRAM = $(APPNAME).bin

AVR_PROGRAMMER ?= stk500v2
AVR_PROGRAMMER_PORT = /dev/ttyACM0

QPN_INCDIR = qp-nano/include
QP_LIBDIR = $(QP_PRTDIR)/$(BINDIR)
QP_SRCDIR = qp-nano/source
QP_LIBS   =
EXTRA_LIBS =
EXTRA_LINK_FLAGS = -Wl,-Map,$(PROGRAMMAPFILE),--cref
TARGET_MCU = atmega32
CFLAGS  = -c -gdwarf-2 -std=gnu99 -Os -fsigned-char -fshort-enums \
	-Wno-attributes \
	-mmcu=$(TARGET_MCU) -Wall -Werror -o$@ \
	-I$(QPN_INCDIR) -I.
LINKFLAGS = -gdwarf-2 -Os -mmcu=$(TARGET_MCU)

SRCS = wordclock.c bsp-avr.c qepn.c qfn.c serial.c

OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

default: $(HEXPROGRAM)

.PHONY: bin
bin: $(BINPROGRAM)
$(BINPROGRAM): $(PROGRAM)
	$(OBJCOPY) -O binary $< $@

$(HEXPROGRAM): $(PROGRAM)
	$(OBJCOPY) -j .text -j .data -O ihex $< $@

$(PROGRAM): $(OBJS) $(QP_LIBS)
	$(LINK) $(LINKFLAGS) -o $(PROGRAM) $(EXTRA_LINK_FLAGS) \
	$(OBJS) $(QP_LIBS) $(EXTRA_LIBS)


ifneq ($(MAKECMDGOALS),clean)
-include $(DEPS)
endif


.PHONY: tags
tags:
	etags *.[ch]
	ctags *.[ch]


# clean targets...

.PHONY: clean realclean

clean:
	-$(RM_RF) $(OBJS) $(PROGRAM) $(HEXPROGRAM) $(PROGRAMMAPFILE) $(BINPROGRAM) $(DEPS)

realclean: clean
	-$(RM_RF) doc *.d *.o *.elf *.hex *.map *.bin

.PHONY: flash
flash: $(HEXPROGRAM)
	avrdude -P $(AVR_PROGRAMMER_PORT) -p m32 -c $(AVR_PROGRAMMER) -U flash:w:$(HEXPROGRAM)

.PHONY: doc
doc:
	@mkdir -p doc
	doxygen Doxyfile
