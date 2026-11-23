/* Modelled plucked strings (guitar.h, docs/guitar.md). */
#include "guitar.h"

#include <math.h>
#include <string.h>

static const float PI = 3.14159265358979f;

static float midi_hz(float n) { return 440.0f * exp2f((n - 69.0f) / 12.0f); }

static float white(u32 *s)
{
	*s ^= *s << 13;
	*s ^= *s >> 17;
	*s ^= *s << 5;
	return (float)(i32)*s * (1.0f / 2147483648.0f);
}

/* TPT state-variable filter (Zavalishin), as in synth.c */
typedef struct {
	float lp, bp, hp;
} Svf;
static Svf svf(float x, float g, float k, float *ic1, float *ic2)
{
	const float a1 = 1.0f / (1.0f + g * (g + k));
	const float a2 = g * a1, a3 = g * a2;
	const float v3 = x - *ic2;
	const float v1 = a1 * *ic1 + a2 * v3;
	const float v2 = *ic2 + a2 * *ic1 + a3 * v3;
	*ic1 = 2.0f * v1 - *ic1;
	*ic2 = 2.0f * v2 - *ic2;
	return (Svf){v2, v1, x - k * v1 - v2};
}
static float svf_g(float hz, float sr)
{
	float f = hz < 20.0f ? 20.0f : hz > sr * 0.45f ? sr * 0.45f : hz;
	return tanf(PI * f / sr);
}

/* ---- models ------------------------------------------------------------------ */

typedef struct {
	const char *name;
	int strings;          /* courses */
	i8 tuning[6];
	float t60;            /* decay at 110 Hz, seconds (sustain 1) */
	float noise;          /* excitation noise share */
	float pickup;         /* electric pickup position (0 = acoustic body) */
	float amp;            /* amp make-up after clipping */
	bool body;
} ModelDef;

static const ModelDef models[GTR_COUNT] = {
    [GTR_STEEL] = {"Steel acoustic", 6, {40, 45, 50, 55, 59, 64}, 7.0f, 0.35f, 0.0f, 1.6f, true},
    [GTR_NYLON] = {"Nylon classical", 6, {40, 45, 50, 55, 59, 64}, 4.5f, 0.15f, 0.0f, 1.8f, true},
    [GTR_CLEAN] = {"Electric clean", 6, {40, 45, 50, 55, 59, 64}, 6.0f, 0.25f, 0.22f, 1.5f, false},
    [GTR_CRUNCH] = {"Electric crunch", 6, {40, 45, 50, 55, 59, 64}, 6.0f, 0.25f, 0.12f, 0.95f, false},
    [GTR_LEAD] = {"Electric lead", 6, {40, 45, 50, 55, 59, 64}, 8.0f, 0.25f, 0.12f, 0.8f, false},
    [GTR_BASS] = {"Electric bass", 4, {28, 33, 38, 43, 0, 0}, 5.0f, 0.15f, 0.2f, 1.6f, false},
    [GTR_12STRING] = {"12-string acoustic", 6, {40, 45, 50, 55, 59, 64}, 7.0f, 0.35f, 0.0f, 1.2f, true},
};

const char *gtr_model_name(GuitarModel m) { return m < GTR_COUNT ? models[m].name : "?"; }

Patch gtr_preset(GuitarModel m)
{
	Patch p = patch_default();
	p.inst = INST_GUITAR;
	p.mono = false;
	p.gtr = m;
	p.gain = 1.0f;
	switch (m) {
	case GTR_STEEL:
		p.gtr_tone = 0.6f, p.gtr_pick = 0.18f, p.gtr_sustain = 1.0f, p.gtr_gain = 0.0f, p.gtr_body = 0.6f;
		break;
	case GTR_NYLON:
		p.gtr_tone = 0.3f, p.gtr_pick = 0.25f, p.gtr_sustain = 1.0f, p.gtr_gain = 0.0f, p.gtr_body = 0.7f;
		break;
	case GTR_CLEAN:
		p.gtr_tone = 0.55f, p.gtr_pick = 0.15f, p.gtr_sustain = 1.0f, p.gtr_gain = 0.08f, p.gtr_body = 0.5f;
		break;
	case GTR_CRUNCH:
		p.gtr_tone = 0.5f, p.gtr_pick = 0.12f, p.gtr_sustain = 1.2f, p.gtr_gain = 0.45f, p.gtr_body = 0.6f;
		break;
	case GTR_LEAD:
		p.gtr_tone = 0.55f, p.gtr_pick = 0.12f, p.gtr_sustain = 2.0f, p.gtr_gain = 0.8f, p.gtr_body = 0.6f;
		break;
	case GTR_BASS:
		p.gtr_tone = 0.35f, p.gtr_pick = 0.2f, p.gtr_sustain = 1.0f, p.gtr_gain = 0.1f, p.gtr_body = 0.5f;
		break;
	case GTR_12STRING:
		p.gtr_tone = 0.65f, p.gtr_pick = 0.18f, p.gtr_sustain = 1.0f, p.gtr_gain = 0.0f, p.gtr_body = 0.6f;
		break;
	default:
		break;
	}
	return p;
}

int gtr_open_strings(const Patch *p, int open[6])
{
	const ModelDef *m = &models[p->gtr < GTR_COUNT ? p->gtr : 0];
	for (int i = 0; i < 6; i++) {
		int t = p->gtr_tuning[i] ? p->gtr_tuning[i] : m->tuning[i];
		open[i] = t + p->gtr_capo;
	}
	return m->strings;
}

int gtr_pick_string(const Patch *p, int note, float hand, int *fret)
{
	int open[6];
	int n = gtr_open_strings(p, open);
	int best = -1;
	float best_d = 1e9f;
	for (int s = 0; s < n; s++) {
		int f = note - open[s];
		if (f < 0 || f > 20)
			continue;
		/* stay near the hand; open strings are cheap; among equals, the thinner string */
		float d = f == 0 ? fabsf(hand - 2.0f) * 0.6f : fabsf((float)f - hand);
		d -= s * 0.01f;
		if (d < best_d)
			best_d = d, best = s;
	}
	if (best >= 0 && fret)
		*fret = note - open[best];
	return best;
}

/* ---- strings ----------------------------------------------------------------- */

void gtr_init(GuitarState *g, float sr)
{
	memset(g, 0, sizeof *g);
	g->sr = sr;
	g->inv_sr = 1.0f / sr;
	g->hand = 3.0f;
	g->noise = 0x2545F491u;
	for (int i = 0; i < GTR_STRINGS; i++)
		g->s[i].pending = -1, g->s[i].scale = 1.0f;
}

void gtr_reset(GuitarState *g)
{
	float sr = g->sr;
	gtr_init(g, sr);
}

bool gtr_idle(const GuitarState *g)
{
	for (int i = 0; i < GTR_STRINGS; i++)
		if (g->s[i].active || g->s[i].pending >= 0)
			return false;
	return true;
}

/* one-pole loop low-pass magnitude at w (radians / sample) */
static float lp_mag(float a, float w) { return (1.0f - a) / sqrtf(1.0f - 2.0f * a * cosf(w) + a * a); }

/* loop gain per period for a decay of t60 seconds of the fundamental */
static float loop_gain(float f0, float t60, float a, float sr)
{
	float g = powf(10.0f, -3.0f / (f0 * t60));
	return g / lp_mag(a, 2.0f * PI * f0 / sr);
}

static float t60_for(const Patch *p, float f0, u8 art)
{
	const ModelDef *m = &models[p->gtr];
	float t = m->t60 * p->gtr_sustain * sqrtf(110.0f / f0);
	if (art == ART_PALM)
		t = 0.12f + 0.1f * p->gtr_sustain;
	else if (art == ART_DEAD)
		t = 0.025f;
	else if (art == ART_HARMONIC)
		t *= 1.3f;
	return t < 0.02f ? 0.02f : t > 30.0f ? 30.0f : t;
}

static float loop_lp(const Patch *p, u8 art, float f0)
{
	float a = 0.04f + (1.0f - p->gtr_tone) * 0.42f;
	if (p->gtr == GTR_NYLON)
		a += 0.12f;
	if (art == ART_PALM)
		a = 0.72f;
	else if (art == ART_DEAD)
		a = 0.85f;
	else if (art == ART_HARMONIC)
		a *= 0.5f;
	/* high notes: the per-period loss would make them dull too soon */
	a *= f0 > 330.0f ? sqrtf(330.0f / f0) : 1.0f;
	return a;
}

static void set_pitch(GtrString *s, float period, float a)
{
	s->period = period;
	s->lp_coef = a;
}

/* Strike string s now. */
static void pluck(GuitarState *g, const Patch *p, GtrString *s, int note, u8 art, float vel, float len_s, float bend,
                  float up, float detune_cents)
{
	const ModelDef *m = &models[p->gtr];
	if (art == ART_HARMONIC)
		note += 12;
	float f0 = midi_hz((float)note + detune_cents * 0.01f);
	float period = g->sr / f0;
	if (period > GTR_LINE - 8)
		period = GTR_LINE - 8;
	if (period < 4.0f)
		period = 4.0f;
	float a = loop_lp(p, art, f0);
	bool legato = art == ART_HAMMER && s->active && s->amp > 1e-3f;
	/* fold the last ramp into the line (no change to what sounds) */
	if (s->scale != 1.0f && s->scale != 0.0f)
		for (u32 i = 0; i < GTR_LINE; i++)
			s->buf[i] *= s->scale;
	s->scale = 1.0f;
	/* the pick meets the string: most of the old vibration stops, over 3 ms
	 * (at once it was a click on every re-struck string); a hammer-on
	 * instead feeds the vibration a little */
	const float ramp_s = 0.003f;
	s->scale_n = (u32)(ramp_s * g->sr);
	s->scale_mul = powf(legato ? 1.15f : 0.15f, 1.0f / (float)s->scale_n);
	s->exc_wait = legato ? 0 : s->scale_n;
	s->active = true;
	s->held = true;
	s->note = (u8)note;
	s->art = art;
	s->age = 0;
	s->len_s = len_s;
	s->bend = bend;
	s->base_period = period;
	s->bloom = 0;
	s->bloom_ph = 0;
	s->amp = 1.0f;
	set_pitch(s, period, a);
	s->gain = loop_gain(f0, t60_for(p, f0, art), a, g->sr);
	s->gain_to = s->gain;
	s->lp_to = a;
	{
		float w = 2.0f * PI / period;
		s->comp = atan2f(a * sinf(w), 1.0f - a * cosf(w)) / w;
	}
	s->damp_to = art == ART_STACCATO ? 0.02f : art == ART_PALM ? 0.05f : 0.09f;

	/* the excitation: one period */
	u32 n = (u32)lrintf(period);
	if (n >= GTR_LINE)
		n = GTR_LINE - 1;
	float beta = art == ART_HARMONIC ? 0.5f : p->gtr_pick;
	beta = beta < 0.04f ? 0.04f : beta > 0.5f ? 0.5f : beta;
	float hard = 0.25f + 0.45f * p->gtr_tone + 0.3f * vel + (up > 0 ? 0.08f : 0.0f);
	if (p->gtr == GTR_NYLON)
		hard *= 0.6f;
	if (art == ART_HAMMER)
		hard *= 0.35f;
	float noise = art == ART_DEAD ? 1.0f : m->noise;
	float *e = s->exc_buf;
	float mean = 0;
	for (u32 i = 0; i < n; i++) {
		float t = (float)i / (float)n;
		float tri = t < beta ? t / beta : (1.0f - t) / (1.0f - beta);
		e[i] = (1.0f - noise) * tri + noise * white(&g->noise);
		mean += e[i];
	}
	mean /= (float)n;
	/* hardness: a one-pole low-pass run twice (forward, backward: no phase skew) */
	float c = 1.0f - (hard > 1.0f ? 1.0f : hard) * 0.92f, y = 0;
	for (u32 i = 0; i < n; i++)
		y = e[i] = (1.0f - c) * (e[i] - mean) + c * y;
	y = 0;
	for (u32 i = n; i-- > 0;)
		y = e[i] = (1.0f - c) * e[i] + c * y;
	/* the pick position: harmonics with a node there are missing */
	u32 d = (u32)lrintf(beta * (float)n);
	if (d > 0 && art != ART_DEAD) {
		float tmp[GTR_LINE];
		memcpy(tmp, e, n * sizeof(float));
		for (u32 i = 0; i < n; i++)
			e[i] = tmp[i] - tmp[(i + n - d) % n];
	}
	float pk = 1e-6f;
	for (u32 i = 0; i < n; i++)
		pk = fmaxf(pk, fabsf(e[i]));
	float lvl = vel * (art == ART_HAMMER ? 0.5f : 1.0f) * (up > 0 ? 0.85f : 1.0f) / pk;
	if (art == ART_DEAD)
		n = n / 3 + 1, lvl *= 0.6f;
	for (u32 i = 0; i < n; i++)
		e[i] *= lvl;
	/* the pick's edge: a 0.5 ms raised-cosine fade-in (a step into the
	 * loop would click) */
	u32 fade = (u32)(0.0005f * g->sr);
	for (u32 i = 0; i < fade && i < n; i++)
		e[i] *= 0.5f - 0.5f * cosf(PI * (float)i / (float)fade);
	s->exc_len = legato ? 0 : n;
	s->exc_at = 0;

}

void gtr_trigger(GuitarState *g, const Patch *p, const NoteEvent *e, float vel, float len_s)
{
	int open[6];
	int courses = gtr_open_strings(p, open);
	if (e->fx == FX_STRUM) {
		/* notes[k] on course k; strummed in order with e->strum ms between strings */
		int order[6], n = 0;
		for (int k = 0; k < courses; k++)
			if (e->notes[k])
				order[n++] = k;
		bool up = e->strum < 0;
		float step = fabsf((float)e->strum) * 0.001f * g->sr;
		for (int i = 0; i < n; i++) {
			int k = up ? order[n - 1 - i] : order[i];
			int at = (int)lrintf(step * (float)i);
			/* later strings of a strum are a little softer; upstrokes lighter on the bass */
			float v = vel * (1.0f - 0.05f * (float)i / (float)(n > 1 ? n - 1 : 1));
			if (up && k < 3)
				v *= 0.8f;
			GtrString *s = &g->s[k];
			s->pending = at;
			s->p_note = e->notes[k];
			s->p_art = e->art;
			s->p_vel = v;
			s->p_len = len_s;
			s->p_bend = 0;
			s->p_up = up ? 1.0f : 0.0f;
			if (p->gtr == GTR_12STRING) {
				GtrString *c = &g->s[6 + k];
				c->pending = at + (int)(0.0015f * g->sr) * (up ? -1 : 1);
				if (c->pending < 0)
					c->pending = 0;
				c->p_note = (u8)(e->notes[k] + (k < 4 ? 12 : 0));
				c->p_art = e->art;
				c->p_vel = v * 0.7f;
				c->p_len = len_s;
				c->p_bend = 0;
				c->p_up = s->p_up;
			}
		}
		/* the fretting hand moves to the chord */
		int lo = 99;
		for (int k = 0; k < courses; k++)
			if (e->notes[k] && e->notes[k] - open[k] > 0)
				lo = e->notes[k] - open[k] < lo ? e->notes[k] - open[k] : lo;
		if (lo < 99)
			g->hand = (float)lo + 1.0f;
		return;
	}
	/* single notes: each on its string (given, or picked near the hand) */
	int count = e->fx == FX_SLIDE_UP || e->art == ART_SLIDE || e->art == ART_BEND ? 1 : e->count;
	for (int i = 0; i < count && i < 6; i++) {
		int note = e->notes[i];
		int fret = 0;
		int k = e->string >= 1 && e->string <= courses && i == 0 ? e->string - 1 : gtr_pick_string(p, note, g->hand, &fret);
		if (k < 0)
			continue; /* out of the instrument's range */
		if (e->string && i == 0)
			fret = note - open[k];
		g->hand = g->hand * 0.6f + (float)(fret > 0 ? fret : g->hand) * 0.4f;
		float bend = 0;
		if ((e->art == ART_SLIDE || e->art == ART_BEND || e->fx == FX_SLIDE_UP) && e->count >= 2)
			bend = (float)((int)e->notes[1] - (int)e->notes[0]);
		GtrString *s = &g->s[k];
		s->pending = 0;
		s->p_note = (u8)note;
		s->p_art = e->fx == FX_SLIDE_UP ? ART_SLIDE : e->art;
		s->p_vel = vel;
		s->p_len = len_s;
		s->p_bend = bend;
		s->p_up = 0;
		if (p->gtr == GTR_12STRING) {
			GtrString *c = &g->s[6 + k];
			c->pending = (int)(0.0015f * g->sr);
			c->p_note = (u8)(note + (k < 4 ? 12 : 0));
			c->p_art = s->p_art;
			c->p_vel = vel * 0.7f;
			c->p_len = len_s;
			c->p_bend = bend;
			c->p_up = 0;
		}
	}
}

/* the fretting finger lifts (or the palm lands): the string dies in damp_to */
static void release_string(GuitarState *g, GtrString *s, bool fast)
{
	s->held = false;
	float f0 = g->sr / s->period;
	float t = fast ? 0.01f : s->damp_to;
	s->lp_to = s->lp_coef < 0.5f ? 0.5f : s->lp_coef;
	s->gain_to = loop_gain(f0, t, s->lp_to, g->sr);
}

void gtr_release(GuitarState *g, const Patch *p, bool fast)
{
	(void)p;
	for (int i = 0; i < GTR_STRINGS; i++) {
		GtrString *s = &g->s[i];
		if (!s->active || !s->held)
			continue;
		if (!fast && (s->art == ART_RING || s->art == ART_FEEDBACK))
			continue;
		release_string(g, s, fast);
	}
}

/* cubic Lagrange read, d samples back from the write position */
static inline float read_frac(const float *buf, u32 pos, float d)
{
	float fd = floorf(d);
	float fr = d - fd;
	u32 i = (pos - (u32)fd) & (GTR_LINE - 1);
	/* taps at delays fd-1, fd, fd+1, fd+2 */
	float ym1 = buf[(i + 1) & (GTR_LINE - 1)], y0 = buf[i], y1 = buf[(i - 1) & (GTR_LINE - 1)],
	      y2 = buf[(i - 2) & (GTR_LINE - 1)];
	float c0 = y0;
	float c1 = y1 - ym1 / 3.0f - y0 * 0.5f - y2 / 6.0f;
	float c2 = 0.5f * (ym1 + y1) - y0;
	float c3 = (y2 - ym1) / 6.0f + 0.5f * (y0 - y1);
	return ((c3 * fr + c2) * fr + c1) * fr + c0;
}

static float string_step(GuitarState *g, const Patch *p, GtrString *s, bool control)
{
	if (s->pending >= 0) {
		if (s->pending == 0) {
			s->pending = -1;
			pluck(g, p, s, s->p_note, s->p_art, s->p_vel, s->p_len, s->p_bend, s->p_up,
			      (s - g->s) >= 6 ? 3.0f : 0.0f);
		} else {
			s->pending--;
		}
	}
	if (!s->active)
		return 0.0f;
	if (control) {
		/* pitch: bends and slides over the event, vibrato */
		float semis = 0;
		if (s->bend != 0 && s->len_s > 0) {
			float u = s->age / (s->len_s * (s->art == ART_BEND ? 0.35f : 0.6f));
			u = u > 1.0f ? 1.0f : u;
			semis += s->art == ART_SLIDE ? s->bend * u : s->bend * (1.0f - (1.0f - u) * (1.0f - u));
		}
		bool vib = s->art == ART_VIBRATO || s->art == ART_FEEDBACK || (p->gtr == GTR_LEAD && s->age > 0.45f);
		if (vib && s->age > 0.15f) {
			float ramp = fminf(1.0f, (s->age - 0.15f) / 0.3f);
			semis += (p->gtr == GTR_BASS ? 0.12f : 0.28f) * ramp * sinf(2.0f * PI * 5.5f * s->age);
		}
		s->period = s->base_period * exp2f(-semis / 12.0f);
		/* note-off at the event's end; let-ring and feedback notes ring on until re-struck */
		if (s->held && s->len_s > 0 && s->age >= s->len_s && s->art != ART_RING && s->art != ART_FEEDBACK)
			release_string(g, s, false);
		if (s->art == ART_FEEDBACK && s->held && s->age > 0.8f)
			s->bloom = fminf(1.0f, s->bloom + 16.0f * g->inv_sr * 0.35f);
	}
	/* note-off: gain and brightness glide (~2 ms) instead of stepping */
	s->gain += (s->gain_to - s->gain) * 0.01f;
	s->lp_coef += (s->lp_to - s->lp_coef) * 0.01f;
	/* the loop low-pass delays the wave too: its phase delay at the struck
	 * pitch, fixed for the note (recomputed per sample, a filter change
	 * would move the read point and click) */
	float d = s->period - s->comp * (s->period / s->base_period);
	if (d < 2.0f)
		d = 2.0f;
	if (s->scale_n) {
		s->scale *= s->scale_mul;
		s->scale_n--;
	}
	float x = read_frac(s->buf, s->pos, d) * s->scale;
	s->lp = (1.0f - s->lp_coef) * x + s->lp_coef * s->lp;
	float y = s->lp * s->gain;
	if (s->art == ART_FEEDBACK && s->held)
		y = tanhf(y * 1.02f); /* endless: the loop gain sits above 1, bounded */
	float in = y;
	if (s->exc_wait)
		s->exc_wait--;
	else if (s->exc_at < s->exc_len)
		in += s->exc_buf[s->exc_at++];
	s->buf[s->pos] = in / s->scale;
	s->pos = (s->pos + 1) & (GTR_LINE - 1);
	s->age += g->inv_sr;

	float raw = in;
	const ModelDef *m = &models[p->gtr];
	if (m->pickup > 0) {
		/* a magnetic pickup: the string seen at a point, a comb on the output */
		float pd = s->base_period * m->pickup;
		raw -= read_frac(s->buf, s->pos, pd < 2.0f ? 2.0f : pd) * s->scale;
	}
	/* DC blocker on the way out (in the loop its phase would detune the string) */
	float out = raw - s->dc_x + 0.999f * s->dc_y;
	s->dc_x = raw;
	s->dc_y = out;
	if (s->bloom > 0) {
		/* feedback: the octave harmonic swells in from the amp */
		s->bloom_ph += 2.0f / s->period;
		if (s->bloom_ph >= 1.0f)
			s->bloom_ph -= 1.0f;
		out = out * (1.0f - 0.5f * s->bloom) + 0.35f * s->bloom * sinf(2.0f * PI * s->bloom_ph);
	}
	/* stop a string that has died away */
	s->amp = s->amp * 0.9995f + fabsf(in) * 0.0005f;
	if (s->exc_at >= s->exc_len && !s->exc_wait && s->amp < 2e-5f && s->age > 0.05f) {
		s->active = false;
		s->scale = 1.0f;
		s->scale_n = 0;
		memset(s->buf, 0, sizeof s->buf);
		s->lp = s->dc_x = s->dc_y = 0;
	}
	return out;
}

float gtr_render(GuitarState *g, const Patch *p)
{
	const ModelDef *m = &models[p->gtr < GTR_COUNT ? p->gtr : 0];
	bool control = (g->ctl++ & 15) == 0;
	float sum = 0;
	int n = p->gtr == GTR_12STRING ? 12 : 6;
	for (int i = 0; i < n; i++)
		sum += string_step(g, p, &g->s[i], control);
	if (p->gtr == GTR_12STRING)
		sum *= 0.75f;
	float y;
	if (m->body) {
		/* an acoustic body: the air mode, two top-plate modes, a bright one */
		static const float hz[4] = {98.0f, 204.0f, 395.0f, 2600.0f};
		static const float q[4] = {6.0f, 5.0f, 4.0f, 1.5f};
		static const float lv[4] = {0.9f, 0.7f, 0.4f, 0.3f};
		float res = 0;
		for (int b = 0; b < 4; b++) {
			float l = lv[b] * (b == 3 && p->gtr == GTR_NYLON ? 0.3f : 1.0f);
			res += l * svf(sum, svf_g(hz[b], g->sr), 1.0f / q[b], &g->body_ic1[b], &g->body_ic2[b]).bp;
		}
		y = sum * (1.0f - 0.5f * p->gtr_body) + res * p->gtr_body;
	} else {
		/* the amp: high-pass, pre-gain, asymmetric clipping */
		float x = svf(sum, svf_g(p->gtr == GTR_BASS ? 35.0f : 90.0f, g->sr), 1.41f, &g->pre_hp_ic1, &g->pre_hp_ic2).hp;
		float gain = 1.0f + 60.0f * p->gtr_gain * p->gtr_gain;
		x *= gain;
		x = tanhf(x + 0.12f) - tanhf(0.12f);
		/* tone stack: some bass, scooped mids under gain, treble */
		Svf lo = svf(x, svf_g(220.0f, g->sr), 1.2f, &g->tone_ic1[0], &g->tone_ic2[0]);
		Svf mid = svf(x, svf_g(750.0f, g->sr), 1.0f, &g->tone_ic1[1], &g->tone_ic2[1]);
		Svf hi = svf(x, svf_g(2800.0f, g->sr), 1.2f, &g->tone_ic1[2], &g->tone_ic2[2]);
		float scoop = p->gtr_gain > 0.3f ? 0.45f : 0.1f;
		x = x + 0.2f * lo.lp - scoop * mid.bp + (p->gtr_tone - 0.4f) * hi.hp;
		if (p->gtr == GTR_LEAD)
			x = tanhf(3.0f * x); /* the second gain stage */
		x *= m->amp;
		/* the cabinet: band limits and a presence peak */
		float cab_lo = p->gtr == GTR_BASS ? 40.0f : 80.0f, cab_hi = p->gtr == GTR_BASS ? 3500.0f : 5200.0f;
		x = svf(x, svf_g(cab_lo, g->sr), 1.41f, &g->cab_ic1[0], &g->cab_ic2[0]).hp;
		x = svf(x, svf_g(cab_hi, g->sr), 1.41f, &g->cab_ic1[1], &g->cab_ic2[1]).lp;
		x = svf(x, svf_g(cab_hi, g->sr), 1.1f, &g->cab_ic1[2], &g->cab_ic2[2]).lp;
		x += 0.25f * p->gtr_body * svf(x, svf_g(2300.0f, g->sr), 0.7f, &g->cab_ic1[3], &g->cab_ic2[3]).bp;
		y = x;
	}
	y *= p->gain * 0.5f;
	float a = fabsf(y);
	g->level = a > g->level ? a : g->level * 0.9999f;
	return y;
}
