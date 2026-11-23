/* The chord library: the well-known shapes rank first, every voicing is
 * playable and spells its chord, names parse back. */
#include "chords.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, ...)                                                                                                  \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			fails++;                                                                                       \
			printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                    \
			printf(__VA_ARGS__);                                                                           \
			printf("\n");                                                                                  \
		}                                                                                                      \
	} while (0)

static ChordLib lib;

/* the shape of the best voicing of a chord, or among the first k */
static bool among(const char *chord, const char *shape, int k)
{
	int r, q;
	if (!chord_parse(chord, &r, &q))
		return false;
	char buf[32];
	for (int i = 0; i < chordlib_count(&lib, r, q) && i < k; i++) {
		voicing_shape(&lib, chordlib_get(&lib, r, q, i), buf, sizeof buf);
		if (!strcmp(buf, shape))
			return true;
	}
	return false;
}

static void show(const char *chord)
{
	int r, q;
	chord_parse(chord, &r, &q);
	printf("%-6s", chord);
	char buf[32];
	for (int i = 0; i < chordlib_count(&lib, r, q) && i < 6; i++) {
		voicing_shape(&lib, chordlib_get(&lib, r, q, i), buf, sizeof buf);
		printf(" %-10s", buf);
	}
	printf("\n");
}

int main(int argc, char **argv)
{
	const int std[6] = {40, 45, 50, 55, 59, 64};
	chordlib_build(&lib, std, 6);
	if (argc > 1) {
		for (int i = 1; i < argc; i++)
			show(argv[i]);
		return 0;
	}
	/* the open chords every method book starts with: first */
	CHECK(among("C", "x32010", 1), "C");
	CHECK(among("G", "320003", 3), "G");
	CHECK(among("D", "xx0232", 1), "D");
	CHECK(among("A", "x02220", 1), "A");
	CHECK(among("E", "022100", 1), "E");
	CHECK(among("Am", "x02210", 1), "Am");
	CHECK(among("Em", "022000", 1), "Em");
	CHECK(among("Dm", "xx0231", 1), "Dm");
	CHECK(among("E7", "020100", 3), "E7");
	CHECK(among("A7", "x02020", 3), "A7");
	CHECK(among("Cmaj7", "x32000", 2), "Cmaj7");
	/* barre chords near the top */
	CHECK(among("F", "133211", 3), "F");
	CHECK(among("Bm", "x24432", 3) || among("Bm", "x2443x", 3), "Bm");
	CHECK(among("E5", "022xxx", 2) || among("E5", "02xxxx", 2), "E5");
	CHECK(among("A5", "x022xx", 2) || among("A5", "x02xxx", 2), "A5");

	/* every voicing: spells its chord, root in the bass, a hand can play it */
	int total = 0;
	for (int r = 0; r < 12; r++)
		for (int q = 0; q < Q_COUNT; q++) {
			int n = chordlib_count(&lib, r, q);
			CHECK(n > 0, "no voicing for %s%s", note_names[r], qualities[q].suffix);
			for (int i = 0; i < n; i++) {
				const Voicing *v = chordlib_get(&lib, r, q, i);
				u8 notes[6];
				voicing_notes(&lib, v, notes);
				int bass = -1, lo = 99, hi = 0;
				for (int s = 0; s < 6; s++) {
					if (!notes[s])
						continue;
					if (bass < 0)
						bass = notes[s] % 12;
					int pc = (notes[s] % 12 - r + 12) % 12;
					bool in = false;
					for (int k = 0; k < qualities[q].n; k++)
						in |= qualities[q].iv[k] == pc;
					CHECK(in, "%s%s: a foreign note", note_names[r], qualities[q].suffix);
					if (v->fret[s] > 0)
						lo = v->fret[s] < lo ? v->fret[s] : lo, hi = v->fret[s] > hi ? v->fret[s] : hi;
				}
				CHECK(bass == r, "%s%s: bass", note_names[r], qualities[q].suffix);
				CHECK(lo == 99 || hi - lo <= 3, "%s%s: span", note_names[r], qualities[q].suffix);
				if (i > 0)
					CHECK(v->score >= chordlib_get(&lib, r, q, i - 1)->score, "sorted");
				total++;
			}
		}
	printf("%d voicings\n", total);

	/* names */
	int r, q;
	CHECK(chord_parse("F#m7", &r, &q) && r == 6 && q == Q_MIN7, "F#m7");
	CHECK(chord_parse("Bbmaj7", &r, &q) && r == 10 && q == Q_MAJ7, "Bbmaj7");
	CHECK(chord_parse("E5", &r, &q) && r == 4 && q == Q_POWER, "E5");
	CHECK(!chord_parse("H", &r, &q), "H");
	CHECK(chord_identify((1 << 0) | (1 << 4) | (1 << 7), 0, &r, &q) && r == 0 && q == Q_MAJ, "identify C");
	CHECK(chord_identify((1 << 9) | (1 << 0) | (1 << 4), 9, &r, &q) && r == 9 && q == Q_MIN, "identify Am");

	/* strums: a bar of Folk is 7 strums */
	Song s;
	song_init(&s);
	s.ntracks = 1;
	u8 notes[6];
	chord_parse("G", &r, &q);
	voicing_notes(&lib, chordlib_get(&lib, r, q, 0), notes);
	int n = strum_write(&s, 0, 0, ticks_per_bar, notes, 6, &strum_patterns[strum_find("Folk")], 90, ART_NORMAL, false);
	CHECK(n == 6, "folk strums: %d", n);
	CHECK(s.nevents == 6 && s.events[0].fx == FX_STRUM && s.events[0].strum > 0, "a down strum first");
	song_free(&s);

	if (fails)
		printf("%d failures\n", fails);
	return fails != 0;
}
