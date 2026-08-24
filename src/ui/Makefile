CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -I. $(shell pkg-config --cflags sdl2 SDL2_image SDL2_ttf SDL2_mixer 2>/dev/null)
LDLIBS ?= $(shell pkg-config --libs sdl2 SDL2_image SDL2_ttf SDL2_mixer 2>/dev/null || echo -lSDL2 -lSDL2_image -lSDL2_ttf -lSDL2_mixer) -lsqlite3

.PHONY: all clean
all: consoleos-ui

consoleos-ui: main.c theme.c theme.h ipc.h
	$(CC) $(CFLAGS) -o consoleos-ui main.c theme.c $(LDLIBS)

clean:
	rm -f consoleos-ui
