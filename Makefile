CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -pedantic
LDFLAGS := -lm -lSDL3
REL_FLAGS := -O2 -D DISABLE_DEBUG -D DISABLE_CPU_LOG
DBG_FLAGS := -ggdb -Og -D DISABLE_CPU_LOG
# For profiling
#DBG_FLAGS := -ggdb -Og -D DISABLE_DEBUG -D DISABLE_CPU_LOG
# For memory checks
#DBG_FLAGS := -ggdb -O2 -fsanitize=address -D DISABLE_DEBUG -D DISABLE_CPU_LOG

ARCH := $(shell uname -m)

BIN := nones
VERSION ?= 0.1
TARBALL_NAME := $(BIN)-$(VERSION)-linux-$(ARCH).tar.gz

# Posix compatiable version of $(wildcard)
SRCS := $(shell echo src/*.c)
OBJS := $(SRCS:src/%.c=%.o)

BUILD_DIR := build
REL_DIR := $(BUILD_DIR)/release
DBG_DIR := $(BUILD_DIR)/debug

DBG_OBJS := $(addprefix $(DBG_DIR)/, $(OBJS))
REL_OBJS := $(addprefix $(REL_DIR)/, $(OBJS))

REL_BIN := $(REL_DIR)/$(BIN)
DBG_BIN := $(DBG_DIR)/$(BIN)


.PHONY: all clean release debug run tarball

all: release

release: $(REL_BIN)
	@cp $< $(BIN)

$(REL_BIN): $(REL_OBJS)
	$(CC) $(REL_FLAGS) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(REL_DIR)/%.o: src/%.c
	@mkdir -p $(REL_DIR)
	$(CC) $(REL_FLAGS) $(CFLAGS) -c -o $@ $<

debug: $(DBG_BIN)
	@cp $< $(BIN)

$(DBG_BIN): $(DBG_OBJS)
	$(CC) $(DBG_FLAGS) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(DBG_DIR)/%.o: src/%.c
	@mkdir -p $(DBG_DIR)
	$(CC) $(DBG_FLAGS) $(CFLAGS) -c -o $@ $<

run:
	./$(BIN)

clean:
	@rm -rf $(BUILD_DIR)
	@if [ -f "$(BIN)" ]; then rm $(BIN); fi
	@if [ -f "$(TARBALL_NAME)" ]; then rm $(TARBALL_NAME); fi

tarball:
	@if [ ! -f "$(BIN)" ]; then \
		echo "Please run make before creating a tarball."; \
	else \
		echo "Creating tarball $(TARBALL_NAME)..."; \
		strip $(BIN); \
		tar -czf $(TARBALL_NAME) $(BIN); \
	fi
