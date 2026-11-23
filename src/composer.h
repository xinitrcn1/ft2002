/* The procedural composer: seed + mood -> a complete song (form, harmony,
 * motifs, rhythm, instruments, mix).
 *
 * Ported from geosim (src/client/audio/composer.hpp/.cpp, C++20) to C23,
 * bit-exact: the same seed and mood give the same song as geosim's composer,
 * on any platform (own PRNG, no library distributions). */
#ifndef FT_COMPOSER_H
#define FT_COMPOSER_H

#include "song.h"

/* `out` is song_init'ed here (any previous content is not freed). Returns
 * false only when memory runs out (out is then freed and empty). */
bool compose(Song *out, u64 seed, Mood m);
/* The main menu's song: wartime with an arp solo (the arpeggio carries the
 * melody). Deterministic: the first song of the seed chain that has one. */
bool compose_menu(Song *out, u64 seed);
/* splitmix64 step: the next song's seed from the previous one. */
u64 next_seed(u64 seed);

#endif
