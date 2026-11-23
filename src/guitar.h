/* Modelled plucked strings: guitars and basses (docs/guitar.md).
 *
 * Each string is an extended Karplus-Strong loop (Jaffe & Smith 1983): a
 * delay line one period long, read with a cubic Lagrange fractional delay
 * (so slides, bends and vibrato move the pitch smoothly), a one-pole loop
 * low-pass whose depth sets the brightness decay, and a loop gain from the
 * wanted decay time. A pluck injects one period of excitation shaped by the
 * pick: a displaced-triangle shape plus noise, low-passed by pick hardness
 * and velocity, comb-filtered at the pick position (the harmonics with a
 * node there are missing). A string rings one note at a time.
 *
 * After the strings: an acoustic body (resonant band-passes) or an
 * electric pickup (comb at its position) and an amp (pre-gain, asymmetric
 * clipping, tone stack, a second stage for the lead model) and a cabinet
 * (band limits and a presence peak).
 *
 * Real-time safe: no allocation after gtr_init, no locks. */
#ifndef FT_GUITAR_H
#define FT_GUITAR_H

#include "song.h"

#define GTR_STRINGS 12          /* 6 courses; strings 6..11 are the 12-string's companions */
#define GTR_LINE 4096           /* delay line, power of two: down to ~12 Hz at 48 kHz */

typedef struct {
	float buf[GTR_LINE];
	u32 pos;
	float period;               /* samples, the loop's total delay at the current pitch */
	float base_period;          /* at the struck note (bends and slides move from it) */
	float lp;                   /* loop low-pass state */
	float lp_coef;              /* 0 bright .. 1 dark */
	float gain;                 /* loop gain per period */
	float gain_to, lp_to;       /* where gain and lp_coef glide (note-off: no step) */
	float comp;                 /* the loop filter's phase delay at the struck pitch */
	float dc_x, dc_y;
	float exc_buf[GTR_LINE];
	u32 exc_len, exc_at;
	u32 exc_wait;               /* samples before the excitation goes in (the pick's contact) */
	/* the pick's contact damps the old vibration over a few ms, not at once:
	 * the line holds values / scale, and scale ramps (folded in at the next pluck) */
	float scale, scale_mul;
	u32 scale_n;
	float amp;                  /* smoothed level (for stopping idle strings) */
	bool active;
	u8 note, art;
	float age, len_s;           /* seconds since the pluck; the event's length */
	float bend;                 /* semitones to reach (bend / slide), over len_s */
	bool held;                  /* before note-off */
	float damp_to;              /* decay time after note-off, seconds */
	/* a strum: this string's pluck waits */
	i32 pending;                /* samples until a pending pluck, -1 none */
	u8 p_note, p_art;
	float p_vel, p_len, p_bend, p_up;
	float bloom;                /* feedback: the octave harmonic swelling in */
	float bloom_ph;
} GtrString;

typedef struct {
	GtrString s[GTR_STRINGS];
	float sr, inv_sr;
	/* body / amp / cabinet state */
	float body_ic1[4], body_ic2[4];
	float pre_hp_ic1, pre_hp_ic2;
	float tone_ic1[3], tone_ic2[3];
	float cab_ic1[4], cab_ic2[4];
	float hand;                 /* the fretting hand's position (fret), for automatic strings */
	u32 noise;
	u32 ctl;                    /* control-rate counter */
	float level;                /* output peak meter */
} GuitarState;

/* Open-string MIDI notes for a patch (tuning, capo, model); returns the count of courses (4 or 6). */
int gtr_open_strings(const Patch *p, int open[6]);
/* The string (0-based, low to high) and fret a single note would use
 * from hand position `hand`, -1 when out of range. */
int gtr_pick_string(const Patch *p, int note, float hand, int *fret);

void gtr_init(GuitarState *g, float sr);
void gtr_reset(GuitarState *g); /* silence */
/* Strike: a strum (FX_STRUM, notes per string) or a single note. vel 0..1;
 * len_s the event length. */
void gtr_trigger(GuitarState *g, const Patch *p, const NoteEvent *e, float vel, float len_s);
/* Note-off for strings started before (not ART_RING ones). */
void gtr_release(GuitarState *g, const Patch *p, bool fast);
/* One mono sample of the instrument (strings + body / amp + cabinet). */
float gtr_render(GuitarState *g, const Patch *p);
bool gtr_idle(const GuitarState *g);

/* Presets: a patch for each model (tone, pick, sustain, gain, body). */
Patch gtr_preset(GuitarModel m);
const char *gtr_model_name(GuitarModel m);

#endif
