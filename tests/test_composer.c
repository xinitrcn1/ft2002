/* The composer port: determinism, invariants and golden values taken from
 * geosim's original C++ composer (work/parity/dump.cpp). Returns nonzero
 * on failure. */
#include "composer.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond, ...)                                                    \
	do {                                                                \
		if(!(cond)) {                                               \
			++failures;                                         \
			fprintf(stderr, "%s:%d: %s: ", __FILE__, __LINE__, #cond); \
			fprintf(stderr, __VA_ARGS__);                        \
			fputc('\n', stderr);                                \
		}                                                           \
	} while(0)

static bool event_eq(const NoteEvent *a, const NoteEvent *b)
{
	return a->tick == b->tick && a->len == b->len && a->ch == b->ch && a->vel == b->vel && a->layer == b->layer && a->fx == b->fx && a->count == b->count &&
	       a->art == b->art && memcmp(a->notes, b->notes, sizeof a->notes) == 0 && a->gate == b->gate && a->strum == b->strum && a->string == b->string;
}

/* FNV-1a over the events in work/parity's canonical line form */
static u64 events_hash(const Song *s)
{
	u64 h = 0xCBF29CE484222325ull;
	char line[256];
	for(u32 i = 0; i < s->nevents; ++i) {
		const NoteEvent *e = &s->events[i];
		snprintf(line, sizeof line, "ev %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %d %u\n", e->tick, e->len, e->ch, e->vel, e->layer, (unsigned)e->fx, e->count,
		         e->art, e->notes[0], e->notes[1], e->notes[2], e->notes[3], e->notes[4], e->notes[5], e->gate, e->strum, e->string);
		for(const char *p = line; *p; ++p) h = (h ^ (unsigned char)*p) * 0x100000001B3ull;
	}
	return h;
}

static void check_song(const Song *s, u64 seed, Mood m)
{
	CHECK(s->ntracks == CH_COUNT, "seed %llu %s: ntracks %d", (unsigned long long)seed, mood_name(m), s->ntracks);
	CHECK(s->nevents > 0, "seed %llu %s: no events", (unsigned long long)seed, mood_name(m));
	const double d = song_duration(s);
	CHECK(d >= 30.0 && d <= 600.0, "seed %llu %s: duration %.1f s", (unsigned long long)seed, mood_name(m), d);
	for(u32 i = 0; i < s->nevents; ++i) {
		const NoteEvent *e = &s->events[i];
		if(i > 0 && e->tick < s->events[i - 1].tick) {
			CHECK(false, "seed %llu %s: event %u out of order", (unsigned long long)seed, mood_name(m), i);
			break;
		}
		if(e->ch >= s->ntracks) {
			CHECK(false, "seed %llu %s: event %u on track %u", (unsigned long long)seed, mood_name(m), i, e->ch);
			break;
		}
		bool notes_ok = e->count >= 1 && e->count <= 6;
		for(int n = 0; n < 6; ++n) notes_ok = notes_ok && e->notes[n] <= 127;
		if(!notes_ok) {
			CHECK(false, "seed %llu %s: event %u notes out of range", (unsigned long long)seed, mood_name(m), i);
			break;
		}
	}
}

int main(void)
{
	/* determinism and invariants: 50 seeds x every mood */
	for(u64 seed = 1; seed <= 50; ++seed) {
		for(int mi = 0; mi < MOOD_COUNT; ++mi) {
			const Mood m = (Mood)mi;
			Song a, b;
			if(!compose(&a, seed, m) || !compose(&b, seed, m)) {
				CHECK(false, "seed %llu %s: compose failed", (unsigned long long)seed, mood_name(m));
				return 1;
			}
			bool same = a.nevents == b.nevents && a.bpm == b.bpm && a.key == b.key && strcmp(a.description, b.description) == 0;
			for(u32 i = 0; same && i < a.nevents; ++i) same = event_eq(&a.events[i], &b.events[i]);
			CHECK(same, "seed %llu %s: not deterministic", (unsigned long long)seed, mood_name(m));
			check_song(&a, seed, m);
			song_free(&a);
			song_free(&b);
		}
	}

	/* golden values from geosim's C++ composer */
	static const struct {
		u64 seed;
		Mood m;
		u32 nevents;
		u64 hash;
	} golden[] = {
		{7, MOOD_CALM, 2708, 0x5ad431506dcaf6e3ull},
		{42, MOOD_WARTIME, 6886, 0xf80526618fdf8c39ull},
		{123, MOOD_FRUSTRATED, 5641, 0x13fb9888a696c687ull},
	};
	for(size_t i = 0; i < sizeof golden / sizeof golden[0]; ++i) {
		Song s;
		if(!compose(&s, golden[i].seed, golden[i].m)) return 1;
		const u64 h = events_hash(&s);
		CHECK(s.nevents == golden[i].nevents && h == golden[i].hash, "seed %llu %s: %u events, hash %016llx", (unsigned long long)golden[i].seed,
		      mood_name(golden[i].m), s.nevents, (unsigned long long)h);
		song_free(&s);
	}
	{
		Song s;
		if(!compose(&s, 7, MOOD_CALM)) return 1;
		CHECK(s.bpm == 87.0f && s.key == 55 && s.final_key == 55 && s.mode == 3 && s.total_bars == 64, "seed 7 calm basics");
		CHECK(strcmp(s.style, "lo-fi") == 0, "seed 7 calm style %s", s.style);
		CHECK(strcmp(s.description,
		             "seed 7, calm, lo-fi, 87 BPM, G lydian, prog 1-2-5-1 / 1-5-6-III / 1-2-1-2, groove halftime/breakbeat, bass walking/walking, low lead, "
		             "motifs syncopated/legato, swing, 64 bars (177 s): intro8 verse8 chorus8 breakdown8 verse8 drop16 outro8; transitions: kick-out+reverse, "
		             "clap-roll+reverse, stop+reverse+downlifter, pad-swell, roll, cut") == 0,
		      "seed 7 calm description: %s", s.description);
		song_free(&s);
	}
	{
		Song s;
		if(!compose_menu(&s, 1)) return 1;
		bool solo = false;
		for(u32 i = 0; i < s.nsections; ++i) solo = solo || s.sections[i].kind == SEC_ARP_SOLO;
		CHECK(solo, "menu 1: no arp solo");
		CHECK(s.mood == MOOD_WARTIME && s.total_bars == 296 && s.nevents == 15214 && events_hash(&s) == 0x45a1f8b0a8620482ull, "menu 1: %u bars, %u events, hash %016llx",
		      s.total_bars, s.nevents, (unsigned long long)events_hash(&s));
		song_free(&s);
	}
	CHECK(next_seed(0) == 0xE220A8397B1DCDAFull, "next_seed(0) = %016llx", (unsigned long long)next_seed(0));

	if(failures) fprintf(stderr, "test_composer: %d failure(s)\n", failures);
	else printf("test_composer: ok\n");
	return failures ? 1 : 0;
}
