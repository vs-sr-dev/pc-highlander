# pc-highlander - the engine.  Tools are Python and are not built here.
#
#   make            build/hlview
#   make run        a backdrop with the wine bottle standing in it
#
# SDL3 comes from pkg-config; on Windows that means an MSYS2 mingw64 shell.

# MSYS2's make drops TMP and TEMP from recipe environments, and gcc then tries
# to write its temporaries into C:\WINDOWS.  Give it somewhere it can write.
TMPDIR := $(shell cygpath -m "$(CURDIR)" 2>/dev/null || echo "$(CURDIR)")/build/tmp
TMP    := $(TMPDIR)
TEMP   := $(TMPDIR)
export TMPDIR TMP TEMP
$(shell mkdir -p $(TMPDIR))

CC       = gcc
CFLAGS  ?= -O2 -std=c99 -Wall -Wextra -Wno-unused-parameter
CFLAGS  += $(shell pkg-config --cflags sdl3)
# Track header dependencies: without this a change to, say, model.h relinks
# objects compiled against the old struct, which fails in ways that look like
# a bug in the renderer.
CFLAGS  += -MMD -MP
# -mconsole must come after SDL3, which asks for -mwindows: the viewer
# prints what it loaded, so it wants a console.
LDLIBS  += $(shell pkg-config --libs sdl3) -lm -mconsole

SRC := src/main.c src/util/io.c src/util/json.c src/game/scene.c \
       src/game/model.c src/game/anim.c src/game/actor.c src/game/set.c \
       src/game/control.c src/game/ai.c src/game/sheet.c \
       src/r3d/r3d.c src/platform/window.c
OBJ := $(SRC:src/%.c=build/%.o)
BIN := build/hlview

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

-include $(OBJ:.o=.d)

run: $(BIN)
	./$(BIN) --scene CA_CAM06 --model boot:6 --object CA_WINE

clean:
	rm -rf build

.PHONY: all run clean
