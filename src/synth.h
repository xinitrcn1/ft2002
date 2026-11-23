/* The player: sequencer + voices + mixer (ported from geosim's synth.hpp,
 * docs/14-audio.md#synthesis there; docs/audio.md here).
 *
 * Runs on the audio thread. player_render never allocates, does no I/O;
 * every buffer is made by player_new. The song is read while it plays (the
 * caller locks the audio stream around edits, see audio.c), so patch and
 * mix edits are heard at once.
 *
 * Signal path per track: voices (2 oscillators + sub, a drum model, or the
 * modelled strings of guitar.h) -> optional sample-and-hold -> SVF low-pass
 * -> amp envelope; the track sum is gain/pan/sidechain-ducked into the
 * stereo bus and sent to a tempo-synced ping-pong delay and a Schroeder
 * reverb; the bus goes through a DC blocker, a look-ahead peak limiter
 * (-1 dBFS) and a soft clip.
 *
 * Additions over geosim for a workstation: seek, a loop range, mute and
 * solo, per-track meters, a metronome, and notes played from the UI
 * (player_preview) on top of the song. */
#ifndef FT_SYNTH_H
#define FT_SYNTH_H

#include "song.h"
#include "guitar.h"

#define VOICES_PER_TRACK 6
#define PREVIEW_QUEUE 64

typedef struct {
	bool active, gate;
	u8 ch, count, drum, arp_index;
	NoteFx fx;
	bool preview;            /* started by player_preview, not the song */
	u32 start_tick;
	float freq[4];
	float phase1[4], phase2[4], phase_sub[4];
	float phase_u[4][4];
	float fic1[3], fic2[3];
	float glide_from;
	float vel;
	float env, fenv;
	int stage;
	float release_coef;
	double gate_left;
	float age;
	float len_s;
	float ic1, ic2;
	float g, k;
	float ic1b, ic2b;
	float freq_mul, duty;
	float dcoef, fcoef, ratio2;
	float crush_phase, crush_hold;
	float attack, sustain;
	float bend;
	u16 gate_mask;
	float gate_amp;
	float dphase;
	float metal[6];
	u32 noise;
} Voice;

typedef struct {
	float sr, inv_sr;
	const Song *song;
	bool running;            /* the sequencer advances (transport playing) */
	bool finished;
	u32 next_event;
	u32 tick;
	double samples_per_tick;
	double to_next_tick;
	int section;
	bool outro_requested;
	u32 control_counter;
	u64 kicks;
	u64 frames;              /* rendered since the last seek */

	float intensity, intensity_target;
	float volume, volume_target;
	float bright_mul;
	float row_seconds;

	u32 loop_start, loop_end; /* ticks; loop when loop_end > loop_start */
	bool loop;
	u32 mute, solo;           /* track bit masks */
	bool metronome;
	float click_t, click_hz;  /* the metronome's click */
	u32 click_noise;

	float peak[FT_MAX_TRACKS]; /* per-track meters (decaying peak) */
	float peak_l, peak_r;

	Voice voices[FT_MAX_TRACKS][VOICES_PER_TRACK];
	GuitarState *gtr[FT_MAX_TRACKS];
	float pan_l[FT_MAX_TRACKS], pan_r[FT_MAX_TRACKS];
	float duck_t;

	int sweep_dir;
	float sweep_t, sweep_len, sweep_g, sweep_k;
	float sweep_ic1[FT_MAX_TRACKS], sweep_ic2[FT_MAX_TRACKS];
	float hp_g[FT_MAX_TRACKS], hp_ic1[FT_MAX_TRACKS], hp_ic2[FT_MAX_TRACKS];
	float hp_hz[FT_MAX_TRACKS];
	float form_g[FT_MAX_TRACKS][3], form_a[FT_MAX_TRACKS][3];
	u8 form_vowel[FT_MAX_TRACKS];

	float *chorus_buf; /* FT_MAX_TRACKS * CHORUS_LEN */
	int chorus_pos;
	float chorus_phase;
	float *delay_l, *delay_r;
	u32 delay_pos, delay_len, delay_size;
	float delay_lp_l, delay_lp_r;
	struct {
		float buf[3400];
		int len, pos;
		float store;
	} combs[8];
	struct {
		float buf[1400];
		int len, pos;
	} allpasses[4];
	float dc_x_l, dc_y_l, dc_x_r, dc_y_r;
	float la_buf[64 * 2];
	int la_pos;
	float lim_hold, lim_gain;
	int lim_hold_count;
	float tables[4][32];

	/* notes from the UI (single producer: the UI thread under the stream lock) */
	NoteEvent preview_q[PREVIEW_QUEUE];
	u32 preview_n;
	bool preview_release;     /* release every preview note */
} Player;

Player *player_new(float sample_rate);
void player_free(Player *p);
/* Starts s from its first tick, running (NULL: stop; voices ring out). */
void player_set_song(Player *p, const Song *s);
/* Attach s without starting the sequencer (the editor's song). */
void player_attach(Player *p, const Song *s);
void player_seek(Player *p, u32 tick);
void player_set_running(Player *p, bool on);
void player_set_intensity(Player *p, float target);
void player_set_intensity_now(Player *p, float v);
void player_set_volume(Player *p, float v);
void player_request_outro(Player *p);
void player_hard_stop(Player *p);
/* Play a note or chord on a track now (the UI: chord palette, keyboard). */
void player_preview(Player *p, const NoteEvent *e);
void player_preview_release(Player *p);
/* Interleaved stereo float frames. */
void player_render(Player *p, float *out, int frames);
static inline bool player_finished(const Player *p) { return p->finished; }

#endif
