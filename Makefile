# ============================================================
# Chunkify Smart Chunking — Unified Makefile
# macOS (Apple Silicon) + Linux
# ============================================================

CC      = clang
SRC_DIR = src
OBJ_DIR = build/obj
BIN_DIR = bin

BIN = $(BIN_DIR)/chunkify_cli

SRC = $(SRC_DIR)/smartchunk.c \
      $(SRC_DIR)/splitter.c \
      $(SRC_DIR)/stitcher.c \
      $(SRC_DIR)/chunkify_cli.c

OBJ = $(SRC:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# ------------------------------------------------------------
# macOS pkg-config fix (Homebrew)
# ------------------------------------------------------------
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

ifeq ($(UNAME_S),Darwin)
    export PKG_CONFIG_PATH := /opt/homebrew/lib/pkgconfig
endif

# ------------------------------------------------------------
# FFmpeg pkg-config flags
# ------------------------------------------------------------
FFMPEG_CFLAGS := $(shell pkg-config --cflags libavformat libavcodec libavutil libavfilter)
FFMPEG_LIBS   := $(shell pkg-config --libs   libavformat libavcodec libavutil libavfilter)

# ------------------------------------------------------------
# Compiler flags
# ------------------------------------------------------------
CFLAGS  = -O3 -Wall -Wextra -std=c11 $(FFMPEG_CFLAGS)
LDFLAGS = $(FFMPEG_LIBS)

ifeq ($(UNAME_S),Darwin)
    ifeq ($(UNAME_M),arm64)
        CFLAGS += -DFFMPEG_MACOS_ARM64
    endif
endif

# Allow pthread on Linux explicitly
ifeq ($(UNAME_S),Linux)
    LDFLAGS += -pthread
endif

# ------------------------------------------------------------
# Build rules
# ------------------------------------------------------------

all: $(BIN)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Compile C → .o
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Link executable
$(BIN): $(OBJ) | $(BIN_DIR)
	$(CC) -o $(BIN) $(OBJ) $(LDFLAGS)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

.PHONY: all clean
