# **Chunkify – Ultra-Fast Smart Video Chunking, Splitting & Stitching Engine**

Chunkify is a high-performance C/FFmpeg toolkit for **lossless video chunking**, **parallel remuxing**, and **bit-perfect stitching**. It was built for production ingest/transcode pipelines where speed, determinism, and cross-platform compatibility matter.

Chunkify works by:

- **Scanning packets without decoding** → 20–50× faster than decode-based analyzers
- **Detecting GOP-safe cut points** and planning optimal chunk boundaries
- **Remuxing each chunk** (zero quality loss, no re-encode)
- **Stitching chunks back together** with monotonic timestamps and CMAF-friendly options

Built on **FFmpeg 8.x** and optimized for **macOS ARM64** and **Linux (gcc)**.

---

## 🚀 Features

### ⚡ Smart Chunking (Packet-Level Analysis)
- Uses only the demuxer—no decoding required
- Enforces min/max/target durations or auto-splits by desired parallelism
- Avoids tiny tail chunks via adaptive merging

### ⚡ Lossless Splitting
- Remux-only extraction for every planned chunk
- Optional fragmented MP4 output (`--frag`)
- Stream parameters preserved verbatim

### ⚡ Perfect Stitching
- Timestamp normalization + offset tracking to guarantee contiguous playback
- Supports mp4/mov/matroska/webm (auto-detected or forced)
- Bit-exact when source chunks weren’t re-encoded

---

## 🛠 Prerequisites

- FFmpeg 8 development headers (`libavformat`, `libavcodec`, `libavutil`, `libavfilter`)
- `pkg-config`, `clang`/`gcc`, `make`
- macOS 13+ (Apple Silicon) or Linux (glibc ≥ 2.31)

```bash
# macOS
brew install ffmpeg pkg-config

# Ubuntu / Debian
sudo apt-get install ffmpeg pkg-config build-essential
```

---

## 🧱 Building

```bash
make        # builds bin/chunkify_cli
make clean  # removes build artifacts
```

The Makefile automatically:
- Detects macOS vs Linux
- Sets Homebrew’s pkg-config path when needed
- Adds pthread linkage on Linux

---

## 🚦 CLI Usage

```
bin/chunkify_cli [options] <input> <chunks_dir> [final_output]

Planning:
  --target <sec>         Preferred chunk duration (default 60)
  --min <sec>            Minimum chunk duration
  --max <sec>            Maximum chunk duration
  --ideal-par <n>        Overrides target to create N equal chunks
  --min-chunks <n>       Guarantee at least N chunks
  --max-chunks <n>       Cap chunk count (merges if needed)
  --allow-tiny-last      Keep tiny trailing chunk (disabled by default)

Outputs:
  --plan-json <path>     Write chunk plan as JSON array
  --force-format <fmt>   Force muxer (mp4/mov/matroska/webm/…)
  --frag                 Enable fragmented MP4 flags
  --no-split             Skip chunk extraction (plan only)
  --no-stitch            Skip final stitch (chunking only)
```

### Example Workflows

```bash
# 1. Plan + split + stitch (default)
bin/chunkify_cli source.mp4 chunks final.mp4

# 2. Plan only, emit JSON plan
bin/chunkify_cli --no-split --no-stitch --plan-json plan.json source.mp4 chunks

# 3. Split to Matroska chunks, keep originals for later
bin/chunkify_cli --force-format matroska --no-stitch source.mp4 chunks
```

Chunk files are written as `chunks/chunk_0000.mp4`, `chunk_0001.mp4`, … . Stitching expects this naming convention when rebuilding the final asset.

---

## 📦 JSON Plan Format

Using `--plan-json plan.json` produces:

```json
[
  {"index": 0, "start": 0.000, "end": 60.000},
  {"index": 1, "start": 60.000, "end": 120.000}
]
```

Values correspond to `sc_chunk { index, start, end }` in seconds. You can feed this data into custom schedulers or external workers.

---

## 🧩 Architecture

| Module           | Purpose |
|------------------|---------|
| `smartchunk.*`   | Probes packets and plans chunks based on keyframes plus configurable constraints. |
| `splitter.*`     | Remuxes planned chunks into standalone files (supports fragmented MP4). |
| `stitcher.*`     | Concatenates the generated chunks, rescaling timestamps to avoid gaps. |
| `chunkify_cli.c` | CLI surface tying everything together, parses options, emits plans, and controls the pipeline. |

Each module exposes a clean C interface so you can embed Chunkify in other apps or services.

---

## 🧪 Tips

- Keep chunk durations ≥ GOP length for best parallelism.
- Fragmented MP4 (`--frag`) is recommended for low-latency streaming ingestion; disable it for legacy MP4 players.
- When using `--no-split`, ensure your `chunks_dir` already contains files created by Chunkify (same naming scheme).

---

## 🤝 Contributing

1. Fork and clone this repository.
2. Install FFmpeg 8 headers locally.
3. Run `make` and exercise `bin/chunkify_cli` on sample media.
4. Submit focused PRs (one feature/fix per PR). Include notes about any FFmpeg API surfaces you touch.

---

Chunkify is purpose-built for high-throughput VOD and distributed encoding stacks—drop it into your toolchain to get sub-second planning and frame-accurate stitching out of the box.
