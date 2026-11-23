/* The player (synth.h): sequencer, voices, mixer, effects, limiter.
 * Ported from geosim's src/client/audio/synth.cpp; the DSP is the same,
 * statement for statement, with tracks in place of the fixed channels and
 * the workstation's additions (seek, loop, mute / solo, meters,
 * metronome, preview notes, modelled guitars). */
#include "synth.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CHORUS_LEN 2048
#define COMB_COUNT 4
#define ALLPASS_COUNT 2
#define COMB_MAX 3400
#define ALLPASS_MAX 1400
#define LOOKAHEAD 64

static const float pi = 3.14159265358979f;
static const float two_pi = 6.28318530717959f;

static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static float midi_hz(float note) { return 440.0f * exp2f((note - 69.0f) / 12.0f); }

/* PolyBLEP residual for band-limited saw / pulse edges. */
static float blep(float t, float dt)
{
	if (t < dt) {
		t /= dt;
		return t + t - t * t - 1.0f;
	}
	if (t > 1.0f - dt) {
		t = (t - 1.0f) / dt;
		return t * t + t + t + 1.0f;
	}
	return 0.0f;
}

static float white(u32 *s)
{
	*s ^= *s << 13;
	*s ^= *s >> 17;
	*s ^= *s << 5;
	return (float)(i32)*s * (1.0f / 2147483648.0f);
}

/* Topology-preserving state-variable filter (Zavalishin). */
typedef struct {
	float lp, bp, hp;
} SvfOut;
static SvfOut svf(float x, float g, float k, float *ic1, float *ic2)
{
	const float a1 = 1.0f / (1.0f + g * (g + k));
	const float a2 = g * a1, a3 = g * a2;
	const float v3 = x - *ic2;
	const float v1 = a1 * *ic1 + a2 * v3;
	const float v2 = *ic2 + a2 * *ic1 + a3 * v3;
	*ic1 = 2.0f * v1 - *ic1;
	*ic2 = 2.0f * v2 - *ic2;
	return (SvfOut){v2, v1, x - k * v1 - v2};
}

static float svf_g(float hz, float sr) { return tanf(pi * clampf(hz, 20.0f, sr * 0.45f) / sr); }
static float quantize8(float x) { return roundf(clampf(x, -1.0f, 1.0f) * 127.0f) / 127.0f; }

/* TR-808 hat oscillator frequencies. */
static const float metal_hz[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};

static int role(const Player *p, int ch) { return p->song && p->song->role[ch] ? p->song->role[ch] - 1 : -1; }
static int ntracks(const Player *p) { return p->song ? (p->song->ntracks < FT_MAX_TRACKS ? p->song->ntracks : FT_MAX_TRACKS) : 0; }

Player *player_new(float sample_rate)
{
	Player *p = calloc(1, sizeof *p);
	if (!p)
		return NULL;
	p->sr = sample_rate;
	p->inv_sr = 1.0f / sample_rate;
	p->intensity = p->intensity_target = 0.5f;
	p->volume = 0.0f;
	p->volume_target = 0.8f;
	p->bright_mul = 1.0f;
	p->row_seconds = 0.125f;
	p->samples_per_tick = 1000;
	p->duck_t = 10.0f;
	p->sweep_len = 1;
	p->sweep_g = 1;
	p->sweep_k = 1.3f;
	p->lim_hold = p->lim_gain = 1.0f;
	p->click_noise = 0x1234567u;
	p->delay_size = (u32)(sample_rate * 2.0f) + 1;
	p->delay_len = 1;
	p->delay_l = calloc(p->delay_size, sizeof(float));
	p->delay_r = calloc(p->delay_size, sizeof(float));
	p->chorus_buf = calloc((size_t)FT_MAX_TRACKS * CHORUS_LEN, sizeof(float));
	bool ok = p->delay_l && p->delay_r && p->chorus_buf;
	for (int t = 0; t < FT_MAX_TRACKS && ok; t++) {
		p->gtr[t] = malloc(sizeof(GuitarState));
		if (!p->gtr[t])
			ok = false;
		else
			gtr_init(p->gtr[t], sample_rate);
	}
	if (!ok) {
		player_free(p);
		return NULL;
	}
	for (int t = 0; t < FT_MAX_TRACKS; t++)
		for (int v = 0; v < VOICES_PER_TRACK; v++)
			p->voices[t][v].noise = 22222u;
	/* Freeverb tunings at 44.1 kHz, scaled; right channel slightly longer. */
	static const int comb_t[COMB_COUNT] = {1116, 1188, 1277, 1356};
	static const int ap_t[ALLPASS_COUNT] = {556, 441};
	const float scale = fminf(sample_rate, 96000.0f) / 44100.0f;
	for (int side = 0; side < 2; ++side) {
		for (int i = 0; i < COMB_COUNT; ++i) {
			int l = (int)((float)(comb_t[i] + side * 23) * scale);
			p->combs[side * COMB_COUNT + i].len = l < COMB_MAX ? l : COMB_MAX;
		}
		for (int i = 0; i < ALLPASS_COUNT; ++i) {
			int l = (int)((float)(ap_t[i] + side * 23) * scale);
			p->allpasses[side * ALLPASS_COUNT + i].len = l < ALLPASS_MAX ? l : ALLPASS_MAX;
		}
	}
	/* "Sample-like" single-cycle waves, 8-bit like Amiga chip samples. */
	for (int i = 0; i < wavetable_len; ++i) {
		const float x = two_pi * (float)i / wavetable_len;
		const float organ = sinf(x) + 0.5f * sinf(2 * x) + 0.25f * sinf(4 * x);
		float bass = 0;
		for (int h = 1; h <= 6; ++h)
			bass += sinf((float)h * x) / (float)h * (h % 2 ? 1.0f : 0.6f);
		const float vox = sinf(x) + 0.2f * sinf(2 * x) + 0.6f * sinf(3 * x) + 0.3f * sinf(5 * x);
		const float pluck = (sinf(x) >= 0 ? 0.6f : -0.6f) + 0.4f * sinf(2 * x);
		p->tables[0][i] = organ / 1.6f;
		p->tables[1][i] = bass / 1.9f;
		p->tables[2][i] = vox / 1.8f;
		p->tables[3][i] = roundf(pluck * 31.0f) / 31.0f; /* 6-bit */
	}
	for (int t = 0; t < 3; ++t)
		for (int i = 0; i < wavetable_len; ++i)
			p->tables[t][i] = quantize8(p->tables[t][i]);
	return p;
}

void player_free(Player *p)
{
	if (!p)
		return;
	for (int t = 0; t < FT_MAX_TRACKS; t++)
		free(p->gtr[t]);
	free(p->delay_l);
	free(p->delay_r);
	free(p->chorus_buf);
	free(p);
}

static bool layer_on(const Player *p, u8 layer)
{
	switch (layer) {
	case LAYER_MIN_MID:
		return p->intensity >= layer_mid;
	case LAYER_MIN_HIGH:
		return p->intensity >= layer_high;
	case LAYER_MAX_MID:
		return p->intensity < layer_mid;
	case LAYER_MAX_HIGH:
		return p->intensity < layer_high;
	default:
		return true;
	}
}

static void release_channel(Player *p, int ch, u32 before_tick, bool fast, float min_release)
{
	const Patch *pa = &p->song->patches[ch];
	if (pa->inst == INST_GUITAR) {
		gtr_release(p->gtr[ch], pa, fast);
		return;
	}
	for (int i = 0; i < VOICES_PER_TRACK; i++) {
		Voice *v = &p->voices[ch][i];
		if (!v->active || (!v->preview && v->start_tick >= before_tick))
			continue;
		v->gate = false;
		if (v->stage != 2 || fast) {
			v->stage = 2;
			float t = fast ? 0.006f : fmaxf(fmaxf(0.005f, pa->release), min_release);
			v->release_coef = expf(-4.6f * p->inv_sr / t);
		}
	}
}

static Voice *allocate(Player *p, int ch)
{
	Voice *pool = p->voices[ch];
	Voice *best = &pool[0];
	for (int i = 0; i < VOICES_PER_TRACK; i++) {
		Voice *v = &pool[i];
		if (!v->active)
			return v;
		if (v->age > best->age)
			best = v;
	}
	return best; /* steal the oldest */
}

/* the event index of the first event at or after tick */
static u32 lower_bound(const Song *s, u32 tick)
{
	u32 lo = 0, hi = s->nevents;
	while (lo < hi) {
		u32 mid = lo + (hi - lo) / 2;
		if (s->events[mid].tick < tick)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

static void jump_to_outro(Player *p)
{
	p->outro_requested = false;
	const Song *s = p->song;
	if (!s)
		return;
	for (u32 i = 0; i < s->nsections; ++i) {
		if (s->sections[i].kind != SEC_OUTRO)
			continue;
		if ((int)i <= p->section)
			return; /* already there */
		p->tick = s->sections[i].start_bar * ticks_per_bar;
		p->section = (int)i;
		p->next_event = lower_bound(s, p->tick);
		for (int c = 0; c < ntracks(p); c++)
			release_channel(p, c, ~0u, false, 0.0f);
		return;
	}
}

static void trigger(Player *p, const NoteEvent *e, bool preview);

static void process_tick(Player *p)
{
	const Song *s = p->song;
	if (!s || !p->running)
		return;
	if (p->tick % ticks_per_bar == 0) {
		const u32 bar = p->tick / ticks_per_bar;
		/* track the section; honour a pending outro request at an 8-bar boundary */
		while (p->section + 1 < (int)s->nsections && s->sections[p->section + 1].start_bar <= bar)
			++p->section;
		if (p->outro_requested && bar % 8 == 0 && bar > 0 && s->nsections) {
			const SectionKind k = s->sections[p->section].kind;
			if (k != SEC_INTRO && k != SEC_OUTRO)
				jump_to_outro(p);
			else if (k == SEC_OUTRO)
				p->outro_requested = false;
		}
	}
	if (p->loop && p->loop_end > p->loop_start && p->tick >= p->loop_end) {
		player_seek(p, p->loop_start);
		return;
	}
	if (p->tick >= song_total_ticks(s)) {
		p->finished = true;
		return;
	}
	if (p->metronome && p->tick % (ticks_per_row * rows_per_beat) == 0) {
		p->click_t = 0;
		p->click_hz = p->tick % ticks_per_bar == 0 ? 1760.0f : 1320.0f;
	}
	while (p->next_event < s->nevents && s->events[p->next_event].tick <= p->tick) {
		const NoteEvent *e = &s->events[p->next_event++];
		if (e->tick == p->tick && e->ch < ntracks(p) && layer_on(p, e->layer))
			trigger(p, e, false);
	}
	/* tracker-style arpeggio: next chord note every tick */
	for (int c = 0; c < FT_MAX_TRACKS; c++)
		for (int i = 0; i < VOICES_PER_TRACK; i++) {
			Voice *v = &p->voices[c][i];
			if (v->active && v->fx == FX_ARP && v->count > 1)
				v->arp_index = (u8)((v->arp_index + 1) % v->count);
		}
}

static void update_control(Player *p, Voice *v);

static void trigger(Player *p, const NoteEvent *e, bool preview)
{
	const Patch *pa = &p->song->patches[e->ch];
	const float vel = powf((float)e->vel / 127.0f, 1.4f);
	const float len_s = (float)((double)e->len * p->samples_per_tick) * p->inv_sr;
	if (pa->inst == INST_GUITAR) {
		gtr_trigger(p->gtr[e->ch], pa, e, vel, len_s);
		return;
	}
	if (pa->inst == INST_KIT && (e->notes[0] == DR_LP_CLOSE || e->notes[0] == DR_LP_OPEN)) {
		p->sweep_dir = e->notes[0] == DR_LP_CLOSE ? -1 : 1;
		p->sweep_t = 0.0f;
		p->sweep_len = fmaxf(0.05f, len_s);
		if (p->sweep_dir > 0)
			for (int c = 0; c < FT_MAX_TRACKS; c++)
				p->sweep_ic1[c] = p->sweep_ic2[c] = 0.0f;
		return;
	}
	if (pa->inst == INST_KIT) {
		/* Each note is its own drum voice; mono kits choke earlier hits
		 * (closed hat cuts the open hat), hits on the same tick layer. */
		if (pa->mono)
			release_channel(p, e->ch, p->tick, true, 0.0f);
		for (int i = 0; i < e->count && i < 6; i++) {
			Voice *v = allocate(p, e->ch);
			*v = (Voice){0};
			v->active = v->gate = true;
			v->preview = preview;
			v->ch = e->ch;
			v->drum = e->notes[i];
			v->start_tick = p->tick;
			v->vel = vel;
			v->glide_from = 1.0f;
			v->fenv = 1.0f;
			v->freq_mul = 1.0f;
			v->ratio2 = 1.0f;
			v->k = 1.0f;
			v->duty = 0.5f;
			v->gate_amp = 1.0f;
			v->attack = 0.005f;
			v->sustain = 0.7f;
			v->count = 1;
			v->len_s = len_s;
			v->gate_left = (double)e->len * p->samples_per_tick;
			v->noise = 0x9E3779B9u ^ (p->tick * 2654435761u) ^ ((u32)e->notes[i] << 8);
			if (v->drum == DR_KICK) {
				p->duck_t = 0.0f;
				++p->kicks;
			}
			for (int m = 0; m < 6; ++m)
				v->metal[m] = (float)m * 0.17f;
			float hz = 1000.0f;
			switch (v->drum) {
			case DR_SNARE:
				hz = 1800.0f;
				break;
			case DR_CLAP:
				hz = 1150.0f;
				break;
			case DR_HAT_CLOSED:
			case DR_HAT_OPEN:
				hz = 7000.0f;
				break;
			case DR_CRASH:
				hz = 3500.0f;
				break;
			case DR_SHAKER:
				hz = 5500.0f;
				break;
			case DR_IMPACT:
				hz = 900.0f;
				break;
			default:
				break;
			}
			v->g = svf_g(hz, p->sr);
		}
		return;
	}
	/* Tonal: one (paraphonic) voice per event. Mono channels cut the
	 * previous note; with glide the new note slides from the old pitch. */
	float from_ratio = 1.0f;
	if (pa->mono) {
		const Voice *prev = NULL;
		for (int i = 0; i < VOICES_PER_TRACK; i++) {
			const Voice *v = &p->voices[e->ch][i];
			if (v->active && v->gate && (v->start_tick < p->tick || v->preview) && (!prev || v->age < prev->age))
				prev = v;
		}
		if (prev && pa->glide > 0.0f)
			from_ratio = prev->freq[0] * prev->freq_mul / fmaxf(1.0f, midi_hz(e->notes[0]));
		release_channel(p, e->ch, p->tick, pa->release < 0.05f || role(p, e->ch) != CH_PAD, 0.0f);
	}
	Voice *v = allocate(p, e->ch);
	const u32 seed = v->noise;
	*v = (Voice){0};
	v->noise = seed * 1664525u + 1013904223u;
	v->active = v->gate = true;
	v->preview = preview;
	v->ch = e->ch;
	v->start_tick = p->tick;
	v->fx = e->fx;
	v->count = e->count < 1 ? 1 : e->count > 4 ? 4 : e->count;
	for (int i = 0; i < v->count; i++) {
		v->freq[i] = midi_hz(e->notes[i]);
		v->phase1[i] = 0.0f;
		v->phase2[i] = 0.25f * (float)i;
		for (int u = 0; u < 4; u++)
			v->phase_u[i][u] = 0.13f + 0.21f * (float)u + 0.07f * (float)i; /* fixed: renders stay deterministic */
	}
	v->glide_from = from_ratio;
	v->vel = vel;
	v->fenv = 1.0f;
	v->freq_mul = 1.0f;
	v->k = 1.0f;
	v->attack = pa->attack;
	v->sustain = pa->sustain;
	float decay = pa->decay;
	v->len_s = len_s;
	if (e->fx == FX_STAB) {
		v->attack = 0.002f;
		v->sustain = 0.0f;
		decay = 0.16f;
	} else if (e->fx == FX_SWELL) {
		v->attack = fmaxf(0.05f, v->len_s * 0.95f);
		v->sustain = 1.0f;
	} else if (e->fx == FX_SLIDE_UP && e->count >= 2) {
		v->bend = (float)((int)e->notes[1] - (int)e->notes[0]);
		v->count = 1;
	}
	v->gate_mask = e->gate;
	v->gate_amp = e->gate ? 0.0f : 1.0f;
	v->gate_left = (double)e->len * p->samples_per_tick;
	v->duty = pa->duty;
	v->dcoef = expf(-4.6f * p->inv_sr / fmaxf(0.005f, decay));
	v->fcoef = expf(-4.6f * p->inv_sr / fmaxf(0.005f, pa->fdecay));
	v->ratio2 = exp2f((pa->semi2 + pa->detune * 0.01f) / 12.0f);
	update_control(p, v);
}

/* Control-rate update (every 16 samples): pitch modulation, pulse width,
 * filter coefficients. */
static void update_control(Player *p, Voice *v)
{
	const Patch *pa = &p->song->patches[v->ch];
	if (pa->inst != INST_SYNTH)
		return;
	float semis = 0.0f;
	if (pa->vib_depth > 0.0f && v->age > pa->vib_delay) {
		const float ramp = fminf(1.0f, (v->age - pa->vib_delay) / 0.25f);
		semis += pa->vib_depth * ramp * sinf(two_pi * pa->vib_rate * v->age);
	}
	if (v->bend != 0.0f && v->len_s > 0.0f) {
		const float u = fminf(1.0f, v->age / v->len_s);
		semis += v->bend * u * u;
	}
	float glide = 1.0f;
	if (v->glide_from != 1.0f && pa->glide > 0.0f) {
		const float u = fminf(1.0f, v->age / pa->glide);
		glide = exp2f(log2f(v->glide_from) * (1.0f - u));
		if (u >= 1.0f)
			v->glide_from = 1.0f;
	}
	v->freq_mul = exp2f(semis / 12.0f) * glide;
	v->duty = clampf(pa->duty + pa->pwm_depth * sinf(two_pi * pa->pwm_rate * v->age), 0.05f, 0.95f);
	const float f0 = v->freq[0] * v->freq_mul;
	float cutoff = pa->cutoff * exp2f(pa->fenv * v->fenv) * powf(f0 / 261.6f, pa->key_track) * (0.6f + 0.4f * v->vel);
	if (p->song->mix.bright[v->ch])
		cutoff *= p->bright_mul;
	v->g = svf_g(cutoff, p->sr);
	v->k = 2.0f - 1.9f * clampf(pa->resonance, 0.0f, 0.98f);
}

static float osc(const Player *p, Voice *v, Wave w, float *ph, float dt, int table)
{
	float y = 0.0f;
	switch (w) {
	case WAVE_SAW:
		y = 2.0f * *ph - 1.0f - blep(*ph, dt);
		break;
	case WAVE_PULSE: {
		y = *ph < v->duty ? 1.0f : -1.0f;
		y += blep(*ph, dt);
		float t2 = *ph - v->duty;
		if (t2 < 0)
			t2 += 1.0f;
		y -= blep(t2, dt);
		break;
	}
	case WAVE_TRIANGLE:
		y = 4.0f * fabsf(*ph - 0.5f) - 1.0f;
		break;
	case WAVE_SINE:
		y = sinf(two_pi * *ph);
		break;
	case WAVE_NOISE:
		y = white(&v->noise);
		break;
	case WAVE_TABLE: {
		const float pos = *ph * wavetable_len;
		const int i0 = (int)pos & (wavetable_len - 1);
		const float fr = pos - floorf(pos);
		y = p->tables[table][i0] * (1.0f - fr) + p->tables[table][(i0 + 1) & (wavetable_len - 1)] * fr;
		break;
	}
	}
	*ph += dt;
	if (*ph >= 1.0f)
		*ph -= 1.0f;
	return y;
}

static float render_tonal(Player *p, Voice *v, const Patch *pa)
{
	float sum = 0.0f;
	const int first = v->fx == FX_ARP ? v->arp_index : 0;
	const int last = v->fx == FX_ARP ? v->arp_index + 1 : v->count;
	for (int i = first; i < last; ++i) {
		const float f1 = v->freq[i] * v->freq_mul;
		const float dt1 = f1 * p->inv_sr;
		const float f2 = f1 * v->ratio2;
		const float dt2 = f2 * p->inv_sr;
		float s = osc(p, v, pa->w1, &v->phase1[i], dt1, pa->table1);
		if (pa->unison > 0 && pa->w1 == WAVE_SAW) {
			/* supersaw: extra band-limited saws spread symmetrically over +-spread cents */
			const int n = pa->unison < 4 ? pa->unison : 4;
			for (int u = 0; u < n; ++u) {
				const float pos = n == 1 ? 0.5f : -1.0f + 2.0f * (float)u / (float)(n - 1);
				const float du = dt1 * exp2f(pos * pa->spread * (1.0f / 1200.0f));
				s += 0.8f * osc(p, v, WAVE_SAW, &v->phase_u[i][u], du, 0);
			}
			s *= 1.0f / (1.0f + 0.8f * (float)n * 0.55f);
		}
		if (pa->mix2 > 0.0f)
			s += pa->mix2 * osc(p, v, pa->w2, &v->phase2[i], dt2, pa->table2);
		if (pa->sub > 0.0f) {
			s += pa->sub * sinf(two_pi * v->phase_sub[i]);
			v->phase_sub[i] += dt1 * 0.5f;
			if (v->phase_sub[i] >= 1.0f)
				v->phase_sub[i] -= 1.0f;
		}
		sum += s;
	}
	if (v->fx != FX_ARP && v->count > 1)
		sum *= 1.0f / sqrtf((float)v->count);
	sum *= 1.0f / (1.0f + pa->mix2 + pa->sub);
	if (pa->drive > 0.0f)
		sum = tanhf(sum * (1.0f + 3.0f * pa->drive));
	if (pa->crush_rate > 0.0f) { /* Paula-style: sample-and-hold at a low rate, 8-bit */
		v->crush_phase += pa->crush_rate * p->inv_sr;
		if (v->crush_phase >= 1.0f) {
			v->crush_phase -= 1.0f;
			v->crush_hold = quantize8(sum);
		}
		sum = v->crush_hold;
	}
	if (pa->formant) {
		/* a choir: three band-passes (Q ~ 3.5) at the vowel's formants,
		 * plus some of the dry voice for body under the first formant */
		float f = 0.3f * sum;
		for (int i = 0; i < 3; ++i)
			f += p->form_a[v->ch][i] * svf(sum, p->form_g[v->ch][i], 0.28f, &v->fic1[i], &v->fic2[i]).bp;
		sum = f;
	}
	/* 24 dB: a first stage without resonance, the resonant one after it */
	const float y = pa->poles4 ? svf(svf(sum, v->g, 1.41f, &v->ic1b, &v->ic2b).lp, v->g, v->k, &v->ic1, &v->ic2).lp
	                           : svf(sum, v->g, v->k, &v->ic1, &v->ic2).lp;

	switch (v->stage) {
	case 0:
		v->env += p->inv_sr / fmaxf(0.001f, v->attack);
		if (v->env >= 1.0f)
			v->env = 1.0f, v->stage = 1;
		break;
	case 1:
		v->env = v->sustain + (v->env - v->sustain) * v->dcoef;
		break;
	default:
		v->env *= v->release_coef;
		break;
	}
	float gate_gain = 1.0f;
	if (v->gate_mask) {
		/* trance gate: open for the first 70 % of each enabled 16th,
		 * ~1.5 ms attack and ~5 ms release so the chops do not click */
		const float rows = v->age / p->row_seconds;
		const int row = (int)rows & 15;
		const bool open = ((v->gate_mask >> row) & 1u) && (rows - floorf(rows)) < 0.7f;
		v->gate_amp += ((open ? 1.0f : 0.0f) - v->gate_amp) * (open ? 0.015f : 0.0045f);
		gate_gain = v->gate_amp;
	}
	v->fenv *= v->fcoef;
	if (v->gate && !v->preview) {
		v->gate_left -= 1.0;
		if (v->gate_left <= 0) {
			v->gate = false;
			v->stage = 2;
			v->release_coef = expf(-4.6f * p->inv_sr / fmaxf(0.005f, pa->release));
		}
	}
	if (v->stage == 2 && v->env < 1e-4f)
		v->active = false;
	return y * v->env * v->vel * pa->gain * gate_gain;
}

static float ex(float t, float decay) { return expf(-4.6f * t / decay); }

static float render_drum(Player *p, Voice *v, const Patch *pa, bool control)
{
	const float t = v->age;
	const float inv_sr = p->inv_sr;
	float y = 0.0f;
	bool done = false;
	switch (v->drum) {
	case DR_KICK: {
		const float f = pa->kick_end + (pa->kick_pitch - pa->kick_end) * expf(-t / pa->kick_sweep);
		v->dphase += f * inv_sr;
		if (v->dphase >= 1.0f)
			v->dphase -= 1.0f;
		const float body = sinf(two_pi * v->dphase) * ex(t, pa->kick_decay);
		const float click = white(&v->noise) * expf(-t / 0.0012f) * pa->kick_click;
		y = tanhf(pa->kick_drive * (body + 0.5f * click));
		done = t > pa->kick_decay * 1.2f;
		break;
	}
	case DR_SNARE: {
		v->dphase += pa->snare_tone * inv_sr;
		if (v->dphase >= 1.0f)
			v->dphase -= 1.0f;
		const float tone = (sinf(two_pi * v->dphase) * 0.6f + sinf(two_pi * v->dphase * 1.62f) * 0.3f) * ex(t, 0.07f);
		const float n = svf(white(&v->noise), v->g, 1.2f, &v->ic1, &v->ic2).hp * ex(t, pa->snare_decay);
		y = 0.9f * (tone + 0.8f * n);
		done = t > pa->snare_decay * 1.3f;
		break;
	}
	case DR_CLAP: {
		float env;
		if (t < 0.027f)
			env = expf(-fmodf(t, 0.009f) / 0.0028f);
		else
			env = ex(t - 0.027f, 0.16f);
		y = 1.6f * svf(white(&v->noise), v->g, 0.9f, &v->ic1, &v->ic2).bp * env;
		done = t > 0.22f;
		break;
	}
	case DR_HAT_CLOSED:
	case DR_HAT_OPEN:
	case DR_CRASH: {
		float metal = 0.0f;
		for (int m = 0; m < 6; ++m) {
			v->metal[m] += metal_hz[m] * pa->hat_tone * (v->drum == DR_CRASH ? 1.45f : 1.0f) * inv_sr;
			if (v->metal[m] >= 1.0f)
				v->metal[m] -= 1.0f;
			metal += v->metal[m] < 0.5f ? 1.0f : -1.0f;
		}
		const float src = metal * 0.12f + white(&v->noise) * 0.5f;
		const float hp = svf(svf(src, v->g, 1.0f, &v->ic1, &v->ic2).hp, svf_g(11000.0f, p->sr), 1.2f, &v->ic1b, &v->ic2b).lp;
		const float decay = v->drum == DR_HAT_CLOSED ? pa->hat_decay : v->drum == DR_HAT_OPEN ? 0.32f : 1.7f;
		const float attack = v->drum == DR_CRASH ? fminf(1.0f, t / 0.004f) : 1.0f;
		y = hp * ex(t, decay) * attack * (v->drum == DR_CRASH ? 0.8f : 1.0f);
		done = t > decay * 1.1f;
		break;
	}
	case DR_RIM: {
		v->dphase += 1700.0f * inv_sr;
		if (v->dphase >= 1.0f)
			v->dphase -= 1.0f;
		y = (sinf(two_pi * v->dphase) * 0.7f + white(&v->noise) * 0.3f) * ex(t, 0.025f);
		done = t > 0.03f;
		break;
	}
	case DR_SHAKER: {
		const float hp = svf(white(&v->noise), v->g, 1.2f, &v->ic1, &v->ic2).hp;
		y = hp * fminf(1.0f, t / 0.006f) * ex(t, 0.06f);
		done = t > 0.07f;
		break;
	}
	case DR_TOM_LOW:
	case DR_TOM_MID:
	case DR_TOM_HIGH: {
		const float base = v->drum == DR_TOM_LOW ? 95.0f : v->drum == DR_TOM_MID ? 125.0f : 165.0f;
		const float f = base * (0.75f + 0.25f * expf(-t / 0.08f));
		v->dphase += f * inv_sr;
		if (v->dphase >= 1.0f)
			v->dphase -= 1.0f;
		y = sinf(two_pi * v->dphase) * ex(t, 0.35f) + white(&v->noise) * 0.1f * ex(t, 0.02f);
		done = t > 0.4f;
		break;
	}
	case DR_DOWNLIFTER: {
		/* band-passed noise falling 4 octaves with a falling tone, fading out */
		const float len = fmaxf(0.1f, v->len_s);
		const float u = fminf(1.0f, t / len);
		if (control)
			v->g = svf_g(6000.0f * exp2f(-4.0f * u), p->sr);
		float s = svf(white(&v->noise), v->g, 0.7f, &v->ic1, &v->ic2).bp;
		v->dphase += (880.0f * exp2f(-3.0f * u)) * inv_sr;
		if (v->dphase >= 1.0f)
			v->dphase -= 1.0f;
		s += 0.12f * sinf(two_pi * v->dphase) * (1.0f - u);
		const float amp = fminf(1.0f, t / 0.01f) * (1.0f - u) * (1.0f - u);
		y = s * amp * 1.1f;
		done = t > len;
		break;
	}
	case DR_RISER:
	case DR_SWELL: {
		const float len = fmaxf(0.1f, v->len_s);
		const float u = fminf(1.0f, t / len);
		const bool riser = v->drum == DR_RISER;
		const float centre = riser ? 300.0f * exp2f(u * 5.0f) : 2500.0f + 4000.0f * u;
		const float n = white(&v->noise);
		if (control)
			v->g = svf_g(centre, p->sr);
		float s = riser ? svf(n, v->g, 0.6f, &v->ic1, &v->ic2).bp : svf(n, v->g, 1.0f, &v->ic1, &v->ic2).hp;
		if (riser) { /* plus a rising tone */
			v->dphase += (110.0f * exp2f(u * 3.0f)) * inv_sr;
			if (v->dphase >= 1.0f)
				v->dphase -= 1.0f;
			s += 0.15f * sinf(two_pi * v->dphase) * u;
		}
		const float amp = riser ? u * u : u * u * u;
		const float tail = t > len ? ex(t - len, 0.03f) : 1.0f;
		y = s * amp * tail * (riser ? 1.2f : 0.9f);
		done = t > len + 0.05f;
		break;
	}
	case DR_IMPACT: {
		const float f = 32.0f + 30.0f * expf(-t / 0.15f);
		v->dphase += f * inv_sr;
		if (v->dphase >= 1.0f)
			v->dphase -= 1.0f;
		const float boom = sinf(two_pi * v->dphase) * ex(t, 1.4f);
		const float n = svf(white(&v->noise), v->g, 1.0f, &v->ic1, &v->ic2).lp * ex(t, 0.25f);
		y = 0.8f * boom + 0.6f * n;
		done = t > 1.5f;
		break;
	}
	default:
		done = true;
		break;
	}
	/* A choked drum fades over a few ms instead of clicking off. */
	if (v->stage == 2) {
		v->env = v->env <= 0.0f ? 1.0f : v->env;
		v->env *= v->release_coef;
		y *= v->env;
		if (v->env < 1e-4f)
			done = true;
	}
	if (done)
		v->active = false;
	return y * v->vel * pa->gain;
}

/* ---- transport ------------------------------------------------------------- */

/* derived per-track state from the song's patches and mix (cheap; every block) */
static void derive(Player *p)
{
	const Song *s = p->song;
	if (!s)
		return;
	/* vowel formants (Hz, level): "aah" and "ooh", fixed whatever the note, which is what makes a voice */
	static const float vowels[2][3][2] = {{{750.0f, 1.0f}, {1150.0f, 0.6f}, {2600.0f, 0.22f}},
	                                      {{350.0f, 1.0f}, {800.0f, 0.45f}, {2400.0f, 0.1f}}};
	for (int c = 0; c < FT_MAX_TRACKS; c++) {
		if (s->mix.highpass[c] != p->hp_hz[c]) {
			p->hp_hz[c] = s->mix.highpass[c];
			p->hp_g[c] = s->mix.highpass[c] > 0.0f ? svf_g(s->mix.highpass[c], p->sr) : 0.0f;
		}
		const int vw = s->patches[c].formant > 2 ? 0 : s->patches[c].formant;
		if (vw != p->form_vowel[c] || (vw && p->form_g[c][0] == 0.0f)) {
			p->form_vowel[c] = (u8)vw;
			for (int i = 0; i < 3; ++i) {
				p->form_g[c][i] = vw ? svf_g(vowels[vw - 1][i][0], p->sr) : 0.0f;
				p->form_a[c][i] = vw ? vowels[vw - 1][i][1] : 0.0f;
			}
		}
		const float a = (clampf(s->mix.pan[c], -1.0f, 1.0f) + 1.0f) * pi * 0.25f;
		p->pan_l[c] = cosf(a);
		p->pan_r[c] = sinf(a);
	}
	p->samples_per_tick = song_seconds_per_tick(s) * (double)p->sr;
	u32 dl = (u32)((double)s->mix.delay_rows * ticks_per_row * p->samples_per_tick);
	p->delay_len = dl < 1 ? 1 : dl > p->delay_size - 1 ? p->delay_size - 1 : dl;
}

void player_attach(Player *p, const Song *s)
{
	p->song = s;
	p->running = false;
	p->finished = false;
	p->sweep_dir = 0;
	p->next_event = 0;
	p->tick = 0;
	p->section = 0;
	p->outro_requested = false;
	p->to_next_tick = 0;
	p->frames = 0;
	if (!s)
		return;
	for (int c = 0; c < FT_MAX_TRACKS; c++)
		p->hp_hz[c] = -1.0f, p->form_vowel[c] = 0, p->form_g[c][0] = 0.0f;
	derive(p);
	p->to_next_tick = p->samples_per_tick;
}

void player_set_song(Player *p, const Song *s)
{
	/* A song that played to its end lets its last chord and note ring out
	 * (the final cadence decays instead of being cut by the patch release). */
	const bool natural_end = p->song && p->finished && !s;
	if (natural_end) {
		for (int c = 0; c < ntracks(p); c++) {
			int r = role(p, c);
			if (r == CH_BASS || r == CH_LEAD || r == CH_PAD || r == CH_COUNTER)
				release_channel(p, c, ~0u, false, 1.4f);
		}
	} else if (p->song) {
		for (int c = 0; c < ntracks(p); c++)
			release_channel(p, c, ~0u, false, 0.0f);
	}
	if (!s) {
		p->song = NULL;
		p->running = false;
		p->finished = false;
		return;
	}
	player_attach(p, s);
	p->running = true;
	process_tick(p); /* events at tick 0 */
	p->to_next_tick = p->samples_per_tick;
}

void player_seek(Player *p, u32 tick)
{
	if (!p->song)
		return;
	for (int c = 0; c < ntracks(p); c++)
		release_channel(p, c, ~0u, false, 0.0f);
	p->tick = tick;
	p->finished = false;
	p->next_event = lower_bound(p->song, tick);
	p->section = 0;
	p->frames = 0;
	p->to_next_tick = p->samples_per_tick;
	if (p->running) {
		/* the bar logic runs on bar lines; set the section here */
		const u32 bar = tick / ticks_per_bar;
		while (p->section + 1 < (int)p->song->nsections && p->song->sections[p->section + 1].start_bar <= bar)
			++p->section;
		process_tick(p);
	}
}

void player_set_running(Player *p, bool on)
{
	if (!p->song)
		return;
	if (on && !p->running) {
		p->running = true;
		if (p->finished)
			player_seek(p, 0);
		else
			player_seek(p, p->tick);
	} else if (!on && p->running) {
		p->running = false;
		for (int c = 0; c < ntracks(p); c++)
			release_channel(p, c, ~0u, false, 0.0f);
	}
}

void player_set_intensity(Player *p, float target) { p->intensity_target = target; }
void player_set_intensity_now(Player *p, float v) { p->intensity = p->intensity_target = v; }
void player_set_volume(Player *p, float v) { p->volume_target = v; }
void player_request_outro(Player *p) { p->outro_requested = true; }

void player_hard_stop(Player *p)
{
	p->song = NULL;
	p->running = false;
	p->finished = false;
	p->sweep_dir = 0;
	for (int c = 0; c < FT_MAX_TRACKS; c++) {
		for (int i = 0; i < VOICES_PER_TRACK; i++)
			p->voices[c][i].active = false;
		gtr_reset(p->gtr[c]);
	}
	memset(p->delay_l, 0, p->delay_size * sizeof(float));
	memset(p->delay_r, 0, p->delay_size * sizeof(float));
	memset(p->chorus_buf, 0, (size_t)FT_MAX_TRACKS * CHORUS_LEN * sizeof(float));
	for (int i = 0; i < 8; i++)
		memset(p->combs[i].buf, 0, sizeof p->combs[i].buf), p->combs[i].store = 0.0f;
	for (int i = 0; i < 4; i++)
		memset(p->allpasses[i].buf, 0, sizeof p->allpasses[i].buf);
	p->delay_lp_l = p->delay_lp_r = 0.0f;
}

void player_preview(Player *p, const NoteEvent *e)
{
	if (p->preview_n < PREVIEW_QUEUE)
		p->preview_q[p->preview_n++] = *e;
}

void player_preview_release(Player *p) { p->preview_release = true; }

static void run_previews(Player *p)
{
	if (!p->song) {
		p->preview_n = 0;
		p->preview_release = false;
		return;
	}
	if (p->preview_release) {
		p->preview_release = false;
		for (int c = 0; c < ntracks(p); c++) {
			const Patch *pa = &p->song->patches[c];
			if (pa->inst == INST_GUITAR) {
				if (!p->running)
					gtr_release(p->gtr[c], pa, false);
				continue;
			}
			for (int i = 0; i < VOICES_PER_TRACK; i++) {
				Voice *v = &p->voices[c][i];
				if (v->active && v->preview && v->gate) {
					v->gate = false;
					v->stage = 2;
					v->release_coef = expf(-4.6f * p->inv_sr / fmaxf(0.005f, pa->release));
				}
			}
		}
	}
	for (u32 i = 0; i < p->preview_n; i++)
		if (p->preview_q[i].ch < ntracks(p))
			trigger(p, &p->preview_q[i], true);
	p->preview_n = 0;
}

/* ---- rendering -------------------------------------------------------------- */

void player_render(Player *p, float *out, int frames)
{
	derive(p);
	run_previews(p);
	/* block-rate smoothing of intensity and volume */
	const float block_s = (float)frames * p->inv_sr;
	p->intensity += (p->intensity_target - p->intensity) * (1.0f - expf(-block_s / 6.0f));
	p->volume += (p->volume_target - p->volume) * (1.0f - expf(-block_s / 0.15f));
	p->bright_mul = exp2f((p->intensity - 0.55f) * 1.6f);
	p->row_seconds = (float)(p->samples_per_tick * ticks_per_row) * p->inv_sr;

	const Song *song = p->song;
	const int nt = ntracks(p);
	const MixSettings *mix = song ? &song->mix : NULL;
	const float fb = mix ? mix->delay_feedback : 0.0f;
	const float room = 0.7f + 0.28f * clampf(mix ? mix->reverb_size : 0.8f, 0.0f, 1.0f);
	const float damp = 0.3f, dly_damp = 0.35f;
	u32 audible = 0;
	for (int c = 0; c < nt; c++)
		if (p->solo ? (p->solo >> c) & 1u : !((p->mute >> c) & 1u))
			audible |= 1u << c;
	float peak_decay = expf(-block_s / 0.3f);
	for (int c = 0; c < FT_MAX_TRACKS; c++)
		p->peak[c] *= peak_decay;
	p->peak_l *= peak_decay;
	p->peak_r *= peak_decay;

	for (int f = 0; f < frames; ++f) {
		if (song && p->running) {
			p->to_next_tick -= 1.0;
			if (p->to_next_tick <= 0) {
				p->to_next_tick += p->samples_per_tick;
				++p->tick;
				process_tick(p);
			}
			p->frames++;
		}
		const bool control = (p->control_counter++ & 15) == 0;
		if (p->sweep_dir != 0) {
			/* closing: 16 kHz -> 280 Hz, then open again at the end; opening:
			 * 280 Hz -> 16 kHz (exponential, slow at first), then bypassed */
			p->sweep_t += p->inv_sr;
			const float u = fminf(1.0f, p->sweep_t / p->sweep_len);
			if (control) {
				const float x = p->sweep_dir < 0 ? u * u * (3.0f - 2.0f * u) : 1.0f - u * u;
				p->sweep_g = svf_g(16000.0f * exp2f(-5.84f * x), p->sr);
			}
			if (u >= 1.0f)
				p->sweep_dir = 0;
		}
		float l = 0, r = 0, dsend = 0, rsend = 0;
		/* sidechain: a short dip after each kick */
		const float duck_env = p->duck_t < 0.004f ? p->duck_t / 0.004f : expf(-(p->duck_t - 0.004f) / 0.09f);
		p->duck_t += p->inv_sr;
		for (int c = 0; c < nt; ++c) {
			const Patch *pa = &song->patches[c];
			float s = 0.0f;
			if (pa->inst == INST_GUITAR) {
				if (!gtr_idle(p->gtr[c]))
					s = gtr_render(p->gtr[c], pa);
			} else {
				for (int i = 0; i < VOICES_PER_TRACK; i++) {
					Voice *v = &p->voices[c][i];
					if (!v->active)
						continue;
					if (pa->inst == INST_KIT)
						s += render_drum(p, v, pa, control || v->age == 0.0f);
					else {
						if (control)
							update_control(p, v);
						s += render_tonal(p, v, pa);
					}
					v->age += p->inv_sr;
				}
			}
			const int rl = role(p, c);
			if (p->sweep_dir != 0 && rl != CH_KICK && rl != CH_FX)
				s = svf(s, p->sweep_g, p->sweep_k, &p->sweep_ic1[c], &p->sweep_ic2[c]).lp;
			if (p->hp_g[c] > 0.0f)
				s = svf(s, p->hp_g[c], 1.41f, &p->hp_ic1[c], &p->hp_ic2[c]).hp;
			if (!((audible >> c) & 1u))
				s = 0.0f;
			if (s == 0.0f && mix->chorus[c] <= 0.0f)
				continue; /* a chorus keeps reading its tail */
			s *= mix->gain[c] * (1.0f - mix->duck[c] * duck_env);
			float as = fabsf(s);
			if (as > p->peak[c])
				p->peak[c] = as;
			if (mix->chorus[c] > 0.0f) {
				/* ensemble: two taps 7-13 ms back, swept in quadrature at 0.6 Hz, one per side */
				float *buf = p->chorus_buf + (ptrdiff_t)c * CHORUS_LEN;
				buf[p->chorus_pos] = s;
				const float wet = mix->chorus[c];
				float taps[2];
				for (int side = 0; side < 2; side++) {
					float ms = 10.0f + 3.0f * (side ? cosf(two_pi * p->chorus_phase) : sinf(two_pi * p->chorus_phase));
					const float d = ms * 0.001f * p->sr;
					float rp = (float)p->chorus_pos - d;
					if (rp < 0)
						rp += CHORUS_LEN;
					const int i0 = (int)rp;
					const float fr = rp - (float)i0;
					taps[side] = buf[i0 % CHORUS_LEN] * (1.0f - fr) + buf[(i0 + 1) % CHORUS_LEN] * fr;
				}
				l += (s * (1.0f - 0.5f * wet) + wet * taps[0]) * p->pan_l[c];
				r += (s * (1.0f - 0.5f * wet) + wet * taps[1]) * p->pan_r[c];
			} else {
				l += s * p->pan_l[c];
				r += s * p->pan_r[c];
			}
			dsend += s * mix->delay[c];
			rsend += s * mix->reverb[c];
		}

		p->chorus_pos = (p->chorus_pos + 1) % CHORUS_LEN;
		p->chorus_phase += 0.6f * p->inv_sr;
		if (p->chorus_phase >= 1.0f)
			p->chorus_phase -= 1.0f;
		/* ping-pong delay with a damped feedback path */
		{
			const u32 n = p->delay_size;
			const u32 rp = (p->delay_pos + n - p->delay_len) % n;
			const float dl = p->delay_l[rp], dr = p->delay_r[rp];
			p->delay_lp_l += (dl - p->delay_lp_l) * (1.0f - dly_damp);
			p->delay_lp_r += (dr - p->delay_lp_r) * (1.0f - dly_damp);
			p->delay_l[p->delay_pos] = dsend + fb * p->delay_lp_r;
			p->delay_r[p->delay_pos] = fb * p->delay_lp_l;
			p->delay_pos = (p->delay_pos + 1) % n;
			l += dl * 0.8f;
			r += dr * 0.8f;
		}
		/* Schroeder/Freeverb-style reverb */
		{
			const float in = rsend * 0.06f;
			float outs[2] = {0, 0};
			for (int side = 0; side < 2; ++side) {
				float acc = 0;
				for (int i = 0; i < COMB_COUNT; ++i) {
					typeof(p->combs[0]) *cb = &p->combs[side * COMB_COUNT + i];
					const float o = cb->buf[cb->pos];
					cb->store = o * (1.0f - damp) + cb->store * damp;
					cb->buf[cb->pos] = in + cb->store * room;
					if (++cb->pos >= cb->len)
						cb->pos = 0;
					acc += o;
				}
				for (int i = 0; i < ALLPASS_COUNT; ++i) {
					typeof(p->allpasses[0]) *ap = &p->allpasses[side * ALLPASS_COUNT + i];
					const float b = ap->buf[ap->pos];
					ap->buf[ap->pos] = acc + b * 0.5f;
					acc = b - acc;
					if (++ap->pos >= ap->len)
						ap->pos = 0;
				}
				outs[side] = acc;
			}
			l += outs[0];
			r += outs[1];
		}
		/* the metronome, after the effects */
		if (p->metronome && p->click_t < 0.03f) {
			float c = sinf(two_pi * p->click_hz * p->click_t) * expf(-p->click_t / 0.006f) * 0.35f;
			l += c;
			r += c;
			p->click_t += p->inv_sr;
		}

		/* DC blocker, master volume */
		p->dc_y_l = l - p->dc_x_l + 0.9995f * p->dc_y_l;
		p->dc_x_l = l;
		p->dc_y_r = r - p->dc_x_r + 0.9995f * p->dc_y_r;
		p->dc_x_r = r;
		l = p->dc_y_l * p->volume;
		r = p->dc_y_r * p->volume;
		if (!isfinite(l) || !isfinite(r))
			l = r = 0.0f; /* never propagate NaN/inf to the device */

		/* look-ahead peak limiter, ceiling -1 dBFS */
		const float ceiling = 0.891f;
		const float peak = fmaxf(fabsf(l), fabsf(r));
		const float req = peak > ceiling ? ceiling / peak : 1.0f;
		if (req <= p->lim_hold) {
			p->lim_hold = req;
			p->lim_hold_count = LOOKAHEAD;
		} else if (p->lim_hold_count > 0) {
			--p->lim_hold_count;
		} else {
			p->lim_hold += (1.0f - p->lim_hold) * 0.0004f; /* ~50 ms release */
			p->lim_hold = fminf(p->lim_hold, 1.0f);
		}
		if (p->lim_hold < p->lim_gain)
			p->lim_gain += (p->lim_hold - p->lim_gain) * (3.0f / LOOKAHEAD);
		else
			p->lim_gain = p->lim_hold;
		const int li = p->la_pos * 2;
		const float ol = p->la_buf[li] * p->lim_gain, orr = p->la_buf[li + 1] * p->lim_gain;
		p->la_buf[li] = l;
		p->la_buf[li + 1] = r;
		p->la_pos = (p->la_pos + 1) % LOOKAHEAD;
		/* soft clip above 0.9 (bounded below 1.0) */
		float o2[2] = {ol, orr};
		for (int k = 0; k < 2; k++) {
			const float a = fabsf(o2[k]);
			if (a > 0.9f) {
				const float y = 0.9f + 0.0999f * tanhf((a - 0.9f) / 0.0999f);
				o2[k] = o2[k] < 0 ? -y : y;
			}
		}
		out[2 * f] = o2[0];
		out[2 * f + 1] = o2[1];
		if (fabsf(o2[0]) > p->peak_l)
			p->peak_l = fabsf(o2[0]);
		if (fabsf(o2[1]) > p->peak_r)
			p->peak_r = fabsf(o2[1]);
	}
}
