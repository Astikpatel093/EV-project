# EV33 - EV Charging Station Placement and Trip Planning System
# Engine and tools in portable C11; desktop app in C++17 (built through CMake). `make help` lists targets.

CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -pedantic
CPPFLAGS += -Iinclude
LDLIBS  += -lm

BUILD   := build
LIB_SRC := $(wildcard src/lib/*.c) $(wildcard src/domain/*.c)
LIB_OBJ := $(patsubst %.c,$(BUILD)/%.o,$(LIB_SRC))
LIB     := $(BUILD)/libev33.a

BINS    := $(BUILD)/ev33 $(BUILD)/ev33_bench $(BUILD)/ev33_gen $(BUILD)/ev33_tests
PROFILES := small medium shock large adversarial

.PHONY: all clean test bench bench-quick data demo sanitize help gui run-gui web serve release


all: $(BINS)

$(BUILD)/%.o: %.c $(wildcard include/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB): $(LIB_OBJ)
	ar rcs $@ $^

$(BUILD)/ev33: $(BUILD)/src/app/main.o $(LIB)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)
$(BUILD)/ev33_bench: $(BUILD)/src/app/bench.o $(LIB)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)
$(BUILD)/ev33_gen: $(BUILD)/tools/ev33_gen.o $(LIB)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)
$(BUILD)/ev33_tests: $(BUILD)/tests/test_main.o $(LIB)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# ---- desktop app (C++17, Dear ImGui + ImPlot on GLFW/OpenGL, vendored in third_party/)
GUI_BUILD := $(BUILD)/gui
gui:
	cmake -S . -B $(GUI_BUILD) -DCMAKE_BUILD_TYPE=Release $(CMAKE_FLAGS)
	cmake --build $(GUI_BUILD) --target ev33_gui -j

run-gui: gui
	./$(GUI_BUILD)/ev33_gui

# ---- website server (C++17, pages generated as HTML + SVG; deployed on Render via the Dockerfile)
WEB_BUILD := $(BUILD)/web
web:
	cmake -S . -B $(WEB_BUILD) -DCMAKE_BUILD_TYPE=Release -DEV33_GUI=OFF
	cmake --build $(WEB_BUILD) --target ev33_web -j

serve: web
	./$(WEB_BUILD)/ev33_web

test: $(BUILD)/ev33_tests
	./$(BUILD)/ev33_tests

bench: $(BUILD)/ev33_bench
	./$(BUILD)/ev33_bench --out results/bench.csv

bench-quick: $(BUILD)/ev33_bench
	./$(BUILD)/ev33_bench --quick --out results/bench_quick.csv

data: $(BUILD)/ev33_gen
	@for p in $(PROFILES); do ./$(BUILD)/ev33_gen --profile $$p --seed 33 --out data/$$p; done

demo: $(BUILD)/ev33
	./$(BUILD)/ev33 demo --data data --out results/demo

# AddressSanitizer + UBSan build of the test suite
sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS="-std=c11 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer" test

# First commit and release tag, using YOUR git identity (no other attribution is added):
#   make release            -> tags v1.0.0
#   make release TAG=v1.0.1
TAG ?= v1.0.0
release:
	@git config user.name >/dev/null || { echo "Set your identity first: git config --global user.name 'Your Name'"; exit 1; }
	$(MAKE) clean
	$(MAKE) all test
	@[ -d .git ] || git init -q
	git add -A
	git commit -q -m "EV33 $(TAG): EV charging station placement and trip planning system" || echo "(nothing new to commit)"
	git tag -a "$(TAG)" -m "EV33 release $(TAG)"
	@echo "Committed and tagged $(TAG). Push with: git remote add origin <url> && git push -u origin HEAD --tags"

clean:
	rm -rf $(BUILD)

help:
	@echo "make            build ev33, ev33_bench, ev33_gen, ev33_tests into build/"
	@echo "make test       run the unit + integration + regression suite"
	@echo "make bench      full benchmark -> results/bench.csv"
	@echo "make data       regenerate data/{small,medium,shock,large,adversarial}"
	@echo "make demo       run the three demonstration scenarios"
	@echo "make sanitize   tests under ASan/UBSan"
	@echo "make gui        build the desktop app build/gui/ev33_gui (CMake + C++17 + OpenGL)"
	@echo "make run-gui    build and start the desktop app"
	@echo "make web        build the website server build/web/ev33_web; make serve runs it on :8033"
	@echo "make release    first commit + tag with your own git identity (TAG=v1.0.0)"
