# Knobby Screenshot Simulator
# Requires: gcc, LVGL installed via arduino-cli (see CONTRIBUTING.md)

# Include configuration for paths
-include ../config.mk

# Default values if not passed from config.sh
LLVM_BIN  ?= 
LVGL_PATH ?= $(HOME)/Arduino/libraries/lvgl
PYTHON    ?= python3

KNOBBY    := ../knobby
BUILD     := build
BIN       := knobby_sim

ifeq ($(OS),Windows_NT)
    SDL2_ROOT   := SDL2/x86_64-w64-mingw32
    BIN_GUI     := knobby_sim_gui.exe
    CC          ?= $(LLVM_BIN)/clang.exe
    SDL_CFLAGS  := -I$(SDL2_ROOT)/include
    SDL_LDFLAGS := -L$(SDL2_ROOT)/lib -lSDL2main -lSDL2
else
    BIN_GUI     := knobby_sim_gui
    CC          ?= gcc
    SDL_CFLAGS  := $(shell pkg-config --cflags sdl2 2>/dev/null)
    SDL_LDFLAGS := $(shell pkg-config --libs sdl2 2>/dev/null || echo "-lSDL2")
endif

CFLAGS    := -Wall -Wno-unused-function \
             -DLV_CONF_INCLUDE_SIMPLE -DSIMULATOR=1 \
             -I. -Iesp_stubs -I$(LVGL_PATH) -I$(LVGL_PATH)/src -I$(KNOBBY) -I$(KNOBBY)/src \
             -O2 -g -MMD -MP

LDFLAGS   := -lm

# SDL2 flags are only used for the GUI build, not the headless screenshot simulator
GUI_CFLAGS  := $(CFLAGS) $(SDL_CFLAGS)
GUI_LDFLAGS := $(SDL_LDFLAGS) $(LDFLAGS)

# ---- Source files ----

# Top-level knobby files
KNOBBY_TOP_SRCS := $(KNOBBY)/knob.c $(KNOBBY)/board_pins.c

# Source files in src/
KNOBBY_SRC_SRCS := $(addprefix $(KNOBBY)/src/, \
	ui_1p.c ui_mp.c mp_layout.c mp_victory.c mp_attack_gesture.c ui_player_menu.c settings.c \
	intro.c assets_intro.c game.c game_state.c game_hooks.c damage_log.c attack.c \
	game_mode.c rename.c dice.c hw.c storage.c mana.c lang.c wifi_ota.c ui_wifi.c custom_keyboard.c round_safe.c minigame.c snake.c pong.c dino.c tetris.c breakout.c flappy.c eggs.c invaders.c rps.c asteroids.c tempest.c)

# Sim files
SIM_SRCS := sim_main.c sim_stubs.c

# LVGL sources (recursive wildcard)
rwildcard = $(wildcard $1$2) $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2))
LVGL_SRCS := $(call rwildcard,$(LVGL_PATH)/src/,*.c)

FONT_SRCS := $(wildcard $(KNOBBY)/src/fonts/*.c)

ALL_SRCS := $(SIM_SRCS) $(KNOBBY_TOP_SRCS) $(KNOBBY_SRC_SRCS) $(FONT_SRCS) $(LVGL_SRCS)
ALL_SRCS_GUI := sim_sdl2.c sim_stubs.c $(KNOBBY_TOP_SRCS) $(KNOBBY_SRC_SRCS) $(FONT_SRCS) $(LVGL_SRCS)

# Object files — flatten into a given build dir using path-mangled names.
# Takes the build dir as $1 so the memory-budget build below (which needs
# its own -m32 + different lv_conf.h object tree, see test-mem-budget) can
# reuse it instead of duplicating the mangling scheme.
obj_name = $(1)/$(subst :,_COL_,$(subst /,__,$(subst ..,UP,$(2:.c=.o))))
ALL_OBJS := $(foreach src,$(ALL_SRCS),$(call obj_name,$(BUILD),$(src)))
ALL_OBJS_GUI := $(foreach src,$(ALL_SRCS_GUI),$(call obj_name,$(BUILD),$(src)))

# Everything except sim_main.o's main() — what the unit test binaries
# under tests/ (each with its own main()) link against instead.
ALL_SRCS_NOMAIN := $(filter-out sim_main.c,$(ALL_SRCS))
ALL_OBJS_NOMAIN := $(foreach src,$(ALL_SRCS_NOMAIN),$(call obj_name,$(BUILD),$(src)))

# ---- Targets ----

.PHONY: screenshot generate-matrix clean gui web sim-gui test test-mem-budget test-all

$(BIN): $(ALL_OBJS)
	$(CC) $(ALL_OBJS) -o $@ $(LDFLAGS)

sim-gui: $(BIN_GUI)
	./$(BIN_GUI)

gui: $(BIN_GUI)
	./$(BIN_GUI)

$(BIN_GUI): $(ALL_OBJS_GUI)
	$(CC) $(ALL_OBJS_GUI) -o $@ $(GUI_LDFLAGS)

web:
	$(PYTHON) gen_web_sources.py $(LVGL_PATH) $(KNOBBY)
	$(EMCC) $(CFLAGS) -o knobby_web.js @web_sources.txt \
		-s USE_SDL=2 -s WASM=1 \
		-s INITIAL_MEMORY=33554432 \
		-s EXPORTED_RUNTIME_METHODS='["ccall", "cwrap"]'

screenshot: $(BIN)
	@mkdir -p screenshots
	./$(BIN) $(ARGS)

generate-matrix: $(BIN)
	./generate_matrix.sh

# ---- Unit tests (headless, no hardware) ----
# Each file under tests/ is a standalone binary with its own main() (see
# sim/tests/test_harness.h) - simpler than one combined test runner, and a
# crash in one test can't take the others down with it. Linked straight
# against ALL_OBJS_NOMAIN rather than going through per-test .o + link
# rules: there are only a handful of these, and always-recompile-the-test-
# file keeps this section short at the cost of a couple seconds per run.
TEST_SRCS  := $(wildcard tests/*.c)
TEST_BUILD := $(BUILD)/tests

test: $(ALL_OBJS_NOMAIN)
	@mkdir -p $(TEST_BUILD)
	@failed=0; \
	for src in $(TEST_SRCS); do \
		name=$$(basename $$src .c); \
		if ! $(CC) $(CFLAGS) -Itests $$src $(ALL_OBJS_NOMAIN) -o $(TEST_BUILD)/$$name $(LDFLAGS) 2>&1; then \
			echo "FAIL: $$name did not compile"; failed=1; continue; \
		fi; \
		echo "---- $$name ----"; \
		if ! ./$(TEST_BUILD)/$$name; then \
			echo "FAIL: $$name"; failed=1; \
		fi; \
		echo; \
	done; \
	if [ $$failed -ne 0 ]; then echo "One or more tests FAILED"; exit 1; fi; \
	echo "All test binaries passed."

# ---- LVGL memory budget test (separate build: real 128KB pool, -m32) ----
# Everything else under tests/ links against the same objects `make sim`
# produces (sim/lv_conf.h bumps LV_MEM_SIZE to 512KB so desktop testing
# isn't fighting the device's pool too - see that file). The memory
# budget test's entire point is measuring against the device's real
# 128KB ceiling, so it needs its own object tree built against
# tests/lv_conf_membudget/lv_conf.h instead, and -m32 so pointer/struct
# sizes match Xtensa rather than a 64-bit desktop overestimating usage.
# Requires a 32-bit gcc multilib (gcc-multilib on Debian/Ubuntu).
BUILD_MB  := $(BUILD)/membudget
CFLAGS_MB := -m32 -Wall -Wno-unused-function \
             -DLV_CONF_INCLUDE_SIMPLE -DSIMULATOR=1 \
             -Itests/lv_conf_membudget -I. -Itests -Iesp_stubs \
             -I$(LVGL_PATH) -I$(LVGL_PATH)/src -I$(KNOBBY) -I$(KNOBBY)/src \
             -O2 -g -MMD -MP
ALL_OBJS_MB := $(foreach src,$(ALL_SRCS_NOMAIN),$(call obj_name,$(BUILD_MB),$(src)))

test-mem-budget: $(ALL_OBJS_MB)
	@mkdir -p $(BUILD_MB)
	$(CC) $(CFLAGS_MB) tests/membudget/test_lvgl_memory_budget.c $(ALL_OBJS_MB) \
		-o $(BUILD_MB)/test_lvgl_memory_budget -lm
	./$(BUILD_MB)/test_lvgl_memory_budget

# ---- game_state.h purity check (deliberately zero LVGL) ----
# Proves game_state.h - and everything it includes (game_types.h,
# game_hooks.h, net_sync.h) - has no LVGL dependency: no -I to
# $(LVGL_PATH), no -DLV_CONF_INCLUDE_SIMPLE, nothing. If a future change
# reintroduces an lv_ type into that header chain, this is what catches
# it (as a compile error here, not a slow architectural drift nobody
# notices). See tests/purity/test_game_state_header_purity.c.
test-game-state-purity:
	@mkdir -p $(BUILD)
	$(CC) -Wall -I$(KNOBBY)/src -O2 \
		tests/purity/test_game_state_header_purity.c \
		-o $(BUILD)/test_game_state_header_purity
	./$(BUILD)/test_game_state_header_purity
	@echo "PASS: game_state.h compiles and runs real logic with zero LVGL dependency"

test-all: test test-mem-budget test-game-state-purity

clean:
	rm -rf $(BUILD) $(BIN) $(BIN_GUI)

# ---- Build rules ----

$(BUILD):
	@mkdir -p $(BUILD)

define compile_rule
$(call obj_name,$(BUILD),$(1)): $(1) | $(BUILD)
	$(CC) $(CFLAGS) -c $$< -o $$@
endef

# GUI-only sources (sim_sdl2.c) need SDL2 include paths
define gui_compile_rule
$(call obj_name,$(BUILD),$(1)): $(1) | $(BUILD)
	$(CC) $(GUI_CFLAGS) -c $$< -o $$@
endef

GUI_ONLY_SRCS := $(filter-out $(ALL_SRCS),$(ALL_SRCS_GUI))

$(foreach src,$(ALL_SRCS),$(eval $(call compile_rule,$(src))))
$(foreach src,$(GUI_ONLY_SRCS),$(eval $(call gui_compile_rule,$(src))))

define compile_rule_mb
$(call obj_name,$(BUILD_MB),$(1)): $(1) | $(BUILD_MB)
	$(CC) $(CFLAGS_MB) -c $$< -o $$@
endef

$(BUILD_MB):
	@mkdir -p $(BUILD_MB)

$(foreach src,$(ALL_SRCS_NOMAIN),$(eval $(call compile_rule_mb,$(src))))

# Auto-generated header dependencies (-MMD)
-include $(wildcard $(BUILD)/*.d) $(wildcard $(BUILD_MB)/*.d)
