# The composer (from geosim)

`src/composer.c` is geosim's procedural composer (`src/client/audio/composer.cpp`,
C++20) ported to C23. geosim's `docs/14-audio.md` explains the musical rules:
style families per mood, forms of 8/16-bar sections, modes and borrowed
chords, motifs, grooves, transitions, the arp solo and the choir block.

## Parity

The port is bit-exact: the same seed and mood write the same song as the
C++ original. `work/parity/run.sh` builds the original composer standalone
(`dump.cpp`) and the port (`dump.c`), dumps every field of seeds 1–300 ×
6 moods plus `compose_menu` 1–20 (1820 songs, ~9.9M events, floats as
`%a`) and compares them, then reruns the port under ASan + UBSan. At the
port: identical (sha256 `2d4e1ff6…fbb43`), sanitizers clean. A full run
takes about 2.5 minutes.

`tests/test_composer.c` checks determinism, ordering, ranges and golden
hashes taken from the C++ dump.

## Differences from the C++

- Fixed-size internals (plan ≤ 64 sections, mutes ≤ 384, phrases ≤ 256
  lead notes); the code's own limits stay well below them, and `compose`
  returns false instead of diverging if one were hit.
- `Song.description` holds 4096 bytes (the longest seen is 843).
- The recursive `push_tonal` is a loop; two always-zero parameters are gone.
