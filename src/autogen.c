/* The autogenerator (autogen.h, docs/autogen.md). */
#include "autogen.h"
#include "chords.h"
#include "composer.h"
#include "guitar.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FT_CLAMP_VEL(v) ((v) < 1 ? 1 : (v) > 127 ? 127 : (v))

static const char *const band_names[BAND_COUNT] = {"synth+guitar", "rock", "acoustic", "metal", "electronic"};

const char *band_name(Band b) { return b < BAND_COUNT ? band_names[b] : "?"; }

bool parse_band(const char *s, Band *b)
{
	for (int i = 0; i < BAND_COUNT; i++)
		if (!strcmp(s, band_names[i])) {
			*b = (Band)i;
			return true;
		}
	return false;
}

/* ---- random numbers (own stream: the composer's draws stay its own) ----------- */

typedef struct {
	u64 s;
} Rng;
static u64 rng_next(Rng *r)
{
	r->s = next_seed(r->s);
	return r->s;
}
static float rng_unit(Rng *r) { return (float)(rng_next(r) >> 40) / 16777216.0f; }
static int rng_range(Rng *r, int lo, int hi) { return lo + (int)(rng_next(r) % (u64)(hi - lo + 1)); }
static bool rng_chance(Rng *r, float p) { return rng_unit(r) < p; }

/* ---- harmony -------------------------------------------------------------------- */

int harmony_extract(const Song *s, ChordSeg *out, int max)
{
	const u32 seg = (u32)ticks_per_bar / 2;
	const u32 total = song_total_ticks(s);
	int n = 0;
	int prev_root = s->key % 12, prev_q = Q_MAJ;
	for (u32 t0 = 0; t0 < total && n < max; t0 += seg) {
		u32 t1 = t0 + seg;
		float w[12] = {0};
		int bass_note = 999;
		for (u32 i = 0; i < s->nevents; i++) {
			const NoteEvent *e = &s->events[i];
			if (e->tick >= t1)
				break;
			if (e->tick + e->len <= t0)
				continue;
			int r = s->role[e->ch] ? s->role[e->ch] - 1 : -1;
			bool harmonic = r == CH_PAD || r == CH_COUNTER || r == CH_CHOIR || r == CH_BASS || r == CH_ARP;
			if (!harmonic || s->patches[e->ch].inst != INST_SYNTH)
				continue;
			u32 a = e->tick > t0 ? e->tick : t0, b = e->tick + e->len < t1 ? e->tick + e->len : t1;
			float dur = (float)(b - a);
			float wt = r == CH_PAD || r == CH_CHOIR ? 1.0f : r == CH_BASS ? 1.2f : r == CH_ARP ? 0.3f : 0.6f;
			int cnt = e->fx == FX_SLIDE_UP ? 1 : e->count;
			for (int k = 0; k < cnt && k < 6; k++)
				w[e->notes[k] % 12] += dur * wt;
			if (r == CH_BASS && e->notes[0] < bass_note && e->tick < t0 + seg / 2)
				bass_note = e->notes[0];
		}
		float mx = 0;
		for (int k = 0; k < 12; k++)
			mx = w[k] > mx ? w[k] : mx;
		int root = prev_root, q = prev_q;
		if (mx > 0) {
			u16 pcs = 0;
			for (int k = 0; k < 12; k++)
				if (w[k] >= mx * 0.18f)
					pcs |= (u16)(1u << k);
			int bp = bass_note < 999 ? bass_note % 12 : -1;
			int r2, q2;
			if (chord_identify(pcs, bp, &r2, &q2)) {
				root = r2, q = q2;
				/* the guitar keeps it simple: rare colours become triads */
				if (q == Q_9 || q == Q_MAJ9 || q == Q_MIN9 || q == Q_6 || q == Q_MIN6)
					q = qualities[q].iv[1] == 3 ? Q_MIN : Q_MAJ;
			}
		}
		if (n > 0 && out[n - 1].root == root && out[n - 1].quality == q) {
			out[n - 1].len += seg;
		} else {
			out[n++] = (ChordSeg){t0, seg, (u8)root, (u8)q, (u8)root};
		}
		prev_root = root, prev_q = q;
	}
	return n;
}

/* ---- tracks --------------------------------------------------------------------- */

static int add_track(Song *s, const char *name, Patch p, float gain, float pan)
{
	if (s->ntracks >= FT_MAX_TRACKS)
		return -1;
	int t = s->ntracks++;
	snprintf(s->track_name[t], sizeof s->track_name[t], "%s", name);
	s->patches[t] = p;
	s->role[t] = 0;
	s->mix.gain[t] = gain;
	s->mix.pan[t] = pan;
	s->mix.delay[t] = 0;
	s->mix.reverb[t] = 0.15f;
	s->mix.duck[t] = 0;
	s->mix.chorus[t] = 0;
	s->mix.highpass[t] = 0;
	s->mix.bright[t] = false;
	return t;
}

/* drop track t: its events go, later tracks move down */
static void remove_track(Song *s, int t)
{
	u32 w = 0;
	for (u32 i = 0; i < s->nevents; i++) {
		NoteEvent e = s->events[i];
		if (e.ch == t)
			continue;
		if (e.ch > t)
			e.ch--;
		s->events[w++] = e;
	}
	s->nevents = w;
	for (int k = t; k + 1 < s->ntracks; k++) {
		memcpy(s->track_name[k], s->track_name[k + 1], sizeof s->track_name[k]);
		s->patches[k] = s->patches[k + 1];
		s->role[k] = s->role[k + 1];
		s->mix.gain[k] = s->mix.gain[k + 1];
		s->mix.pan[k] = s->mix.pan[k + 1];
		s->mix.delay[k] = s->mix.delay[k + 1];
		s->mix.reverb[k] = s->mix.reverb[k + 1];
		s->mix.duck[k] = s->mix.duck[k + 1];
		s->mix.bright[k] = s->mix.bright[k + 1];
		s->mix.chorus[k] = s->mix.chorus[k + 1];
		s->mix.highpass[k] = s->mix.highpass[k + 1];
	}
	s->ntracks--;
	s->patches[s->ntracks] = patch_default();
	s->role[s->ntracks] = 0;
	s->track_name[s->ntracks][0] = 0;
}

static int track_of_role(const Song *s, int ch)
{
	for (int t = 0; t < s->ntracks; t++)
		if (s->role[t] == ch + 1)
			return t;
	return -1;
}

static bool track_has_events(const Song *s, int t)
{
	for (u32 i = 0; i < s->nevents; i++)
		if (s->events[i].ch == t)
			return true;
	return false;
}

/* ---- rhythm guitars ------------------------------------------------------------- */

/* Pattern choices per band and section: a list to pick from (a section's
 * repeats take different ones), and a busier fill for the last two bars of
 * each 8-bar phrase. */
typedef struct {
	const char *main[6];
	const char *fill[3];
} PatternSet;

static int list_len(const char *const *l, int max)
{
	int n = 0;
	while (n < max && l[n])
		n++;
	return n;
}

static const PatternSet *pattern_set(Band band, SectionKind k, const char *style)
{
	bool fast = !strcmp(style, "amiga punk") || !strcmp(style, "hardtrance") || !strcmp(style, "hardstyle");
	static const PatternSet ac_intro = {{"Travis", "Ballad picking", "Pima", "Forward roll", "Outside-in", "Pedal arpeggio"}, {"Arpeggio up-down 16ths", "Cascade 16ths"}};
	static const PatternSet ac_quiet = {{"Arpeggio up", "Arpeggio down", "Root-fifth picks", "Syncopated picking", "Backward roll"}, {"Pinch + roll", "Arpeggio 16ths"}};
	static const PatternSet ac_loud = {{"Folk", "Island", "Calypso", "Pop", "Offbeat pushes"}, {"Punk 16ths", "Push 3-3-2"}};
	static const PatternSet ac_verse = {{"Bass + strum", "Pop", "Waltz feel", "Travis pinch", "Half-time"}, {"Folk", "Island"}};
	static const PatternSet ac_solo = {{"Arpeggio 16ths", "Arpeggio up-down 16ths", "Cascade 16ths"}, {"Pinch + roll"}};
	static const PatternSet mt_quiet = {{"Halves", "Whole", "Muted arpeggio", "Stop-time"}, {"Chug + accent"}};
	static const PatternSet mt_verse = {{"Gallop", "Metal 16ths", "Chug + accent", "Rock chug"}, {"Punk 16ths", "Gallop"}};
	static const PatternSet mt_loud = {{"Rock 8ths", "Push 3-3-2", "Punk", "Chug + accent"}, {"Metal 16ths", "Punk 16ths"}};
	static const PatternSet rk_intro = {{"Arpeggio up", "Halves", "Muted arpeggio", "Pedal arpeggio", "Stop-time"}, {"Rock chug"}};
	static const PatternSet rk_quiet = {{"Whole", "Half-time", "Arpeggio down", "Root-fifth picks"}, {"Halves", "Arpeggio up"}};
	static const PatternSet rk_verse = {{"Rock chug", "Muted arpeggio", "Shuffle", "Push 3-3-2", "Offbeat pushes"}, {"Rock 8ths", "Punk"}};
	static const PatternSet rk_loud = {{"Rock 8ths", "Shuffle", "Push 3-3-2", "Calypso", "Stop-time"}, {"Punk 16ths", "Rock 8ths"}};
	static const PatternSet rk_fast = {{"Punk", "Punk 16ths", "Rock 8ths"}, {"Punk 16ths"}};
	static const PatternSet rk_solo = {{"Arpeggio 16ths", "Cascade 16ths", "Arpeggio up-down 16ths"}, {"Punk 16ths"}};
	static const PatternSet sg_quiet = {{"Ballad", "Half-time", "Whole", "Arpeggio up"}, {"Arpeggio down"}};
	static const PatternSet sg_loud = {{"Pop", "Ska", "Calypso", "Offbeat pushes", "Disco scratch"}, {"Funk 16ths", "Disco scratch"}};
	static const PatternSet sg_dance = {{"Ska", "Reggae", "Disco scratch", "Offbeat pushes"}, {"Funk 16ths"}};
	static const PatternSet sg_verse = {{"Funk", "Quarters", "Disco scratch", "Syncopated picking", "Root-fifth picks"}, {"Funk 16ths", "Arpeggio 16ths"}};
	static const PatternSet sg_solo = {{"Arpeggio 16ths", "Cascade 16ths", "Arpeggio up-down 16ths"}, {"Pinch + roll"}};
	bool quiet = k == SEC_INTRO || k == SEC_OUTRO || k == SEC_BREAKDOWN || k == SEC_BRIDGE;
	bool loud = k == SEC_CHORUS || k == SEC_DROP || k == SEC_CHOIR;
	switch (band) {
	case BAND_ACOUSTIC:
		return k == SEC_ARP_SOLO ? &ac_solo : k == SEC_INTRO || k == SEC_OUTRO ? &ac_intro : quiet ? &ac_quiet : loud ? &ac_loud : &ac_verse;
	case BAND_METAL:
		return quiet ? &mt_quiet : loud ? &mt_loud : &mt_verse;
	case BAND_ROCK:
		if (k == SEC_ARP_SOLO)
			return &rk_solo;
		if (k == SEC_INTRO)
			return &rk_intro;
		if (quiet)
			return &rk_quiet;
		if (fast)
			return &rk_fast;
		return loud ? &rk_loud : &rk_verse;
	default:
		if (k == SEC_ARP_SOLO)
			return &sg_solo;
		if (quiet)
			return &sg_quiet;
		if (loud)
			return !strcmp(style, "eurodance") || !strcmp(style, "techno") ? &sg_dance : &sg_loud;
		return &sg_verse;
	}
}

static const StrumPattern *pattern_named(const char *n)
{
	int i = strum_find(n);
	return &strum_patterns[i >= 0 ? i : 0];
}

static u8 section_vel(SectionKind k)
{
	switch (k) {
	case SEC_INTRO:
	case SEC_OUTRO:
		return 72;
	case SEC_BREAKDOWN:
	case SEC_BRIDGE:
		return 66;
	case SEC_CHORUS:
	case SEC_DROP:
	case SEC_CHOIR:
		return 104;
	default:
		return 88;
	}
}

/* the voicing nearest the hand (and its alternative for a second guitar) */
static const Voicing *choose_voicing(const ChordLib *lib, int root, int q, float *hand, bool high, bool power)
{
	if (power)
		q = Q_POWER;
	int n = chordlib_count(lib, root, q);
	if (!n) {
		q = qualities[q].iv[1] == 3 ? Q_MIN : Q_MAJ;
		n = chordlib_count(lib, root, q);
	}
	const Voicing *best = NULL;
	float bs = 1e9f;
	for (int i = 0; i < n && i < 8; i++) {
		const Voicing *v = chordlib_get(lib, root, q, i);
		float pos = v->lo ? (float)v->lo : 1.0f;
		float target = high ? *hand + 5.0f : *hand;
		float c = v->score + fabsf(pos - target) * 0.5f;
		if (power && v->fret[0] < 0 && v->fret[1] < 0)
			c += 2.0f; /* chugs want the low strings */
		if (c < bs)
			bs = c, best = v;
	}
	if (best && !high)
		*hand = *hand * 0.5f + (best->lo ? (float)best->lo : 1.0f) * 0.5f;
	return best;
}

static void write_rhythm(Song *s, int ch, const ChordSeg *segs, int nseg, Band band, bool high, Rng *r)
{
	ChordLib *lib = malloc(sizeof *lib);
	if (!lib)
		return;
	int open[6];
	int strings = gtr_open_strings(&s->patches[ch], open);
	chordlib_build(lib, open, strings);
	bool power = s->patches[ch].gtr == GTR_CRUNCH || s->patches[ch].gtr == GTR_LEAD;
	float hand = high ? 5.0f : 1.0f;
	/* the pattern of each section kind's first occurrence, so repeats differ */
	int first_pick[SEC_COUNT];
	int seen[SEC_COUNT] = {0};
	for (int k = 0; k < SEC_COUNT; k++)
		first_pick[k] = -1;
	for (u32 si = 0; si < s->nsections; si++) {
		const Section *sec = &s->sections[si];
		u32 a = sec->start_bar * (u32)ticks_per_bar, b = (sec->start_bar + sec->bars) * (u32)ticks_per_bar;
		const PatternSet *set = pattern_set(band, sec->kind, s->style);
		int nm = list_len(set->main, 6), nf = list_len(set->fill, 3);
		int pick;
		if (first_pick[sec->kind] < 0) {
			pick = rng_range(r, 0, nm - 1);
			first_pick[sec->kind] = pick;
		} else {
			/* a repeat: the same pattern half the time (the song's identity), else the next one */
			pick = rng_chance(r, 0.5f) ? first_pick[sec->kind] : (first_pick[sec->kind] + 1 + seen[sec->kind]) % nm;
		}
		seen[sec->kind]++;
		const StrumPattern *pat = pattern_named(set->main[pick]);
		const StrumPattern *fill = nf ? pattern_named(set->fill[rng_range(r, 0, nf - 1)]) : pat;
		bool fills = sec->bars >= 8 && rng_chance(r, 0.7f);
		/* power chords in the loud parts only; open voicings ring in the quiet ones */
		bool loud = sec->kind == SEC_CHORUS || sec->kind == SEC_DROP || sec->kind == SEC_VERSE || sec->kind == SEC_GROOVE;
		bool pw = power && loud;
		u8 vel = section_vel(sec->kind);
		for (int k = 0; k < nseg; k++) {
			u32 t0 = segs[k].tick > a ? segs[k].tick : a, t1 = segs[k].tick + segs[k].len < b ? segs[k].tick + segs[k].len : b;
			if (t1 <= t0)
				continue;
			/* a band guitar plays the chord plainly: sevenths and added notes off */
			int q = segs[k].quality;
			if (band != BAND_SYNTH_GUITAR) {
				if (q == Q_MAJ7 || q == Q_ADD9 || q == Q_6)
					q = Q_MAJ;
				else if (q == Q_MIN7 || q == Q_MINADD9 || q == Q_MIN6)
					q = Q_MIN;
				else if (q == Q_DIM7 || q == Q_MIN7B5)
					q = Q_DIM;
			}
			const Voicing *v = choose_voicing(lib, segs[k].root, q, &hand, high, pw);
			if (!v)
				continue;
			u8 notes[6];
			voicing_notes(lib, v, notes);
			u8 jv = (u8)FT_CLAMP_VEL(vel + rng_range(r, -5, 5));
			/* bar by bar: the last two bars of each 8-bar phrase take the fill */
			for (u32 t = t0; t < t1;) {
				u32 bar = t / (u32)ticks_per_bar - sec->start_bar;
				u32 bar_end = (t / (u32)ticks_per_bar + 1) * (u32)ticks_per_bar;
				u32 e = bar_end < t1 ? bar_end : t1;
				bool in_fill = fills && bar % 8 >= 6;
				/* a pattern shorter than a bar restarts on the bar line */
				strum_write_ex(s, ch, t, e - t, notes, strings, in_fill ? fill : pat, (u8)FT_CLAMP_VEL(jv + (in_fill ? 6 : 0)),
				               ART_NORMAL, false, t == t0);
				t = e;
			}
		}
	}
	free(lib);
}

/* ---- lead and bass ---------------------------------------------------------------- */

static void write_lead(Song *s, int src, int ch, Rng *r)
{
	u32 n0 = s->nevents;
	u32 last_end = 0;
	int last_note = -1;
	for (u32 i = 0; i < n0; i++) {
		const NoteEvent *e = &s->events[i];
		if (e->ch != src || s->patches[src].inst != INST_SYNTH)
			continue;
		NoteEvent g = *e;
		g.ch = (u8)ch;
		g.fx = FX_NONE;
		g.count = 1;
		g.gate = 0;
		int n = e->notes[0];
		while (n > 86)
			n -= 12;
		while (n < 52)
			n += 12;
		g.notes[0] = (u8)n;
		g.art = ART_NORMAL;
		bool after_rest = e->tick > last_end + (u32)ticks_per_row * 2;
		if (e->len >= (u16)(ticks_per_row * 6))
			g.art = ART_VIBRATO;
		if (last_note >= 0 && !after_rest && abs(n - last_note) <= 2 && e->len <= ticks_per_row * 2)
			g.art = ART_HAMMER;
		if (last_note >= 0 && !after_rest && abs(n - last_note) >= 5 && rng_chance(r, 0.35f) && e->len >= ticks_per_row * 2) {
			/* slide into the leap from two frets below */
			g.art = ART_SLIDE;
			g.count = 2;
			g.notes[1] = (u8)n;
			g.notes[0] = (u8)(n - 2);
		}
		/* a phrase's last long note: bent up into it */
		const NoteEvent *nx = NULL;
		for (u32 j = i + 1; j < n0; j++)
			if (s->events[j].ch == src) {
				nx = &s->events[j];
				break;
			}
		if (e->len >= ticks_per_row * 4 && (!nx || nx->tick > e->tick + e->len + (u32)ticks_per_row * 4) && rng_chance(r, 0.5f)) {
			g.art = ART_BEND;
			g.count = 2;
			g.notes[0] = (u8)(n - 2);
			g.notes[1] = (u8)n;
		}
		g.vel = (u8)FT_CLAMP_VEL(e->vel + 6);
		song_add(s, g);
		last_end = e->tick + e->len;
		last_note = n;
	}
}

static void write_bass(Song *s, int src, int ch)
{
	u32 n0 = s->nevents;
	for (u32 i = 0; i < n0; i++) {
		const NoteEvent *e = &s->events[i];
		if (e->ch != src)
			continue;
		NoteEvent g = *e;
		g.ch = (u8)ch;
		g.fx = FX_NONE;
		g.count = 1;
		g.gate = 0;
		int n = e->notes[0];
		while (n > 52)
			n -= 12;
		while (n < 28)
			n += 12;
		g.notes[0] = (u8)n;
		g.art = e->len <= 3 ? ART_STACCATO : ART_NORMAL;
		song_add(s, g);
	}
}

/* ---- the whole ------------------------------------------------------------------- */

static void transpose(Song *s, int semis)
{
	if (!semis)
		return;
	s->key += semis;
	s->final_key += semis;
	for (u32 i = 0; i < s->nevents; i++) {
		NoteEvent *e = &s->events[i];
		if (s->patches[e->ch].inst == INST_KIT)
			continue;
		for (int k = 0; k < 6; k++)
			if (e->notes[k])
				e->notes[k] = (u8)FT_CLAMP_VEL(e->notes[k] + semis);
	}
}

bool autogen(Song *out, const AutogenOptions *o)
{
	ComposeOptions co = {.variations = o->variations};
	if (!compose_ex(out, o->seed, o->mood, &co))
		return false;
	for (int c = 0; c < CH_COUNT && c < out->ntracks; c++) {
		out->role[c] = (u8)(c + 1);
		snprintf(out->track_name[c], sizeof out->track_name[c], "%s", channel_name(c));
		out->track_name[c][0] = (char)(out->track_name[c][0] - 32); /* Kick, Snare, ... */
	}
	if (o->bpm > 0)
		out->bpm = o->bpm;
	if (o->key >= 0) {
		int d = ((o->key % 12) - out->key % 12 + 12) % 12;
		transpose(out, d > 6 ? d - 12 : d);
	}
	Rng r = {o->seed ^ 0xF7200200ull ^ ((u64)o->band << 32)};
	ChordSeg segs[1024];
	int nseg = harmony_extract(out, segs, 1024);

	int bass = track_of_role(out, CH_BASS), lead = track_of_role(out, CH_LEAD);
	switch (o->band) {
	case BAND_ELECTRONIC:
		break;
	case BAND_SYNTH_GUITAR: {
		Patch p = gtr_preset(rng_chance(&r, 0.5f) ? GTR_CLEAN : GTR_STEEL);
		int g = add_track(out, "Guitar", p, 0.55f, -0.3f);
		out->mix.delay[g] = 0.12f;
		write_rhythm(out, g, segs, nseg, o->band, false, &r);
		break;
	}
	case BAND_ROCK:
	case BAND_METAL: {
		bool metal = o->band == BAND_METAL;
		Patch pb = gtr_preset(GTR_BASS);
		int b = add_track(out, "Bass guitar", pb, 0.75f, 0.0f);
		out->mix.duck[b] = 0.2f;
		write_bass(out, bass, b);
		Patch pr = gtr_preset(metal ? GTR_LEAD : GTR_CRUNCH);
		if (metal)
			pr.gtr_sustain = 1.2f, pr.gtr_tone = 0.45f;
		int g1 = add_track(out, "Rhythm L", pr, 0.5f, -0.6f);
		write_rhythm(out, g1, segs, nseg, o->band, false, &r);
		pr.gtr_pick = 0.16f;
		int g2 = add_track(out, "Rhythm R", pr, 0.5f, 0.6f);
		write_rhythm(out, g2, segs, nseg, o->band, true, &r);
		Patch pl = gtr_preset(GTR_LEAD);
		int l = add_track(out, "Lead guitar", pl, 0.5f, 0.1f);
		out->mix.delay[l] = 0.18f;
		out->mix.reverb[l] = 0.25f;
		write_lead(out, lead, l, &r);
		/* the synths that stay: drums, a quiet pad, the fx; the rest go */
		for (int t = out->ntracks - 1; t >= 0; t--) {
			int rl = out->role[t] ? out->role[t] - 1 : -1;
			if (rl == CH_BASS || rl == CH_LEAD || rl == CH_ARP || rl == CH_COUNTER || rl == CH_CHOIR)
				remove_track(out, t);
			else if (rl == CH_PAD)
				out->mix.gain[t] *= metal ? 0.0f : 0.45f;
		}
		break;
	}
	case BAND_ACOUSTIC: {
		Patch pb = gtr_preset(GTR_BASS);
		pb.gtr_tone = 0.25f;
		int b = add_track(out, "Bass guitar", pb, 0.7f, 0.0f);
		write_bass(out, bass, b);
		int g1 = add_track(out, "Steel", gtr_preset(rng_chance(&r, 0.3f) ? GTR_12STRING : GTR_STEEL), 0.6f, -0.35f);
		write_rhythm(out, g1, segs, nseg, o->band, false, &r);
		int g2 = add_track(out, "Nylon", gtr_preset(GTR_NYLON), 0.5f, 0.35f);
		out->mix.reverb[g2] = 0.3f;
		write_lead(out, lead, g2, &r);
		for (int t = out->ntracks - 1; t >= 0; t--) {
			int rl = out->role[t] ? out->role[t] - 1 : -1;
			if (rl == CH_BASS || rl == CH_LEAD || rl == CH_ARP || rl == CH_COUNTER || rl == CH_CHOIR || rl == CH_FX)
				remove_track(out, t);
			else if (rl == CH_PAD)
				out->mix.gain[t] *= 0.35f;
			else if (rl == CH_KICK || rl == CH_SNARE || rl == CH_HATS)
				out->mix.gain[t] *= 0.6f;
		}
		break;
	}
	default:
		break;
	}
	/* tracks the composer left empty go */
	for (int t = out->ntracks - 1; t >= 0; t--)
		if (!track_has_events(out, t))
			remove_track(out, t);
	song_sort(out);
	char extra[96];
	snprintf(extra, sizeof extra, ", %s band", band_name(o->band));
	size_t dl = strlen(out->description);
	if (dl + strlen(extra) < sizeof out->description)
		memcpy(out->description + dl, extra, strlen(extra) + 1);
	return true;
}
