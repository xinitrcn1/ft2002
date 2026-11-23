/* Parity dump of the C23 port: the same canonical text form as dump.cpp.
 *   cc -std=c23 -O2 -Isrc src/song.c src/composer.c work/parity/dump.c -lm
 * usage: dump [first_seed last_seed [menu_last]]  (default 1 300 20) */
#include "composer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fnv(u64 *h, const char *s)
{
	for(; *s; ++s) *h = (*h ^ (unsigned char)*s) * 0x100000001B3ull;
}

static void dump(const char *head, const Song *s)
{
	printf("%s\n", head);
	printf("bpm %a key %d final_key %d mode %d total_bars %u seed %llu mood %s\n", (double)s->bpm, s->key, s->final_key, s->mode, s->total_bars, (unsigned long long)s->seed,
	       mood_name(s->mood));
	printf("style %s\n", s->style);
	printf("desc %s\n", s->description);
	printf("tracks %d\n", s->ntracks);
	for(int c = 0; c < s->ntracks; ++c) {
		const Patch *p = &s->patches[c];
		printf("track %d %s\n", c, s->track_name[c]);
		printf("patch %d %d %d %d %d %d %d %a %a %a %a %a %a %a %d %a %d %d %a %a %a %a %a %a %a %a %a %a %a %a %a %a %a %a", c, (int)p->inst, p->mono ? 1 : 0, (int)p->w1,
		       (int)p->w2, p->table1, p->table2, (double)p->mix2, (double)p->detune, (double)p->semi2, (double)p->duty, (double)p->pwm_depth, (double)p->pwm_rate,
		       (double)p->sub, p->unison, (double)p->spread, p->poles4 ? 1 : 0, p->formant, (double)p->attack, (double)p->decay, (double)p->sustain, (double)p->release,
		       (double)p->cutoff, (double)p->resonance, (double)p->fenv, (double)p->fdecay, (double)p->key_track, (double)p->glide, (double)p->vib_depth,
		       (double)p->vib_rate, (double)p->vib_delay, (double)p->crush_rate, (double)p->drive, (double)p->gain);
		printf(" %a %a %a %a %a %a %a %a %a %a", (double)p->kick_pitch, (double)p->kick_end, (double)p->kick_decay, (double)p->kick_click, (double)p->kick_sweep,
		       (double)p->kick_drive, (double)p->snare_tone, (double)p->snare_decay, (double)p->hat_decay, (double)p->hat_tone);
		printf(" %d %a %a %a %a %a %d %d %d %d %d %d %d\n", (int)p->gtr, (double)p->gtr_tone, (double)p->gtr_pick, (double)p->gtr_sustain, (double)p->gtr_gain,
		       (double)p->gtr_body, p->gtr_tuning[0], p->gtr_tuning[1], p->gtr_tuning[2], p->gtr_tuning[3], p->gtr_tuning[4], p->gtr_tuning[5], p->gtr_capo);
		const MixSettings *x = &s->mix;
		printf("mix %d %a %a %a %a %a %d %a %a\n", c, (double)x->gain[c], (double)x->pan[c], (double)x->delay[c], (double)x->reverb[c], (double)x->duck[c],
		       x->bright[c] ? 1 : 0, (double)x->chorus[c], (double)x->highpass[c]);
	}
	printf("mixg %a %a %a\n", (double)s->mix.delay_rows, (double)s->mix.delay_feedback, (double)s->mix.reverb_size);
	printf("sections %u\n", s->nsections);
	for(u32 i = 0; i < s->nsections; ++i) printf("sec %d %u %u\n", (int)s->sections[i].kind, s->sections[i].start_bar, s->sections[i].bars);
	u64 h = 0xCBF29CE484222325ull;
	char line[256];
	char *evs = nullptr;
	size_t evlen = 0;
	FILE *mem = open_memstream(&evs, &evlen);
	for(u32 i = 0; i < s->nevents; ++i) {
		const NoteEvent *e = &s->events[i];
		snprintf(line, sizeof line, "ev %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %d %u\n", e->tick, e->len, e->ch, e->vel, e->layer, (unsigned)e->fx, e->count,
		         e->art, e->notes[0], e->notes[1], e->notes[2], e->notes[3], e->notes[4], e->notes[5], e->gate, e->strum, e->string);
		fnv(&h, line);
		fputs(line, mem);
	}
	fclose(mem);
	printf("events %u %016llx\n", s->nevents, (unsigned long long)h);
	fputs(evs, stdout);
	free(evs);
}

int main(int argc, char **argv)
{
	unsigned long long first = 1, last = 300, menu_last = 20;
	if(argc >= 3) first = strtoull(argv[1], nullptr, 0), last = strtoull(argv[2], nullptr, 0);
	if(argc >= 4) menu_last = strtoull(argv[3], nullptr, 0);
	char head[128];
	Song s;
	for(unsigned long long seed = first; seed <= last; ++seed) {
		for(int m = 0; m < MOOD_COUNT; ++m) {
			snprintf(head, sizeof head, "song %llu %s", seed, mood_name((Mood)m));
			if(!compose(&s, seed, (Mood)m)) {
				fprintf(stderr, "compose failed\n");
				return 1;
			}
			dump(head, &s);
			song_free(&s);
		}
	}
	for(unsigned long long seed = 1; seed <= menu_last; ++seed) {
		snprintf(head, sizeof head, "menu %llu", seed);
		if(!compose_menu(&s, seed)) {
			fprintf(stderr, "compose_menu failed\n");
			return 1;
		}
		dump(head, &s);
		song_free(&s);
	}
	return 0;
}
