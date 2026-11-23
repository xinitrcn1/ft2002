/* the player's block time while sounds die away (denormals): a strum and a
 * pad chord, then 30 s of tails; with and without flush-to-zero */
#include "chords.h"
#include "presets.h"
#include "synth.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <xmmintrin.h>
int main(int argc, char **argv)
{
	if (argc > 1)
		_mm_setcsr(_mm_getcsr() | 0x8040); /* FTZ | DAZ */
	Song s;
	song_init(&s);
	s.ntracks = 3;
	s.patches[0] = preset_patch(preset_find("Steel acoustic"));
	s.patches[1] = preset_patch(preset_find("Warm pad"));
	s.patches[2] = preset_patch(preset_find("Drum kit"));
	for (int t = 0; t < 3; t++)
		s.mix.gain[t] = 0.7f, s.mix.reverb[t] = 0.4f, s.mix.delay[t] = 0.2f, s.mix.chorus[t] = 0.3f;
	NoteEvent e = note_event();
	e.fx = FX_STRUM, e.count = 6, e.strum = 9, e.len = 96;
	u8 nn[6] = {43, 47, 50, 55, 59, 67};
	memcpy(e.notes, nn, 6);
	song_add(&s, e);
	NoteEvent p = note_event();
	p.ch = 1, p.count = 3, p.len = 96, p.notes[0] = 55, p.notes[1] = 59, p.notes[2] = 62;
	song_add(&s, p);
	NoteEvent k = note_event();
	k.ch = 2, k.notes[0] = DR_CRASH;
	song_add(&s, k);
	s.total_bars = 1;
	Player *pl = player_new(48000);
	player_set_song(pl, &s);
	float buf[2048];
	double worst[4] = {0}, sum[4] = {0};
	int nb[4] = {0};
	for (int b = 0; b < 48000 * 32 / 1024; b++) {
		struct timespec t0, t1;
		clock_gettime(CLOCK_MONOTONIC, &t0);
		player_render(pl, buf, 1024);
		clock_gettime(CLOCK_MONOTONIC, &t1);
		double ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
		int q = b * 1024 / 48000 / 8; /* 8-second quarters */
		q = q > 3 ? 3 : q;
		worst[q] = ms > worst[q] ? ms : worst[q];
		sum[q] += ms;
		nb[q]++;
	}
	for (int q = 0; q < 4; q++)
		printf("%2d-%2d s: mean %.3f ms, worst %.3f ms (of 21.3)\n", q * 8, q * 8 + 8, sum[q] / nb[q], worst[q]);
	return 0;
}
