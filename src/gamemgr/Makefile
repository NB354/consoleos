CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -I.
LDLIBS ?= -lsqlite3 -larchive -lcrypto

.PHONY: all clean
all: consoleos-gamemgrd

consoleos-gamemgrd: gamemgrd.c ipc.h zpk_format.h
	$(CC) $(CFLAGS) -o consoleos-gamemgrd gamemgrd.c $(LDLIBS)

clean:
	rm -f consoleos-gamemgrd
