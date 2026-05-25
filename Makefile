CC       = sdcc
DEVKITSMS = /home/claude/devkitSMS
SMSLIB   = $(DEVKITSMS)/SMSlib
IHX2SMS  = $(DEVKITSMS)/ihx2sms/Linux/ihx2sms
CRT0     = $(DEVKITSMS)/crt0/crt0_sms.rel

CFLAGS   = -mz80 --peep-file $(SMSLIB)/src/peep-rules.txt \
           -I$(SMSLIB)/src -I.
LDFLAGS  = -mz80 --no-std-crt0 --data-loc 0xC000

PROGNAME = jazzrunner

all: $(PROGNAME).sms

main.rel: main.c palettes.h tiles.h
	$(CC) $(CFLAGS) -c main.c

$(PROGNAME).ihx: main.rel
	$(CC) -o $@ $(LDFLAGS) $(CRT0) $(SMSLIB)/SMSlib.lib main.rel

$(PROGNAME).sms: $(PROGNAME).ihx
	$(IHX2SMS) $(PROGNAME).ihx $(PROGNAME).sms

clean:
	rm -f *.rel *.ihx *.asm *.sym *.lst *.noi *.lk *.map *.sms

.PHONY: all clean
.PHONY: $(PROGNAME).ihx
