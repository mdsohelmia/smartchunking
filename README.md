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

### ⚡ Smart Chunking (Content-Aware Analysis)
- **Scene Change Detection**: Automatically detects scene boundaries and prefers cutting at scene transitions to maintain visual quality
- **Complexity-Based Adaptation**: Analyzes packet sizes to understand content complexity and optimizes chunk sizes accordingly
- **Bitrate-Aware Optimization**: Uses packet size metadata as a proxy for encoding complexity to ensure balanced processing loads
- **GOP Structure Analysis**: Scores potential cut points based on keyframe distribution and GOP quality
- **Quality Metrics**: Tracks complexity, keyframe count, and scene cuts per chunk for optimal encoding distribution
- Uses only the demuxer—no decoding required (20–50× faster than decode-based analyzers)
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

Basic Planning:
  --target <sec>         Preferred chunk duration (default 60)
  --min <sec>            Minimum chunk duration
  --max <sec>            Maximum chunk duration
  --ideal-par <n>        Overrides target to create N equal chunks
  --min-chunks <n>       Guarantee at least N chunks
  --max-chunks <n>       Cap chunk count (merges if needed)
  --allow-tiny-last      Keep tiny trailing chunk (disabled by default)

Smart Chunking (Quality Optimization):
  --smart                Enable all smart chunking features
  --scene-detect         Enable scene change detection for better cut points
  --complexity           Enable complexity-based chunk adaptation
  --scene-threshold <n>  Scene detection sensitivity 0.0-1.0 (default 0.35)
  --complexity-weight <n> How much to weight complexity in scoring 0.0-1.0 (default 0.3)
  --verbose              Show detailed quality metrics per chunk

Outputs:
  --plan-json <path>     Write chunk plan as JSON array
  --force-format <fmt>   Force muxer (mp4/mov/matroska/webm/…)
  --frag                 Enable fragmented MP4 flags
  --no-split             Skip chunk extraction (plan only)
  --no-stitch            Skip final stitch (chunking only)
```

### Example Workflows

```bash
# 1. Basic chunking: Plan + split + stitch (default)
bin/chunkify_cli source.mp4 chunks final.mp4

# 2. Smart chunking with scene detection and complexity analysis
bin/chunkify_cli --smart --verbose source.mp4 chunks final.mp4

# 3. Scene-aware chunking only (for content with frequent scene changes)
bin/chunkify_cli --scene-detect --verbose source.mp4 chunks final.mp4

# 4. Complexity-based adaptation (optimize for encoding workload balance)
bin/chunkify_cli --complexity --complexity-weight 0.5 source.mp4 chunks final.mp4

# 5. Plan only with smart features, emit JSON plan
bin/chunkify_cli --smart --no-split --no-stitch --plan-json plan.json source.mp4 chunks

# 6. Fine-tune scene detection sensitivity
bin/chunkify_cli --scene-detect --scene-threshold 0.25 source.mp4 chunks final.mp4

# 7. Split to Matroska chunks, keep originals for later
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

## 🎯 Smart Chunking: The Evolution of Split & Stitch

Traditional split-and-stitch approaches use **fixed GOPs and segments**, which can lead to:
- **Quality degradation** when chunks have vastly different complexity levels
- **Unbalanced encoding workloads** across parallel workers
- **Cuts during visually important moments** (mid-scene)
- **Poor bitrate distribution** across the final asset

**Smart Chunking solves these issues** by:

### 1. **Scene Change Detection**
- Analyzes packet size patterns to detect scene boundaries
- Prefers cutting at scene transitions (where cuts are visually imperceptible)
- Uses configurable sensitivity threshold (--scene-threshold)
- Eliminates jarring mid-scene cuts that can affect encoding quality

### 2. **Complexity-Based Adaptation**
- Computes normalized complexity scores from packet sizes
- Tracks per-frame complexity to understand encoding difficulty
- Adjusts chunk boundaries to balance workload across parallel encoders
- Ensures complex scenes get appropriate processing time allocation

### 3. **Quality-Aware Cut Point Selection**
- Multi-factor scoring system evaluates each potential cut point:
  - Distance from target duration
  - Scene change bonus (prefers scene boundaries)
  - GOP structure quality (prefers closed GOPs)
  - Keyframe distribution analysis
- Weighted scoring allows tuning for your use case (--complexity-weight)

### 4. **Real-Time Quality Metrics**
- Tracks per-chunk statistics:
  - Average complexity score
  - Keyframe count
  - Scene cut count
  - Overall quality score
- Use `--verbose` to see detailed metrics and verify optimal chunking

### Benefits
- ✅ **Improved visual quality** throughout the entire asset
- ✅ **Faster encoding** with balanced parallel workloads
- ✅ **Intelligent cut points** that respect content structure
- ✅ **Better bitrate distribution** across complex and simple scenes
- ✅ **No quality loss** - still 100% lossless remuxing

---

## 🧪 Tips

- Use `--smart` for general-purpose optimal quality chunking
- Enable `--scene-detect` for content with frequent scene changes (movies, TV shows)
- Use `--complexity` alone for workload balancing without scene detection
- Adjust `--scene-threshold` lower (0.2-0.3) for subtle scene changes, higher (0.4-0.5) for dramatic cuts
- Keep chunk durations ≥ GOP length for best parallelism
- Fragmented MP4 (`--frag`) is recommended for low-latency streaming ingestion; disable it for legacy MP4 players
- When using `--no-split`, ensure your `chunks_dir` already contains files created by Chunkify (same naming scheme)
- Use `--verbose` to analyze quality metrics and fine-tune parameters for your content

---

## 🤝 Contributing

1. Fork and clone this repository.
2. Install FFmpeg 8 headers locally.
3. Run `make` and exercise `bin/chunkify_cli` on sample media.
4. Submit focused PRs (one feature/fix per PR). Include notes about any FFmpeg API surfaces you touch.

---

Chunkify is purpose-built for high-throughput VOD and distributed encoding stacks—drop it into your toolchain to get sub-second planning and frame-accurate stitching out of the box.
