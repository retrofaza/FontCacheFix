# Cross-compile fcachefix for AROS.
#
# Example:
#   make CC=x86_64-aros-gcc
#   make CC=i386-aros-gcc

CC      ?= x86_64-aros-gcc
CFLAGS  ?= -O2

fcachefix: fcachefix.c
	$(CC) $(CFLAGS) -o $@ fcachefix.c

clean:
	rm -f fcachefix

install:
	cp fcachefix ../../Dist/Scripts/fcachefix
	cp FontCacheFix ../../Dist/Scripts/FontCacheFix

.PHONY: clean install