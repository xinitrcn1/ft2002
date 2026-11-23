#!/bin/sh
# The headless workbench: run the UI scripts in tests/ui, screenshots into
# the build folder. Needs a Vulkan device (a GPU or lavapipe).
set -e
FT=${FT2002:-build/ft2002}
OUT=${OUT:-$(dirname "$FT")/ui-shots}
mkdir -p "$OUT"
dir=$(dirname "$0")
for s in "$dir"/ui/*.txt; do
	sed "s|OUT/|$OUT/|g" "$s" > "$OUT/script.txt"
	echo "== $s"
	"$FT" --headless 1440x900 --script "$OUT/script.txt"
done
