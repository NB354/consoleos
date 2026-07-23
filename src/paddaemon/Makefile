CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -I. $(shell pkg-config --cflags libevdev 2>/dev/null)
LDLIBS ?= $(shell pkg-config --libs libevdev 2>/dev/null || echo -levdev)

.PHONY: all clean
all: consoleos-padd

consoleos-padd: padd.c ../common/ipc.h
	$(CC) $(CFLAGS) -o consoleos-padd padd.c $(LDLIBS)

clean:
	rm -f consoleos-padd
