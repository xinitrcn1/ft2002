/* Clicks and real-time headroom.
 * clicks: renders a strummed guitar per model (Folk, 4 bars) and counts
 *   discontinuities: samples whose second difference exceeds 12x the RMS
 *   of the second difference over the surrounding 2048 samples.
 * timing: plays a generated song through the player in 1024-frame blocks
 *   and reports the worst block time against the block's real duration. */
#include "autogen.h"
#include "chords.h"
#include "export.h"
#include "guitar.h"
#include "synth.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int count_clicks(const float *st, u64 n)
{
	float *d = calloc(n, sizeof *d);
	for (u64 i = 2; i < n; i++) {
		float a = 0.5f * (st[2 * i] + st[2 * i + 1]), b = 0.5f * (st[2 * i - 2] + st[2 * i - 1]),
		      c = 0.5f * (st[2 * i - 4] + st[2 * i - 3]);
		d[i] = a - 2 * b + c;
	}
	/* a click is isolated: 10x the RMS of the second difference within
	 * +-5 ms around it (the sample and its 3 neighbours each side excluded) */
	int clicks = 0;
	const u64 R = 240;
	double *cum = calloc(n + 1, sizeof *cum);
	for (u64 i = 0; i < n; i++)
		cum[i + 1] = cum[i] + (double)d[i] * d[i];
	for (u64 i = R; i + R < n; i++) {
		double e = cum[i + R] - cum[i - R] - (cum[i + 4] - cum[i - 3]);
		double rms = sqrt(e / (2 * R - 7)) + 1e-7;
		if (fabs(d[i]) > 10 * rms)
			clicks++, i += 8;
	}
	free(cum);
	free(d);
	return clicks;
}

int main(int argc, char **argv)
{
	static ChordLib lib;
	int total = 0;
	for (int m = 0; m < GTR_COUNT; m++) {
		Song s;
		song_init(&s);
		s.bpm = 110;
		s.ntracks = 1;
		s.patches[0] = gtr_preset((GuitarModel)m);
		s.mix.gain[0] = 1;
		int open[6];
		int strings = gtr_open_strings(&s.patches[0], open);
		chordlib_build(&lib, open, strings);
		const char *prog[4] = {"G", "C", "D", "Em"};
		for (int b = 0; b < 4; b++) {
			int r, q;
			chord_parse(m == GTR_BASS ? "E5" : prog[b], &r, &q);
			u8 nn[6];
			voicing_notes(&lib, chordlib_get(&lib, r, q, 0), nn);
			strum_write(&s, 0, (u32)b * ticks_per_bar, ticks_per_bar, nn, strings,
			            &strum_patterns[strum_find(m == GTR_CRUNCH ? "Rock chug" : "Folk")], 95, ART_NORMAL, false);
		}
		s.total_bars = 4;
		ExportOptions o = export_defaults(EXP_WAV);
		u64 n;
		float *x = render_song(&s, &o, &n);
		int c = count_clicks(x, n);
		printf("%-20s %5d clicks\n", gtr_model_name((GuitarModel)m), c);
		total += c;
		free(x);
		song_free(&s);
	}
	printf("total %d\n", total);
	if (argc > 1) {
		Song s;
		AutogenOptions ao = {.seed = 2327, .mood = MOOD_WARTIME, .band = BAND_ROCK, .key = -1};
		autogen(&s, &ao);
		Player *p = player_new(48000);
		player_set_song(p, &s);
		float buf[2048];
		double worst = 0, sum = 0;
		int blocks = 0;
		while (!p->finished) {
			struct timespec a, b;
			clock_gettime(CLOCK_MONOTONIC, &a);
			player_render(p, buf, 1024);
			clock_gettime(CLOCK_MONOTONIC, &b);
			double ms = (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
			worst = ms > worst ? ms : worst;
			sum += ms;
			blocks++;
		}
		printf("rock 2327: %d blocks of 21.3 ms, mean %.2f ms, worst %.2f ms\n", blocks, sum / blocks, worst);
	}
	return 0;
}
