#!/usr/local/bin/bash
# Parity check: geosim's C++ composer vs the C23 port, bit-exact.
# Builds both dumpers one after the other (small host: no parallel builds),
# compares their canonical dumps of seeds 1..300 x 6 moods + menu 1..20,
# then runs the port under ASan/UBSan over the same seeds.
# usage: work/parity/run.sh [first last [menu_last]]
set -euo pipefail
cd "$(dirname "$0")/../.."
GEO=${GEOSIM:-$HOME/workspace/geosim}/src/client/audio
OUT=${OUT:-$HOME/.cache/ft2002-parity}
mkdir -p "$OUT"
ARGS=("${@:-1 300 20}")
[ $# -eq 0 ] && ARGS=(1 300 20)

[ "$OUT/dump_cpp" -nt "$GEO/composer.cpp" ] && [ "$OUT/dump_cpp" -nt work/parity/dump.cpp ] ||
	c++ -std=c++20 -O2 -I"$GEO" work/parity/dump.cpp "$GEO/composer.cpp" -o "$OUT/dump_cpp"
cc -std=c23 -O2 -Wall -Wextra -Wno-missing-field-initializers -Isrc src/song.c src/composer.c work/parity/dump.c -lm -o "$OUT/dump_c"

"$OUT/dump_cpp" "${ARGS[@]}" > "$OUT/cpp.txt"
"$OUT/dump_c" "${ARGS[@]}" > "$OUT/c.txt"
echo "songs: $(grep -c -E '^(song|menu) ' "$OUT/cpp.txt") (C++) $(grep -c -E '^(song|menu) ' "$OUT/c.txt") (C)"
echo "events: $(grep -c '^ev ' "$OUT/cpp.txt")"
sha256 -q "$OUT/cpp.txt" "$OUT/c.txt" 2>/dev/null || sha256sum "$OUT/cpp.txt" "$OUT/c.txt"
if cmp "$OUT/cpp.txt" "$OUT/c.txt"; then echo "PARITY: identical"; else echo "PARITY: DIFFERENT"; diff "$OUT/cpp.txt" "$OUT/c.txt" | head -20; exit 1; fi

if [ "${SAN:-1}" = 1 ]; then
	cc -std=c23 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Wno-missing-field-initializers \
		-Isrc src/song.c src/composer.c work/parity/dump.c -lm -o "$OUT/dump_san"
	"$OUT/dump_san" "${ARGS[@]}" > "$OUT/san.txt"
	if cmp "$OUT/cpp.txt" "$OUT/san.txt"; then echo "SANITIZERS: clean, output identical"; else echo "SANITIZERS: output differs"; exit 1; fi
fi
