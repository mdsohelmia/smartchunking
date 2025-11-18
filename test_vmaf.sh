#!/bin/bash
# Test script to achieve VMAF 100

set -e

INPUT="/Users/sohelmia/sites/video/input.mp4"
CHUNKS_DIR="chunks"
OUTPUT="final.mp4"

echo "=== Testing for VMAF 100 ==="
echo

# Clean previous run
rm -rf "$CHUNKS_DIR" "$OUTPUT"

# Run chunkify
echo "1. Running chunkify..."
./bin/chunkify_cli --target 20 "$INPUT" "$CHUNKS_DIR" "$OUTPUT"

echo
echo "2. Computing VMAF score..."
ab-av1 vmaf --reference "$INPUT" --distorted "$OUTPUT"

echo
echo "3. Checking binary equality..."
if cmp -s "$INPUT" "$OUTPUT"; then
    echo "✓ Files are IDENTICAL (bit-perfect)"
else
    echo "✗ Files differ"
    # Check if it's just metadata
    ffmpeg -v error -i "$INPUT" -f md5 -map 0:v -c copy - 2>/dev/null > /tmp/input_video.md5
    ffmpeg -v error -i "$OUTPUT" -f md5 -map 0:v -c copy - 2>/dev/null > /tmp/output_video.md5
    ffmpeg -v error -i "$INPUT" -f md5 -map 0:a -c copy - 2>/dev/null > /tmp/input_audio.md5
    ffmpeg -v error -i "$OUTPUT" -f md5 -map 0:a -c copy - 2>/dev/null > /tmp/output_audio.md5

    echo "  Video MD5: $(cat /tmp/input_video.md5) vs $(cat /tmp/output_video.md5)"
    echo "  Audio MD5: $(cat /tmp/input_audio.md5) vs $(cat /tmp/output_audio.md5)"
fi

echo
echo "4. Comparing timestamps..."
ffprobe -v error -show_entries packet=pts,dts,duration,flags -of csv "$INPUT" > /tmp/input_packets.csv
ffprobe -v error -show_entries packet=pts,dts,duration,flags -of csv "$OUTPUT" > /tmp/output_packets.csv
if diff /tmp/input_packets.csv /tmp/output_packets.csv > /dev/null; then
    echo "✓ Timestamps are identical"
else
    echo "✗ Timestamp differences found:"
    diff /tmp/input_packets.csv /tmp/output_packets.csv | head -20
fi
