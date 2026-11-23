/* The modelled strings: in tune (autocorrelation pitch within 3 cents),
 * decaying, finite and below full scale for every model and articulation;
 * strums spread over the strings; deterministic renders. */
#include "chords.h"
#include "export.h"
#include "guitar.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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

static void one_track(Song *s, GuitarModel m)
{
	song_init(s);
	s->bpm = 120;
	s->ntracks = 1;
	s->patches[0] = gtr_preset(m);
	s->mix.gain[0] = 1.0f;
	s->total_bars = 2;
	snprintf(s->track_name[0], sizeof s->track_name[0], "%s", gtr_model_name(m));
}

/* the strongest period by normalised autocorrelation, refined by parabola */
static double pitch_hz(const float *x, u64 n, int sr, double lo_hz, double hi_hz)
{
	int min_lag = (int)(sr / hi_hz), max_lag = (int)(sr / lo_hz);
	double best = -1;
	int best_lag = 0;
	static double r[4096];
	for (int lag = min_lag; lag <= max_lag + 1 && lag < 4096; lag++) {
		double a = 0, e0 = 0, e1 = 0;
		for (u64 i = 0; i + (u64)lag < n; i++) {
			a += (double)x[i] * x[i + (u64)lag];
			e0 += (double)x[i] * x[i];
			e1 += (double)x[i + (u64)lag] * x[i + (u64)lag];
		}
		r[lag] = a / sqrt(e0 * e1 + 1e-20);
		if (lag <= max_lag && r[lag] > best)
			best = r[lag], best_lag = lag;
	}
	if (best_lag <= min_lag)
		return sr / (double)best_lag;
	double y0 = r[best_lag - 1], y1 = r[best_lag], y2 = r[best_lag + 1];
	double d = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2);
	return sr / (best_lag + d);
}

static float *render_mono(const Song *s, u64 *frames)
{
	ExportOptions o = export_defaults(EXP_WAV);
	o.tail_s = 1.0f;
	float *st = render_song(s, &o, frames);
	if (!st)
		return NULL;
	for (u64 i = 0; i < *frames; i++)
		st[i] = 0.5f * (st[2 * i] + st[2 * i + 1]);
	return st;
}

int main(void)
{
	/* pitch: single notes on a clean electric and a steel acoustic */
	const int notes[] = {40, 45, 52, 57, 64, 69, 76};
	for (int mi = 0; mi < 2; mi++) {
		GuitarModel m = mi ? GTR_STEEL : GTR_CLEAN;
		for (size_t k = 0; k < sizeof notes / sizeof notes[0]; k++) {
			Song s;
			one_track(&s, m);
			s.patches[0].gtr_gain = 0; /* clean: no intermodulation */
			NoteEvent e = note_event();
			e.len = ticks_per_bar;
			e.notes[0] = (u8)notes[k];
			song_add(&s, e);
			u64 n;
			float *x = render_mono(&s, &n);
			double want = 440.0 * pow(2.0, (notes[k] - 69) / 12.0);
			/* 0.3 s in, 0.2 s of signal */
			double got = pitch_hz(x + 14400, 9600, 48000, want / 1.5, want * 1.5);
			double cents = 1200.0 * log2(got / want);
			CHECK(fabs(cents) < 3.0, "%s note %d: %.2f Hz, want %.2f (%.1f cents)", gtr_model_name(m), notes[k], got,
			      want, cents);
			/* it decays: the second half second is quieter than the first */
			double e1 = 0, e2 = 0;
			for (u64 i = 4800; i < 28800; i++)
				e1 += x[i] * x[i];
			for (u64 i = 28800; i < 52800 && i < n; i++)
				e2 += x[i] * x[i];
			CHECK(e2 < e1 && e1 > 1e-6, "%s note %d: no decay (%g, %g)", gtr_model_name(m), notes[k], e1, e2);
			free(x);
			song_free(&s);
		}
	}

	/* every model x articulation, strummed: finite, under full scale, audible */
	static ChordLib lib;
	for (int m = 0; m < GTR_COUNT; m++) {
		Song s;
		one_track(&s, (GuitarModel)m);
		int open[6];
		int strings = gtr_open_strings(&s.patches[0], open);
		chordlib_build(&lib, open, strings);
		int r, q;
		chord_parse(m == GTR_BASS ? "E5" : "G", &r, &q);
		u8 nn[6];
		voicing_notes(&lib, chordlib_get(&lib, r, q, 0), nn);
		for (int a = 0; a < ART_COUNT; a++) {
			NoteEvent e = note_event();
			e.tick = (u32)a * 12;
			e.len = 12;
			e.fx = FX_STRUM;
			e.count = 6;
			e.art = (u8)a;
			e.strum = a % 2 ? -8 : 8;
			e.vel = 110;
			memcpy(e.notes, nn, 6);
			if (a == ART_SLIDE || a == ART_BEND) {
				e.fx = FX_NONE;
				e.count = 2;
				e.notes[0] = (u8)(open[2] + 5);
				e.notes[1] = (u8)(open[2] + 7);
			}
			song_add(&s, e);
		}
		s.total_bars = 2;
		ExportOptions o = export_defaults(EXP_WAV);
		u64 n;
		float *x = render_song(&s, &o, &n);
		double sum = 0;
		float pk = 0;
		bool finite = true;
		for (u64 i = 0; i < n * 2; i++) {
			finite &= isfinite(x[i]);
			sum += (double)x[i] * x[i];
			pk = fabsf(x[i]) > pk ? fabsf(x[i]) : pk;
		}
		double rms_db = 10 * log10(sum / (double)(n * 2) + 1e-20);
		printf("%-20s peak %6.2f dBFS  rms %6.2f dBFS\n", gtr_model_name((GuitarModel)m), 20 * log10(pk + 1e-20), rms_db);
		CHECK(finite, "%s: non-finite samples", gtr_model_name((GuitarModel)m));
		CHECK(pk < 1.0f, "%s: peak %g", gtr_model_name((GuitarModel)m), pk);
		CHECK(rms_db > -40, "%s: too quiet (%.1f dB)", gtr_model_name((GuitarModel)m), rms_db);
		/* determinism */
		u64 n2;
		float *y = render_song(&s, &o, &n2);
		CHECK(n2 == n && !memcmp(x, y, n * 2 * sizeof(float)), "%s: renders differ", gtr_model_name((GuitarModel)m));
		free(x);
		free(y);
		song_free(&s);
	}

	/* a strum spreads its strings: onsets of a down strum at 20 ms a string */
	{
		Song s;
		one_track(&s, GTR_CLEAN);
		NoteEvent e = note_event();
		e.fx = FX_STRUM;
		e.count = 6;
		e.strum = 20;
		e.len = ticks_per_bar;
		u8 nn[6] = {43, 47, 50, 55, 59, 67};
		memcpy(e.notes, nn, 6);
		song_add(&s, e);
		u64 n;
		float *x = render_mono(&s, &n);
		/* energy in the first 15 ms vs 90-105 ms: more strings sound later */
		double a = 0, b = 0;
		for (int i = 0; i < 720; i++)
			a += x[i] * x[i];
		for (int i = 4320; i < 5040; i++)
			b += x[i] * x[i];
		CHECK(b > a * 1.5, "strum does not build: %g -> %g", a, b);
		free(x);
		song_free(&s);
	}
	if (fails)
		printf("%d failures\n", fails);
	return fails != 0;
}
