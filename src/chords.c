/* Chords for fretted instruments (chords.h). */
#include "chords.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define FT_CLAMP_I(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))

const char *const note_names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

/* optional: bit i of the interval list may be omitted (fifths; the root
 * never, it is in the bass) */
const QualityDef qualities[Q_COUNT] = {
    [Q_MAJ] = {"maj", "", 3, {0, 4, 7}, 0},
    [Q_MIN] = {"min", "m", 3, {0, 3, 7}, 0},
    [Q_POWER] = {"5", "5", 2, {0, 7}, 0},
    [Q_7] = {"7", "7", 4, {0, 4, 7, 10}, 1 << 2},
    [Q_MAJ7] = {"maj7", "maj7", 4, {0, 4, 7, 11}, 1 << 2},
    [Q_MIN7] = {"m7", "m7", 4, {0, 3, 7, 10}, 1 << 2},
    [Q_SUS2] = {"sus2", "sus2", 3, {0, 2, 7}, 0},
    [Q_SUS4] = {"sus4", "sus4", 3, {0, 5, 7}, 0},
    [Q_DIM] = {"dim", "dim", 3, {0, 3, 6}, 0},
    [Q_AUG] = {"aug", "aug", 3, {0, 4, 8}, 0},
    [Q_ADD9] = {"add9", "add9", 4, {0, 4, 7, 2}, 1 << 2},
    [Q_6] = {"6", "6", 4, {0, 4, 7, 9}, 1 << 2},
    [Q_MIN6] = {"m6", "m6", 4, {0, 3, 7, 9}, 1 << 2},
    [Q_9] = {"9", "9", 5, {0, 4, 7, 10, 2}, 1 << 2},
    [Q_MIN9] = {"m9", "m9", 5, {0, 3, 7, 10, 2}, 1 << 2},
    [Q_7SUS4] = {"7sus4", "7sus4", 4, {0, 5, 7, 10}, 1 << 2},
    [Q_DIM7] = {"dim7", "dim7", 4, {0, 3, 6, 9}, 0},
    [Q_MIN7B5] = {"m7b5", "m7b5", 4, {0, 3, 6, 10}, 0},
    [Q_MAJ9] = {"maj9", "maj9", 5, {0, 4, 7, 11, 2}, 1 << 2},
    [Q_MINADD9] = {"madd9", "madd9", 4, {0, 3, 7, 2}, 1 << 2},
};

void chord_name(int root, int q, char *buf, size_t n)
{
	snprintf(buf, n, "%s%s", note_names[((root % 12) + 12) % 12], q >= 0 && q < Q_COUNT ? qualities[q].suffix : "?");
}

bool chord_parse(const char *s, int *root, int *q)
{
	static const int base[7] = {9, 11, 0, 2, 4, 5, 7}; /* A..G */
	int c = toupper((unsigned char)s[0]);
	if (c < 'A' || c > 'G')
		return false;
	int r = base[c - 'A'];
	s++;
	if (*s == '#')
		r++, s++;
	else if (*s == 'b')
		r--, s++;
	r = (r + 12) % 12;
	/* the longest suffix that matches */
	int best = -1;
	size_t best_len = 0;
	for (int k = 0; k < Q_COUNT; k++) {
		size_t l = strlen(qualities[k].suffix);
		if (!strcmp(s, qualities[k].suffix) && (best < 0 || l >= best_len))
			best = k, best_len = l;
	}
	if (best < 0 && (!strcmp(s, "min") || !strcmp(s, "-")))
		best = Q_MIN;
	if (best < 0 && !strcmp(s, "maj"))
		best = Q_MAJ;
	if (best < 0)
		return false;
	*root = r;
	*q = best;
	return true;
}

static u16 quality_mask(int root, int q)
{
	u16 m = 0;
	for (int i = 0; i < qualities[q].n; i++)
		m |= (u16)(1u << ((root + qualities[q].iv[i]) % 12));
	return m;
}

bool chord_identify(u16 pcs, int bass_pc, int *root, int *q)
{
	int best_r = -1, best_q = -1;
	float best = 1e9f;
	for (int r = 0; r < 12; r++)
		for (int k = 0; k < Q_COUNT; k++) {
			u16 m = quality_mask(r, k);
			int missing = __builtin_popcount(m & ~pcs), extra = __builtin_popcount(pcs & ~m);
			if (!((pcs >> r) & 1u))
				continue; /* the root must sound */
			float cost = (float)missing * 1.0f + (float)extra * 1.5f + (r == bass_pc ? 0.0f : 0.6f) + (float)k * 0.01f;
			if (cost < best)
				best = cost, best_r = r, best_q = k;
		}
	if (best_r < 0 || best > 2.5f)
		return false;
	*root = best_r;
	*q = best_q;
	return true;
}

/* ---- the voicing search ------------------------------------------------------ */

typedef struct {
	const int *open;
	int strings;
	int root, q;
	u16 mask;        /* chord pitch classes */
	u16 need;        /* pitch classes that must sound */
	Voicing *out;
	u16 *count;
	int window;      /* lowest fret of the window (0: open position) */
	i8 cur[6];
} Search;

/* false: unplayable or not this chord; else v is filled, v->score lower = better */
static bool score_voicing(const Search *S, const i8 *fr, Voicing *v)
{
	int lo = 99, hi = 0, sounded = 0, fretted = 0, first = -1, last = -1, interior_mutes = 0, open_n = 0;
	u16 have = 0;
	int bass_pc = -1;
	for (int s = 0; s < S->strings; s++) {
		if (fr[s] < 0)
			continue;
		if (first < 0)
			first = s;
		last = s;
		sounded++;
		int pc = (S->open[s] + fr[s]) % 12;
		if (bass_pc < 0)
			bass_pc = pc;
		have |= (u16)(1u << pc);
		if (fr[s] == 0)
			open_n++;
		else {
			fretted++;
			lo = fr[s] < lo ? fr[s] : lo;
			hi = fr[s] > hi ? fr[s] : hi;
		}
	}
	if (first < 0)
		return false;
	for (int s = first; s <= last; s++)
		if (fr[s] < 0)
			interior_mutes++;
	if (bass_pc != S->root % 12)
		return false;
	if ((have & S->need) != S->need)
		return false;
	int min_sounded = S->q == Q_POWER ? 2 : 3;
	if (sounded < min_sounded || interior_mutes > 1)
		return false;
	if (S->q == Q_POWER && sounded > 4)
		return false;
	if (lo == 99)
		lo = hi = 0;
	if (hi - lo > 3)
		return false;
	/* fingers: a barre at the lowest fret covers every string from the first
	 * at that fret up to the top sounded string, if none of them is open or muted */
	int fingers = fretted, barre = 0;
	if (fretted >= 2) {
		int bstart = -1, at_lo = 0;
		for (int s = first; s <= last; s++)
			if (fr[s] == lo) {
				if (bstart < 0)
					bstart = s;
				at_lo++;
			}
		bool ok = at_lo >= 2 && lo > 0;
		for (int s = bstart; ok && s <= last; s++)
			if (fr[s] <= 0)
				ok = false;
		if (ok) {
			barre = lo;
			fingers = 1;
			for (int s = first; s <= last; s++)
				if (fr[s] > lo)
					fingers++;
		}
	}
	if (fingers > 4)
		return false;
	/* a barre and open strings above it do not mix (the barre would stop them) */
	float sc = 0;
	sc += (float)lo * 0.35f;                     /* low on the neck is easier */
	sc += (float)(hi - lo) * 0.6f;               /* stretch */
	sc += (float)fingers * 0.45f;
	sc += barre ? 0.9f : 0.0f;
	sc += (float)interior_mutes * 1.8f;
	sc -= (float)sounded * 0.8f;                 /* fuller is better */
	sc -= lo == 0 || open_n ? (float)open_n * 0.25f : 0.0f;
	sc += (float)first * 0.25f;                  /* a root on the low strings is fuller */
	if (S->q == Q_POWER)
		sc += (float)first * 0.5f + (sounded > 3 ? 1.0f : 0.0f) + (float)lo * 0.1f;
	/* doubled thirds sound muddy, doubled roots and fifths are fine */
	int third = -1;
	for (int i = 0; i < qualities[S->q].n; i++)
		if (qualities[S->q].iv[i] == 3 || qualities[S->q].iv[i] == 4)
			third = (S->root + qualities[S->q].iv[i]) % 12;
	if (third >= 0) {
		int n3 = 0;
		for (int s = 0; s < S->strings; s++)
			if (fr[s] >= 0 && (S->open[s] + fr[s]) % 12 == third)
				n3++;
		sc += n3 > 1 ? 0.3f * (float)(n3 - 1) : 0.0f;
	}
	/* the same pitch twice (an open string and a fretted unison), a root
	 * three times, open strings under a hand high up the neck: all awkward */
	int nroot = 0;
	for (int a = 0; a < S->strings; a++) {
		if (fr[a] < 0)
			continue;
		int pa = S->open[a] + fr[a];
		nroot += pa % 12 == S->root % 12;
		for (int b = a + 1; b < S->strings; b++)
			if (fr[b] >= 0 && S->open[b] + fr[b] == pa)
				sc += 0.8f;
		if (fr[a] == 0 && hi >= 4)
			sc += 0.6f + (float)(hi - 4) * 0.15f;
		if (fr[a] == 0 && lo >= 2)
			sc += 0.4f;
	}
	sc += nroot > 2 ? 0.4f * (float)(nroot - 2) : 0.0f;
	memcpy(v->fret, fr, 6);
	for (int s = S->strings; s < 6; s++)
		v->fret[s] = -1;
	v->root = (u8)(S->root % 12);
	v->quality = (u8)S->q;
	v->lo = (u8)lo;
	v->hi = (u8)hi;
	v->sounded = (u8)sounded;
	v->barre = (u8)barre;
	v->score = sc;
	return true;
}

static void keep(Search *S, const Voicing *v)
{
	u16 n = *S->count;
	/* the same shape found from two windows */
	for (int i = 0; i < n; i++)
		if (!memcmp(S->out[i].fret, v->fret, 6))
			return;
	if (n < CHORD_MAX_VOICINGS) {
		S->out[n] = *v;
		*S->count = ++n;
	} else if (v->score < S->out[n - 1].score) {
		S->out[n - 1] = *v;
	} else {
		return;
	}
	/* insertion sort by score */
	for (int i = n - 1; i > 0 && S->out[i].score < S->out[i - 1].score; i--) {
		Voicing t = S->out[i];
		S->out[i] = S->out[i - 1];
		S->out[i - 1] = t;
	}
}

/* Depth-first over strings: muted, open (if a chord tone), or a chord-tone
 * fret within the window [window, window + 3]. */
static void search(Search *S, int s)
{
	if (s == S->strings) {
		Voicing v;
		if (score_voicing(S, S->cur, &v))
			keep(S, &v);
		return;
	}
	S->cur[s] = -1;
	search(S, s + 1);
	int lo = S->window < 1 ? 1 : S->window, hi = S->window + 3;
	if ((S->mask >> (S->open[s] % 12)) & 1u) {
		S->cur[s] = 0;
		search(S, s + 1);
	}
	for (int f = lo; f <= hi; f++)
		if ((S->mask >> ((S->open[s] + f) % 12)) & 1u) {
			S->cur[s] = (i8)f;
			search(S, s + 1);
		}
	S->cur[s] = -1;
}

void chordlib_build(ChordLib *lib, const int open[6], int strings)
{
	memset(lib, 0, sizeof *lib);
	for (int i = 0; i < 6; i++)
		lib->open[i] = open[i];
	lib->strings = strings;
	for (int root = 0; root < 12; root++)
		for (int q = 0; q < Q_COUNT; q++) {
			Search S = {.open = lib->open, .strings = strings, .root = root, .q = q};
			S.mask = quality_mask(root, q);
			S.need = 0;
			for (int i = 0; i < qualities[q].n; i++)
				if (!((qualities[q].optional >> i) & 1u))
					S.need |= (u16)(1u << ((root + qualities[q].iv[i]) % 12));
			S.out = lib->v[root][q];
			S.count = &lib->count[root][q];
			for (int w = 0; w <= 12; w++) {
				S.window = w;
				search(&S, 0);
			}
		}
}

void voicing_notes(const ChordLib *lib, const Voicing *v, u8 notes[6])
{
	for (int s = 0; s < 6; s++)
		notes[s] = s < lib->strings && v->fret[s] >= 0 ? (u8)(lib->open[s] + v->fret[s]) : 0;
}

void voicing_shape(const ChordLib *lib, const Voicing *v, char *buf, size_t n)
{
	size_t k = 0;
	for (int s = 0; s < lib->strings && k + 5 < n; s++) {
		if (v->fret[s] < 0)
			buf[k++] = 'x';
		else if (v->fret[s] < 10)
			buf[k++] = (char)('0' + v->fret[s]);
		else
			k += (size_t)snprintf(buf + k, n - k, "(%d)", v->fret[s]);
	}
	buf[k] = 0;
}

/* ---- strum patterns ---------------------------------------------------------- */

const StrumPattern strum_patterns[] = {
    {"Whole", "ballad", "D...............", ART_RING, 14},
    {"Halves", "ballad", "D.......D.......", ART_RING, 12},
    {"Quarters", "pop", "D...d...D...d...", ART_NORMAL, 10},
    {"Folk", "folk", "D...D.u...u.D.u.", ART_NORMAL, 9},
    {"Pop", "pop", "D...d.u...u.d.u.", ART_NORMAL, 9},
    {"Island", "pop", "D...d.u...u.d.U.", ART_NORMAL, 8},
    {"Ballad", "ballad", "D.......d...d.u.", ART_RING, 13},
    {"Rock 8ths", "rock", "DdDdDdDd", ART_NORMAL, 7},
    {"Rock chug", "rock", "H.h.h.h.H.h.h.h.", ART_PALM, 4},
    {"Punk", "punk", "D.d.d.d.D.d.d.d.", ART_NORMAL, 5},
    {"Gallop", "metal", "H.hhH.hhH.hhH.hh", ART_PALM, 3},
    {"Metal 16ths", "metal", "HhhhHhhhHhhhHhhh", ART_PALM, 3},
    {"Funk", "funk", "D.xu.ux.D.xu.uxu", ART_STACCATO, 6},
    {"Funk 16ths", "funk", "DuxuduXudxxudUxu", ART_STACCATO, 5},
    {"Reggae", "reggae", "..x...U...x...U.", ART_STACCATO, 4},
    {"Ska", "reggae", "..U...U...U...U.", ART_STACCATO, 4},
    {"Shuffle", "rock", "D..dD..dD..dD..d", ART_NORMAL, 7},
    {"Arpeggio up", "fingerstyle", "0.1.2.3.4.5.4.3.", ART_RING, 0},
    {"Arpeggio 16ths", "fingerstyle", "0123432101234321", ART_RING, 0},
    {"Travis", "fingerstyle", "B.4.A.3.B.4.A.3.", ART_RING, 0},
    {"Travis pinch", "fingerstyle", "P.3.A.4.B.3.A.4.", ART_RING, 0},
    {"Ballad picking", "fingerstyle", "B.2.3.4.A.2.3.4.", ART_RING, 0},
    {"Bass + strum", "folk", "B...d.u.A...d.u.", ART_NORMAL, 8},
    /* more strums */
    {"Calypso", "pop", "D.DU.UDU", ART_NORMAL, 8},
    {"Push 3-3-2", "rock", "D..D..D.D..D..D.", ART_NORMAL, 7},
    {"Half-time", "ballad", "D.......D...U.d.", ART_RING, 11},
    {"Offbeat pushes", "pop", "D.....d...d..ud.", ART_NORMAL, 8},
    {"Disco scratch", "funk", "xDxuxDxuxDxuxDxu", ART_STACCATO, 5},
    {"Waltz feel", "folk", "B...d...d...A...", ART_NORMAL, 9},
    {"Chug + accent", "metal", "HhhhD...HhhhD...", ART_PALM, 3},
    {"Punk 16ths", "punk", "DdddDdddDdddDddd", ART_NORMAL, 4},
    {"Stop-time", "rock", "D.......D.....D.", ART_STACCATO, 6},
    /* more picking: arpeggios and rolls */
    {"Arpeggio down", "fingerstyle", "5.4.3.2.1.0.1.2.", ART_RING, 0},
    {"Arpeggio up-down 16ths", "fingerstyle", "0123454321012345", ART_RING, 0},
    {"Pima", "fingerstyle", "B.3.4.5.A.3.4.5.", ART_RING, 0},
    {"Outside-in", "fingerstyle", "0.5.1.4.2.3.1.4.", ART_RING, 0},
    {"Forward roll", "fingerstyle", "0.3.5.0.3.5.0.3.", ART_RING, 0},
    {"Backward roll", "fingerstyle", "5.3.0.5.3.0.5.3.", ART_RING, 0},
    {"Root-fifth picks", "fingerstyle", "0.2.4.2.0.2.4.2.", ART_RING, 0},
    {"Syncopated picking", "fingerstyle", "0..3..4.5..3..4.", ART_RING, 0},
    {"Cascade 16ths", "fingerstyle", "5432104354321043", ART_RING, 0},
    {"Pedal arpeggio", "fingerstyle", "0.4.0.5.0.4.0.3.", ART_RING, 0},
    {"Pinch + roll", "fingerstyle", "P.2.3.4.P.4.3.2.", ART_RING, 0},
    {"Muted arpeggio", "rock", "0.1.2.3.0.1.2.3.", ART_PALM, 0},
};
const int strum_pattern_count = (int)(sizeof strum_patterns / sizeof strum_patterns[0]);

int strum_find(const char *name)
{
	for (int i = 0; i < strum_pattern_count; i++)
		if (!strcmp(strum_patterns[i].name, name))
			return i;
	return -1;
}

static void add_strum(Song *s, int ch, u32 tick, u32 len, const u8 notes[6], i8 strum, u8 vel, u8 art)
{
	NoteEvent e = note_event();
	e.tick = tick;
	e.len = (u16)(len > 65535 ? 65535 : len < 1 ? 1 : len);
	e.ch = (u8)ch;
	e.vel = vel;
	e.fx = FX_STRUM;
	e.count = 6;
	e.art = art;
	e.strum = strum;
	memcpy(e.notes, notes, 6);
	song_add(s, e);
}

static void add_pick(Song *s, int ch, u32 tick, u32 len, int note, int string, u8 vel, u8 art)
{
	NoteEvent e = note_event();
	e.tick = tick;
	e.len = (u16)(len > 65535 ? 65535 : len < 1 ? 1 : len);
	e.ch = (u8)ch;
	e.vel = vel;
	e.count = 1;
	e.notes[0] = (u8)note;
	e.string = (u8)(string + 1);
	e.art = art;
	song_add(s, e);
}

int strum_write(Song *s, int ch, u32 t0, u32 ticks, const u8 notes[6], int strings, const StrumPattern *p, u8 vel,
                u8 art, bool ring)
{
	return strum_write_ex(s, ch, t0, ticks, notes, strings, p, vel, art, ring, true);
}

/* what a chord change landing on a rest plays: the pattern's own first
 * kind of hit (a down strum, the bass, the lowest picked string, a chug) */
static char attack_of(const char *code)
{
	for (const char *c = code; *c; c++)
		if (*c == 'D' || *c == 'd')
			return 'd';
	for (const char *c = code; *c; c++)
		if (*c == 'B' || *c == 'P')
			return 'B';
	for (const char *c = code; *c; c++)
		if (*c >= '0' && *c <= '5')
			return '0';
	for (const char *c = code; *c; c++)
		if (*c == 'H' || *c == 'h')
			return 'h';
	return 'd';
}

int strum_write_ex(Song *s, int ch, u32 t0, u32 ticks, const u8 notes[6], int strings, const StrumPattern *p, u8 vel,
                   u8 art, bool ring, bool attack_first)
{
	int sounding[6], ns = 0;
	for (int k = 0; k < strings && k < 6; k++)
		if (notes[k])
			sounding[ns++] = k;
	if (!ns || !ticks)
		return 0;
	const char *code = p->code;
	int steps = (int)strlen(code);
	u8 use_art = art != ART_NORMAL ? art : p->art;
	int added = 0;
	u32 step_ticks = (u32)ticks_per_row;
	u32 nsteps = (ticks + step_ticks - 1) / step_ticks;
	/* the pattern is locked to the bar: step k of the call is the song's
	 * 16th (t0 / 6 + k), so a chord change mid-bar keeps the groove */
	u32 s0 = t0 / step_ticks;
#define STEP(k) code[(s0 + (k)) % (u32)steps]
	for (u32 k = 0; k < nsteps; k++) {
		char c = STEP(k);
		if (c == '.' && k == 0 && attack_first)
			c = attack_of(code);
		if (c == '.')
			continue;
		u32 tick = t0 + k * step_ticks;
		/* length: to the next step that sounds (or the end) */
		u32 next = k + 1;
		while (next < nsteps && STEP(next) == '.')
			next++;
		u32 len = (next - k) * step_ticks;
		if (next >= nsteps || ring)
			len = t0 + ticks - tick;
		bool accent = isupper((unsigned char)c) || c == 'B' || c == 'P';
		int v = vel + (accent ? 12 : 0);
		u8 a = use_art;
		u8 sn[6] = {0};
		switch (c) {
		case 'D':
		case 'd':
			add_strum(s, ch, tick, len, notes, p->speed ? p->speed : 8, (u8)FT_CLAMP_I(v, 1, 127), a);
			added++;
			break;
		case 'U':
		case 'u': {
			/* an upstroke catches the top three or four strings */
			int from = ns > 4 ? ns - 4 : 0;
			for (int i = from; i < ns; i++)
				sn[sounding[i]] = notes[sounding[i]];
			add_strum(s, ch, tick, len, sn, (i8)-(p->speed ? p->speed : 8), (u8)FT_CLAMP_I(v - 10, 1, 127), a);
			added++;
			break;
		}
		case 'H':
		case 'h': {
			int to = ns < 3 ? ns : 3;
			for (int i = 0; i < to; i++)
				sn[sounding[i]] = notes[sounding[i]];
			add_strum(s, ch, tick, len, sn, p->speed ? p->speed : 3, (u8)FT_CLAMP_I(v, 1, 127), a);
			added++;
			break;
		}
		case 'x':
		case 'X':
			add_strum(s, ch, tick, 3, notes, 4, (u8)FT_CLAMP_I(v - 15, 1, 127), ART_DEAD);
			added++;
			break;
		case 'B':
		case 'A': {
			int i = c == 'B' ? 0 : (ns > 1 ? 1 : 0);
			add_pick(s, ch, tick, len, notes[sounding[i]], sounding[i], (u8)FT_CLAMP_I(v, 1, 127), a);
			added++;
			break;
		}
		case 'P':
			add_pick(s, ch, tick, len, notes[sounding[0]], sounding[0], (u8)FT_CLAMP_I(v, 1, 127), a);
			add_pick(s, ch, tick, len, notes[sounding[ns - 1]], sounding[ns - 1], (u8)FT_CLAMP_I(v - 6, 1, 127), a);
			added += 2;
			break;
		default:
			if (c >= '0' && c <= '5') {
				int i = c - '0';
				if (i >= ns)
					i = ns - 1 - (i - ns) % ns; /* fewer strings: fold back down */
				if (i < 0)
					i = 0;
				add_pick(s, ch, tick, len, notes[sounding[i]], sounding[i], (u8)FT_CLAMP_I(v - 4, 1, 127), a);
				added++;
			}
			break;
		}
	}
	return added;
#undef STEP
}
