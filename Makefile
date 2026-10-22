# Makefile - short commands for the usual tasks. CMakeLists.txt is the real build description;
# every target here runs CMake, so Make and CMake always build the same thing.
#
#   make            configure (Release) and build everything except the desktop app
#   make gui        build the desktop app ev33_gui as well (needs OpenGL; X11 headers on Linux)
#   make run        build and start the desktop app
#   make test       build and run the unit, integration and adversarial tests
#   make bench      build and run the benchmarks (writes benchmarks/results.csv)
#   make data       regenerate the benchmark datasets in data/generated (seed 12345)
#   make web        build and start the website server on http://localhost:8033
#   make clean      delete the build folder
#
# Variables: BUILD=folder (default build), CONFIG=Release|Debug, GENERATOR="Unix Makefiles" |
# "MinGW Makefiles" | Ninja (empty = CMake's default for this platform), JOBS=parallel jobs.
# Windows: run from an MSYS2 UCRT64 shell (make) or with mingw32-make in PowerShell.

BUILD     ?= build
CONFIG    ?= Release
GENERATOR ?=
JOBS      ?= 4
CMAKE     ?= cmake

ifeq ($(OS),Windows_NT)
  EXE := .exe
else
  EXE :=
endif

GEN_FLAG := $(if $(GENERATOR),-G "$(GENERATOR)",)

.PHONY: all configure gui run test bench data web clean help

all: configure
	$(CMAKE) --build $(BUILD) --config $(CONFIG) --parallel $(JOBS)

configure:
	$(CMAKE) -S . -B $(BUILD) $(GEN_FLAG) -DCMAKE_BUILD_TYPE=$(CONFIG)

gui:
	$(CMAKE) -S . -B $(BUILD) $(GEN_FLAG) -DCMAKE_BUILD_TYPE=$(CONFIG) -DEV33_GUI=ON
	$(CMAKE) --build $(BUILD) --config $(CONFIG) --parallel $(JOBS) --target ev33_gui

run: gui
	./$(BUILD)/ev33_gui$(EXE)

test: all
	ctest --test-dir $(BUILD) --output-on-failure -C $(CONFIG)

bench: all
	./$(BUILD)/benchmarks/ev33_bench$(EXE) --out benchmarks/results.csv

data: all
	./$(BUILD)/benchmarks/benchmark_data_generator$(EXE) --seed 12345 --size all --out data/generated

web: all
	./$(BUILD)/ev33_web$(EXE)

clean:
	$(CMAKE) -E rm -rf $(BUILD)

help:
	@echo "make | make gui | make run | make test | make bench | make data | make web | make clean"
	@echo "variables: BUILD=$(BUILD) CONFIG=$(CONFIG) GENERATOR=$(GENERATOR) JOBS=$(JOBS)"
