CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -I.

.PHONY: notifyd clean
notifyd: consoleos-notifyd

consoleos-notifyd: notifyd.c ipc.h
	$(CC) $(CFLAGS) -o consoleos-notifyd notifyd.c

clean:
	rm -f consoleos-notifyd
