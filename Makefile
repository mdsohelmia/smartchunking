# ============================================================
# Chunkify Smart Chunking — macOS Makefile (bin + obj dirs)
# ============================================================

CC = clang

SRC_DIR = .
OBJ_DIR = build/obj
BIN_DIR = bin

BIN = $(BIN_DIR)/chunkify_cli

SRC = smartchunk.c chunkify_cli.c
OBJ = $(SRC:%.c=$(OBJ_DIR)/%.o)

# Homebrew pkg-config path
export PKG_CONFIG_PATH := /opt/homebrew/lib/pkgconfig

# 🔹 Split compile flags and libs from pkg-config
FFMPEG_CFLAGS := $(shell pkg-config --cflags libavformat libavcodec libavutil libavfilter)
FFMPEG_LIBS   := $(shell pkg-config --libs   libavformat libavcodec libavutil libavfilter)

# Base flags
CFLAGS  = -O3 -Wall -Wextra -std=c11 $(FFMPEG_CFLAGS)
LDFLAGS = $(FFMPEG_LIBS)

UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

ifeq ($(UNAME_S),Darwin)
    ifeq ($(UNAME_M),arm64)
        CFLAGS += -DFFMPEG_MACOS_ARM64
    endif
endif

# ============================================================
# Build rules
# ============================================================

all: $(BIN)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): $(OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(BIN) $(OBJ) $(LDFLAGS)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

.PHONY: all clean
