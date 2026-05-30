# Chernobyl skeletal-anim prototype — Makefile
# Mirrors Chernobyl's build approach: raylib via Homebrew on macOS, Emscripten
# for the web target.

NAME    := skeletal_proto
BUILD   := build
BIN     := $(BUILD)/$(NAME)
SRC     := src/main.c

CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter
UNAME_S := $(shell uname -s)

# ---- locate raylib (prefer Homebrew, fall back to /usr/local) -----------
RAYLIB_PREFIX := $(shell brew --prefix raylib 2>/dev/null)
ifeq ($(strip $(RAYLIB_PREFIX)),)
  RAYLIB_PREFIX := /usr/local
endif

# RAYLIB6=1 links against a vendored raylib 6.0 static archive (built by
# `make raylib6`), matching how Chernobyl pins raylib 6.0. Without it we link
# Homebrew's raylib (currently 5.5), whose glTF *animation* import is weaker —
# 6.0 redesigned skeletal animation, so glTF clips (.glb from Quaternius/
# Mixamo) need 6.0 to import correctly. IQM works on both.
RAYLIB6_SRC := vendor/raylib-src/src
RAYLIB6_A   := $(RAYLIB6_SRC)/libraylib.a

ifeq ($(RAYLIB6),1)
  INCLUDES := -I$(RAYLIB6_SRC)
  LIBS     := $(RAYLIB6_A)
else
  INCLUDES := -I$(RAYLIB_PREFIX)/include
  LIBS     := -L$(RAYLIB_PREFIX)/lib -lraylib
endif

ifeq ($(UNAME_S),Darwin)
  LIBS += -framework OpenGL -framework Cocoa -framework IOKit \
          -framework CoreVideo -framework CoreAudio
endif
ifeq ($(UNAME_S),Linux)
  LIBS += -lm -lpthread -ldl -lrt -lX11
endif

# ---- native ------------------------------------------------------------
.PHONY: all run clean web web-serve
all: $(BIN)

$(BIN): $(SRC) | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(SRC) $(LIBS)

$(BUILD):
	mkdir -p $(BUILD)

run: $(BIN)
	./$(BIN)

clean:
	rm -rf $(BUILD) dist-web

# Build a vendored raylib 6.0 static archive (clone first if absent), to link
# against with RAYLIB6=1. Mirrors Chernobyl's pinned-6.0 setup.
.PHONY: raylib6
raylib6: $(RAYLIB6_A)
$(RAYLIB6_A):
	@if [ ! -d $(RAYLIB6_SRC) ]; then \
	  git clone --depth 1 --branch 6.0 https://github.com/raysan5/raylib vendor/raylib-src; fi
	$(MAKE) -C $(RAYLIB6_SRC) PLATFORM=PLATFORM_DESKTOP -j4

# ---- web (Emscripten) --------------------------------------------------
# Requires emsdk on PATH and a raylib built for PLATFORM_WEB.
# Override RAYLIB_WEB to point at libraylib.web.a, e.g.:
#   make web RAYLIB_WEB=vendor/raylib/src/libraylib.web.a
RAYLIB_WEB         ?= vendor/raylib/src/libraylib.web.a
RAYLIB_WEB_INCLUDE ?= vendor/raylib/src
WEBDIR             := dist-web

web: $(SRC)
	mkdir -p $(WEBDIR)
	emcc -o $(WEBDIR)/index.html $(SRC) -Os -Wall \
	    $(RAYLIB_WEB) -I$(RAYLIB_WEB_INCLUDE) \
	    -DPLATFORM_WEB -s USE_GLFW=3 -s ASYNCIFY -s GL_ENABLE_GET_PROC_ADDRESS \
	    -s ALLOW_MEMORY_GROWTH=1 --preload-file assets \
	    --shell-file $(RAYLIB_WEB_INCLUDE)/minshell.html

web-serve: web
	cd $(WEBDIR) && python3 -m http.server 8000
