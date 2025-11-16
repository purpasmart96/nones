CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -pedantic
LDFLAGS := -lm -lSDL3 -lsoxr
REL_FLAGS := -O3 -flto=auto -D DISABLE_DEBUG -D DISABLE_CPU_LOG
DBG_FLAGS := -ggdb -Og -D DISABLE_CPU_LOG
# For profiling
#DBG_FLAGS := -ggdb -Og -D DISABLE_DEBUG -D DISABLE_CPU_LOG
# For memory checks
#DBG_FLAGS := -ggdb -O2 -fsanitize=address -D DISABLE_DEBUG -D DISABLE_CPU_LOG

ifeq ($(OS), Windows_NT)
	ifneq ($(MSYSTEM), UCRT64)
	$(error MSYS2-UCRT64 environment not detected!)
	endif
	OS_NAME := windows
	ARCHIVE_FMT := .zip
endif

OS_NAME ?= $(shell uname -s | tr '[:upper:]' '[:lower:]')
ARCH := $(shell uname -m)

BIN := nones
VERSION := 0.3.1
ARCHIVE_FMT ?= .tar.gz
ARCHIVE := $(BIN)-$(VERSION)-$(OS_NAME)-$(ARCH)$(ARCHIVE_FMT)

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


.PHONY: all clean release debug run tarball win_zip

all: release

release: $(REL_BIN)
ifeq ($(OS_NAME), windows)
	cp /ucrt64/bin/SDL3.dll .
	cp /ucrt64/bin/libsoxr.dll .
#libsoxr uses libgomp... Yuck. Guess we gotta add the rest the required dll's
	cp /ucrt64/bin/libgomp-1.dll .
	cp /ucrt64/bin/libwinpthread-1.dll .
	cp /ucrt64/bin/libgcc_s_seh-1.dll .
endif
	@cp $< $(BIN)

$(REL_BIN): $(REL_OBJS)
	$(CC) $(REL_FLAGS) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(REL_DIR)/%.o: src/%.c
	@mkdir -p $(REL_DIR)
	$(CC) $(REL_FLAGS) $(CFLAGS) -c -o $@ $<

debug: $(DBG_BIN)
ifeq ($(OS_NAME), windows)
	cp /ucrt64/bin/SDL3.dll .
	cp /ucrt64/bin/libsoxr.dll .
#libsoxr uses libgomp... Yuck. Guess we gotta add the rest the required dll's
	cp /ucrt64/bin/libgomp-1.dll .
	cp /ucrt64/bin/libwinpthread-1.dll .
	cp /ucrt64/bin/libgcc_s_seh-1.dll .
endif
	@cp $< $(BIN)

$(DBG_BIN): $(DBG_OBJS)
	$(CC) $(DBG_FLAGS) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(DBG_DIR)/%.o: src/%.c
	@mkdir -p $(DBG_DIR)
	$(CC) $(DBG_FLAGS) $(CFLAGS) -c -o $@ $<

run:
	./$(BIN)

clean:
	@if [ -d "$(BUILD_DIR)" ]; then rm -r $(BUILD_DIR); else echo 'Nothing to clean up'; fi
	@if [ -f "$(BIN)" ]; then rm $(BIN); fi
	@if [ -f "$(ARCHIVE)" ]; then rm $(ARCHIVE); fi
	@if [ -f "SDL3.dll" ]; then rm "SDL3.dll"; fi
	@if [ -f "libsoxr.dll" ]; then rm "libsoxr.dll"; fi
	@if [ -f "libgomp-1.dll" ]; then rm "libgomp-1.dll"; fi
	@if [ -f "libwinpthread-1.dll" ]; then rm "libwinpthread-1.dll"; fi
	@if [ -f "libgcc_s_seh-1.dll" ]; then rm "libgcc_s_seh-1.dll"; fi

tarball:
	@if [ -f "$(BIN)" ]; then \
		strip $(BIN); \
		tar -czf $(ARCHIVE) $(BIN) "LICENSE" "README.md"; \
		echo "Created tarball $(ARCHIVE)..."; \
	else \
		echo "Please run 'make' before creating a tarball."; \
	fi

win_zip:
	@if [ -f "$(BIN)" ]; then \
		strip $(BIN).exe; \
		7z a $(ARCHIVE) $(BIN).exe "SDL3.dll" "libsoxr.dll" "libgomp-1.dll" "libwinpthread-1.dll" "libgcc_s_seh-1.dll" "LICENSE" "README.md"; \
		echo "Created zip $(ARCHIVE)..."; \
	else \
		echo "Please run 'make' before creating a zip."; \
	fi
