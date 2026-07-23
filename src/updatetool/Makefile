CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -I.
LDLIBS ?= -lsodium

.PHONY: all clean
all: consoleos-updated

consoleos-updated: updated.c ../common/ipc.h
	$(CC) $(CFLAGS) -o consoleos-updated updated.c $(LDLIBS)

clean:
	rm -f consoleos-updated
