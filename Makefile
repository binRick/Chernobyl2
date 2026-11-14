# Chernobyl 3D - Makefile (raylib 6.0, the version Chernobyl pins).
# Links a vendored static raylib archive so the build is self-contained.

NAME    := chernobyl2
BUILD   := build
BIN     := $(BUILD)/$(NAME)
SRC     := src/main.c
HDRS    := $(wildcard src/*.h)

CC      ?= clang
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -DGL_SILENCE_DEPRECATION
UNAME_S := $(shell uname -s)

RAYLIB_DIR := vendor/raylib
INCLUDES   := -I$(RAYLIB_DIR)/include
LIBS       := $(RAYLIB_DIR)/lib/libraylib.a

ifeq ($(UNAME_S),Darwin)
  SRC  += src/macicon.m          # Objective-C: sets the Dock / Cmd-Tab icon
  LIBS += -framework OpenGL -framework Cocoa -framework IOKit \
          -framework CoreVideo -framework CoreAudio
endif
ifeq ($(UNAME_S),Linux)
  LIBS += -lm -lpthread -ldl -lrt -lX11
endif

.PHONY: all run debug clean raylib6
all: $(BIN)

$(BIN): $(SRC) $(HDRS) $(RAYLIB_DIR)/lib/libraylib.a | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(SRC) $(LIBS)

$(BUILD):
	mkdir -p $(BUILD)

run: $(BIN)
	./$(BIN)

debug: $(BIN)
	./$(BIN) --debug

clean:
	rm -rf $(BUILD)

# Rebuild the vendored raylib 6.0 archive from source if it's ever missing.
raylib6:
	@if [ ! -d vendor/raylib-src ]; then \
	  git clone --depth 1 --branch 6.0 https://github.com/raysan5/raylib vendor/raylib-src; fi
	$(MAKE) -C vendor/raylib-src/src PLATFORM=PLATFORM_DESKTOP -j4
	mkdir -p $(RAYLIB_DIR)/include $(RAYLIB_DIR)/lib
	cp vendor/raylib-src/src/raylib.h vendor/raylib-src/src/raymath.h vendor/raylib-src/src/rlgl.h $(RAYLIB_DIR)/include/
	cp vendor/raylib-src/src/libraylib.a $(RAYLIB_DIR)/lib/libraylib.a

# ---- Web build (WebGL 2 + WebAssembly) --------------------------------------
#   python3 tools/web/pack_web.py     # build/web/fs: the reduced asset set (textures re-encoded)
#   make web                          # build/web/dist/index.{html,js,wasm,data}
#   make web-serve                    # ...and serve it on http://localhost:8642/
# Needs Homebrew emscripten (emcc wants python >= 3.10, which Xcode's python3 shadows) and
# vendor/raylib/lib/libraylib.web.a: raylib 6.0 built PLATFORM_WEB / GRAPHICS_API_OPENGL_ES3 with
# SUPPORT_FILEFORMAT_JPG (the web textures are JPEG). ../Chernobyl builds exactly that archive.
EMCC_ENV := PATH="/opt/homebrew/bin:$$PATH" EMCC_PYTHON=/opt/homebrew/bin/python3.14
WEBFS    := $(BUILD)/web/fs
WEBDIST  := $(BUILD)/web/dist
.PHONY: web web-serve
web: src/main.c $(HDRS) web/shell.html $(RAYLIB_DIR)/lib/libraylib.web.a
	@test -d $(WEBFS)/assets || { echo "run: python3 tools/web/pack_web.py"; exit 1; }
	@mkdir -p $(WEBDIST)
	$(EMCC_ENV) emcc -std=gnu11 -O2 -Wall -Wno-unused-result -Wno-unused-function \
	    -DPLATFORM_WEB -DGRAPHICS_API_OPENGL_ES3 $(INCLUDES) -o $(WEBDIST)/index.html src/main.c \
	    $(RAYLIB_DIR)/lib/libraylib.web.a \
	    -s USE_GLFW=3 -s MIN_WEBGL_VERSION=2 -s MAX_WEBGL_VERSION=2 -s FORCE_FILESYSTEM=1 \
	    -s ALLOW_MEMORY_GROWTH=1 -s INITIAL_MEMORY=512MB -s MAXIMUM_MEMORY=2GB -s STACK_SIZE=4MB \
	    --preload-file $(WEBFS)@/ --shell-file web/shell.html
	@B=$$(date +%s); sed -i '' -e "s/__C2_BUILD__/$$B/g" -e "s/src=\"index.js\"/src=\"index.js?v=$$B\"/" -e "s/src=index.js/src=index.js?v=$$B/" $(WEBDIST)/index.html; grep -c "v=$$B" $(WEBDIST)/index.html   # the build's number on every file it loads (see shell.html)
	@if [ -d $(BUILD)/web/later/assets ]; then mkdir -p $(WEBDIST)/assets && cp $(BUILD)/web/later/assets/* $(WEBDIST)/assets/; fi   # the enemies the game fetches once it is up (staged loading)
	@if [ -d $(BUILD)/web/intro ]; then cp $(BUILD)/web/intro/* $(WEBDIST)/; fi     # the title intro the page plays (pack_web.py)
	@ls -la $(WEBDIST) | awk '{print $$5, $$9}'
web-serve: web
	cd $(WEBDIST) && python3 -m http.server 8642
