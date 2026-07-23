CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra

.PHONY: all clean
all: consoleos-sandbox

consoleos-sandbox: sandbox.c
	$(CC) $(CFLAGS) -o consoleos-sandbox sandbox.c

clean:
	rm -f consoleos-sandbox
