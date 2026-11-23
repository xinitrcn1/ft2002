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

## Variations (FT2002's, off in `compose`)

`compose_ex(out, seed, mood, &(ComposeOptions){.variations = true})`
draws from its own generator after geosim's choices, so a seed keeps
geosim's tempo, key, mode, style, groove and sounds:
- **progressions**: 40 more (royal road, ii–V–I–vi, doo-wop, i–v–VI–iv,
  VI–iv–i–VII, lydian I–II–iii–II, phrygian i–bII–bIII–bII; borrowed
  I–bIII–IV–I, I–bVI–IV–I, IV–iv–I–V, i–bII–V–i; relative-key pivots).
  Verse, chorus and bridge each have about an even chance to take one.
- **arpeggio orders**: 14 more (Alberti, pedals under a moving voice,
  cascades, rolls), per section class; **wide cells**: 6 more.
- **arp rhythms** for 16th arps, per section class: 3-3-2 pushes, gallop,
  xx.x, syncopated, 8ths, a breath before each half bar, off the beat.

Over seeds 1–300 per mood (`work/clicks/variety.c`), distinct
verse/chorus/bridge progressions, geosim → with variations: calm 215 →
285, tense 159 → 256, crisis 184 → 259, triumph 232 → 286, wartime 197 →
271, frustrated 182 → 250; distinct arp figures (the first 16 arp notes'
intervals and rhythm): 240 → 271, 261 → 289, 264 → 290, 259 → 293,
144 → 253, 146 → 245. `compose` is unchanged: the parity check and the
golden hashes still hold.
