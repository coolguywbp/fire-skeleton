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

CFLAGS_DEBUG	= -O0 -g3 -ggdb3 -fno-strict-aliasing -fstack-protector-strong \
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
    CFLAGS_BASE += $(shell pkg-config --cflags sdl3 sdl3-image sdl3-ttf) #sdl3-mixer)
    LDLIBS_BASE += $(shell pkg-config --libs sdl3 sdl3-image sdl3-ttf) #sdl3-mixer)
else
    $(error "pkg-config is not available. Please install pkg-config.")
endif

CFLAGS		= $(CFLAGS_BASE)
LDLIBS		= $(LDLIBS_BASE)

$(BUILD_DIR):
	$(MKDIR)

all:	$(TARGET)

$(BUILD_DIR)/%.o : $(SRC_DIR)/%.c | $(BUILD_DIR)
	@echo "------ Make $(@) ------"
	rm -f $@
	@$(MKDIR) $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): $(OBJS)
	$(CC) $(LDLIBS) $^ -o $@

clean:
	$(CLEAN)

run: $(TARGET)
	./$<

rebuild: clean all
