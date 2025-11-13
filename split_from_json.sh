#!/bin/bash

if [ $# -lt 2 ]; then
  echo "Usage: ./split_from_json.sh input.mp4 chunks.json"
  exit 1
fi

INPUT="$1"
JSON="$2"
OUTDIR="chunks"

mkdir -p "$OUTDIR"

# Extract fields from JSON using jq
COUNT=$(jq length "$JSON")

for ((i=0; i<COUNT; i++)); do
    START=$(jq -r ".[$i].start" "$JSON")
    END=$(jq -r ".[$i].end" "$JSON")

    OUT="$OUTDIR/chunk_$(printf "%04d" $i).mp4"

    ffmpeg -y -ss "$START" -to "$END" -i "$INPUT" -c copy "$OUT"

    echo "Created $OUT"
done
