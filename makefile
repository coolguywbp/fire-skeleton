TARGET = game
BUILD_DIR		= .build
SRC_DIR = src
CC = gcc

CFLAGS_BASE		= -std=c11

CFLAGS_STRICT	= -Wstrict-aliasing=2 -Wall -Wextra -Werror -Wpedantic \
				  -Wwrite-strings -Wconversion -Wmissing-declarations \
				  -Wmissing-include-dirs -Wfloat-equal -Wsign-compare -Wundef \
				  -Wcast-align -Wswitch-default -Wimplicit-fallthrough \
				  -Wempty-body -Wuninitialized -Wmisleading-indentation \
				  -Wshadow -Wmissing-prototypes -Wstrict-prototypes \
				  -Wold-style-definition

CFLAGS_RELEASE	= -O3 -march=native -flto=auto -fno-plt -fomit-frame-pointer

CFLAGS_DEBUG	= -O0 -g -ggdb3 -fno-strict-aliasing -fstack-protector-strong \
				  -DDEBUG -fno-omit-frame-pointer

LDLIBS_BASE		= -lm

LDLIBS_RELEASE	= -flto

LDLIBS_DEBUG	=

SRCS			= $(shell find $(SRC_DIR) -name '*.c')
OBJS			= $(subst $(SRC_DIR), $(BUILD_DIR), $(SRCS:.c=.o))
DEPS			= $(OBJS:.o=.d)

CFLAGS_DEBUG	+= -fsanitize=address -fsanitize-address-use-after-scope -ftrapv
LDLIBS_DEBUG	+= -fsanitize=address -fsanitize-address-use-after-scope
PKG_CONFIG	:= $(shell command -v pkg-config >/dev/null 2>&1 && echo "yes" || echo "no")
CLEAN		= $(RM) $(TARGET) && $(RM) -r $(BUILD_DIR)
MKDIR		= mkdir -p $(BUILD_DIR)
CFLAGS = 
LDLIBS = 

ifeq ($(PKG_CONFIG),yes)
    CFLAGS_BASE += $(shell pkg-config --cflags sdl3 sdl3-image sdl3-ttf zlib lua5.4) #sdl3-mixer)
    LDLIBS_BASE += $(shell pkg-config --libs sdl3 sdl3-image sdl3-ttf zlib lua5.4) #sdl3-mixer)
else
    $(error "pkg-config is not available. Please install pkg-config.")
endif

CFLAGS		= $(CFLAGS_BASE)
LDLIBS		= $(LDLIBS_BASE)

.PHONY: all debug clean run rebuild web web-clean

$(BUILD_DIR):
	$(MKDIR)

all:	$(TARGET)

debug: CFLAGS += $(CFLAGS_DEBUG) #$(CFLAGS_STRICT)
debug: LDLIBS += $(LDLIBS_DEBUG)
debug: $(TARGET)

$(BUILD_DIR)/%.o : $(SRC_DIR)/%.c | $(BUILD_DIR)
	@echo "------ Make $(@) ------"
	rm -f $@
	@$(MKDIR) $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(TARGET): $(OBJS)
	$(CC) $(LDLIBS) $^ -o $@

clean:
	$(CLEAN)

# Pull in auto-generated header dependencies so editing a .h rebuilds the .c
# files that include it (prevents stale objects with mismatched struct layouts).
-include $(DEPS)

run: $(TARGET)
	./$<

rebuild: clean all

# ----------------------------------------------------------------------------
# WebAssembly build (Emscripten)  --  EXPERIMENTAL / WORK IN PROGRESS
#
# This target compiles and links, but the web build is NOT yet verified to run
# correctly in a browser; more work is needed (renderer/canvas, input, asset
# handling, benchmark assumptions). Treat it as a starting point, not done.
#
# Requires the Emscripten SDK (emcc) and an Emscripten-built SDL3 stack
# (SDL3 + SDL3_image + SDL3_ttf, with their image/font dependencies). Point
# SDL3_WASM at that install (headers in $(SDL3_WASM)/include, libs in
# $(SDL3_WASM)/lib), e.g.:
#
#   make web SDL3_WASM=/path/to/sdl3-emscripten
#
# Output goes to web/game.{html,js,wasm,data}; serve it over HTTP and open
# game.html. The web build is single-threaded (see ECS_CustomNew).
# ----------------------------------------------------------------------------
EMCC		?= emcc
# Prebuilt Emscripten SDL3 stack, vendored in the repo by default. Override on
# the command line (e.g. SDL3_WASM=/elsewhere) to use an install outside it.
SDL3_WASM	?= $(CURDIR)/vendor/sdl3-wasm
# Lua 5.4 built for wasm (the native build links the system lua5.4; Emscripten
# has no such lib, so a static liblua.a is vendored — see vendor/lua-wasm).
LUA_WASM	?= $(CURDIR)/vendor/lua-wasm
WASM_OUT	?= web/game.html

# gnu11 (not strict c11) so the Emscripten/musl headers expose POSIX functions
# like clock_gettime, matching how the code builds against glibc natively.
WASM_CFLAGS	= -std=gnu11 -O3 -I$(SDL3_WASM)/include -I$(LUA_WASM)/include
# Link order matters for static libs: SDL3_image -> libpng -> zlib (provided by
# the Emscripten zlib port). freetype/harfbuzz are bundled inside libSDL3_ttf.a.
# Lua is self-contained. assets/ and scripts/ are baked into the virtual FS so
# the Lua scenes (loaded at runtime) and images/fonts are available in browser.
WASM_LDFLAGS	= -L$(SDL3_WASM)/lib -lSDL3_image -lSDL3_ttf -lSDL3 -lpng16 \
		  -L$(LUA_WASM)/lib -llua \
		  -sUSE_ZLIB=1 -sALLOW_MEMORY_GROWTH=1 \
		  --shell-file web/shell.html \
		  --preload-file assets --preload-file scripts

web:
	@mkdir -p $(dir $(WASM_OUT))
	$(EMCC) $(WASM_CFLAGS) $(SRCS) $(WASM_LDFLAGS) -o $(WASM_OUT)

web-clean:
	$(RM) web/game.html web/game.js web/game.wasm web/game.data
