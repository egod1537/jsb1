CMAKE ?= cmake
BASH ?= /usr/bin/bash
BUILD_DIR ?= build
CMAKE_CONFIGURE_ARGS ?= -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

.PHONY: configure build build-console test fg run clean \
	px4-setup px4-build px4-run px4-status

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -G Ninja $(CMAKE_CONFIGURE_ARGS)

build: configure
	$(CMAKE) --build $(BUILD_DIR)

build-console: configure
	$(CMAKE) --build $(BUILD_DIR) --target jsb-flight-console

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

fg:
	$(BASH) ./scripts/run-flightgear.sh

run:
	$(BASH) ./scripts/run-console.sh

px4-setup:
	$(BASH) ./scripts/px4-wsl.sh setup

px4-build:
	$(BASH) ./scripts/px4-wsl.sh build

px4-run:
	$(BASH) ./scripts/px4-wsl.sh run

px4-status:
	$(BASH) ./scripts/px4-wsl.sh status

clean:
	rm -rf $(BUILD_DIR)
