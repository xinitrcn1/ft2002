/* where the clicks are: a model, Folk G for a bar; prints click times (ms)
 * and the strum / note-off times */
#include "chords.h"
#include "export.h"
#include "guitar.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
	static ChordLib lib;
	int m = argc > 1 ? atoi(argv[1]) : GTR_NYLON;
	Song s;
	song_init(&s);
	s.bpm = 110;
	s.ntracks = 1;
	s.patches[0] = gtr_preset((GuitarModel)m);
	s.mix.gain[0] = 1;
	s.mix.reverb[0] = 0;
	int open[6];
	int strings = gtr_open_strings(&s.patches[0], open);
	chordlib_build(&lib, open, strings);
	int r, q;
	chord_parse("G", &r, &q);
	u8 nn[6];
	voicing_notes(&lib, chordlib_get(&lib, r, q, 0), nn);
	strum_write(&s, 0, 0, ticks_per_bar, nn, strings, &strum_patterns[strum_find("Folk")], 95, ART_NORMAL, false);
	s.total_bars = 1;
	double spt = song_seconds_per_tick(&s) * 1000;
	for (u32 i = 0; i < s.nevents; i++)
		printf("ev %7.1f ms .. %7.1f ms strum %d\n", s.events[i].tick * spt, (s.events[i].tick + s.events[i].len) * spt,
		       s.events[i].strum);
	ExportOptions o = export_defaults(EXP_WAV);
	o.tail_s = 0.5f;
	u64 n;
	float *st = render_song(&s, &o, &n);
	float *x = malloc(n * sizeof *x), *d = calloc(n, sizeof *d);
	for (u64 i = 0; i < n; i++)
		x[i] = 0.5f * (st[2 * i] + st[2 * i + 1]);
	for (u64 i = 2; i < n; i++)
		d[i] = x[i] - 2 * x[i - 1] + x[i - 2];
	const u64 R = 240;
	double *cum = calloc(n + 1, sizeof *cum);
	for (u64 i = 0; i < n; i++)
		cum[i + 1] = cum[i] + (double)d[i] * d[i];
	for (u64 i = R; i + R < n; i++) {
		double e = cum[i + R] - cum[i - R] - (cum[i + 4] - cum[i - 3]);
		double rms = sqrt(e / (2 * R - 7)) + 1e-7;
		if (fabs(d[i]) > 10 * rms) {
			printf("click %7.2f ms  d2 %.4f (%.0fx rms)  x %.4f\n", i / 48.0, d[i], fabs(d[i]) / rms, x[i]);
			i += 8;
		}
	}
	return 0;
}
