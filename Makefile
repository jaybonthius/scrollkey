CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic

ifeq ($(shell uname -s),Darwin)
ifeq ($(origin CC),default)
CC := $(shell xcrun --sdk macosx --find clang)
endif
SDKFLAGS := -isysroot "$(shell xcrun --sdk macosx --show-sdk-path)"
PLATFORM := src/macos.c
NATIVE_TEST := build/macos-test
NATIVE_SANITIZE_TEST := build/macos-test-sanitize
LDLIBS := -framework ApplicationServices -framework CoreFoundation
else
LDLIBS := -lm
endif

.PHONY: all test test-sanitize clean
ifeq ($(shell uname -s),Darwin)
all: build/scrollkey
else
all:
	@echo "Use build-windows.cmd on Windows; the native executable is macOS/Windows only."
	@exit 1
endif

build/scrollkey: src/main.c src/scroll.c $(PLATFORM) src/scroll.h src/platform.h | build
	$(CC) $(CFLAGS) $(SDKFLAGS) src/main.c src/scroll.c $(PLATFORM) $(LDLIBS) -o $@

build/scroll-test: tests/scroll.c src/scroll.c src/scroll.h | build
	$(CC) $(CFLAGS) $(SDKFLAGS) -Isrc tests/scroll.c src/scroll.c -lm -o $@

build/cli-test: src/main.c tests/platform_stub.c src/platform.h | build
	$(CC) $(CFLAGS) $(SDKFLAGS) -Isrc src/main.c tests/platform_stub.c -lm -o $@

build/scroll-test-sanitize: tests/scroll.c src/scroll.c src/scroll.h | build
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer $(SDKFLAGS) -Isrc tests/scroll.c src/scroll.c -lm -o $@

build/macos-test: tests/macos.c src/macos.c src/scroll.c src/scroll.h src/platform.h | build
	$(CC) $(CFLAGS) $(SDKFLAGS) -Isrc tests/macos.c src/scroll.c $(LDLIBS) -o $@

build/macos-test-sanitize: tests/macos.c src/macos.c src/scroll.c src/scroll.h src/platform.h | build
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer $(SDKFLAGS) -Isrc tests/macos.c src/scroll.c $(LDLIBS) -o $@

test: build/scroll-test build/cli-test $(NATIVE_TEST)
	./build/scroll-test
	sh tests/cli.sh ./build/cli-test
	$(if $(NATIVE_TEST),./$(NATIVE_TEST),true)

test-sanitize: build/scroll-test-sanitize $(NATIVE_SANITIZE_TEST)
	./build/scroll-test-sanitize
	$(if $(NATIVE_SANITIZE_TEST),./$(NATIVE_SANITIZE_TEST),true)

build:
	mkdir -p build

clean:
	rm -rf build
