#!/bin/bash

if [ $# -lt 3 ]; then
  echo "Usage: ./stitch_from_json.sh chunks.json chunks_folder output.mp4"
  exit 1
fi

JSON="$1"
DIR="$2"
OUT="$3"

# Temp list file for FFmpeg
LIST=$(mktemp)

COUNT=$(jq length "$JSON")

# Build ordered concat list
for ((i=0; i<COUNT; i++)); do
    FILE="$DIR/chunk_$(printf "%04d" $i).mp4"
    if [ ! -f "$FILE" ]; then
        echo "Missing chunk: $FILE" >&2
        exit 1
    fi
    echo "file '$FILE'" >> "$LIST"
done

# Stitch using concat demuxer (safe, exact, no re-encode)
ffmpeg -y -f concat -safe 0 -i "$LIST" -c copy "$OUT"

echo "Stitched → $OUT"

rm "$LIST"
