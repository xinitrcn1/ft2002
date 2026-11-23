/* Procedural composer. Ported from geosim (src/client/audio/composer.cpp,
 * C++20) to C23. Every random draw, its order and every float operation of
 * the original are kept, so a seed and mood give the same song, bit for bit.
 *
 * docs/14-audio.md#composer (geosim) explains the musical rules; the short
 * version:
 *  - style: a family per song (techno, synthwave, amiga punk, chip, breaks,
 *    eurodance, lo-fi, hardtrance), weighted by mood; it sets the tempo,
 *    the groove templates, bass patterns and sounds;
 *  - form: intro / verse / chorus / bridge / breakdown / drop / outro, in
 *    8- and 16-bar sections, sized so the song lasts 2-3 minutes, sometimes
 *    with a key change for the last chorus;
 *  - harmony: one key and mode per song, 4-chord progressions (1 or 2 bars
 *    per chord) that may borrow chords (bVI, iv, V major, mediants...),
 *    realised by snapping notes into the chord's local scale; pad voicings
 *    chosen for smooth voice leading, suspensions;
 *  - melody: two 2-bar motifs per song (riff, legato, syncopated or anthem
 *    rhythm), placed on the harmony (strong beats snapped to chord tones),
 *    answered by varied copies (antecedent ends open, consequent resolves),
 *    by evolving arpeggios and by a counter voice; the lead's register is
 *    low, mid or high per song (wartime: low, almost a bass);
 *  - rhythm: groove templates per section group (verse / chorus), fills,
 *    rolls (16ths at most, one beat of 32nds) and drops at section boundaries;
 *  - intensity layers: optional events tagged with a layer the player
 *    enables from the live mood intensity. */
#include "composer.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define countof(a) (sizeof(a) / sizeof((a)[0]))

u64 next_seed(u64 seed)
{
	u64 z = seed + 0x9E3779B97F4A7C15ull;
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
	return z ^ (z >> 31);
}

/* --- deterministic random numbers ------------------------------------------- */

typedef struct {
	u64 s;
} Rng;

static Rng rng_make(u64 seed) { return (Rng){seed ^ 0x6A09E667F3BCC909ull}; }
static u64 rng_next(Rng *g)
{
	g->s = next_seed(g->s);
	return g->s;
}
static float rng_uniform(Rng *g) { return (float)(rng_next(g) >> 40) / 16777216.0f; } /* [0,1) */
static float rng_rangef(Rng *g, float lo, float hi) { return lo + (hi - lo) * rng_uniform(g); }
static int rng_range(Rng *g, int lo, int hi) { return lo + (int)(rng_next(g) % (u64)(hi - lo + 1)); } /* inclusive */
static bool rng_chance(Rng *g, float p) { return rng_uniform(g) < p; }
#define rng_pick(g, a) ((a)[rng_next(g) % countof(a)])

/* std::min / std::max / std::clamp, with their exact comparisons */
static inline int imin(int a, int b) { return b < a ? b : a; }
static inline int imax(int a, int b) { return a < b ? b : a; }
static inline int iclamp(int v, int lo, int hi) { return v < lo ? lo : hi < v ? hi : v; }
static inline float minf(float a, float b) { return b < a ? b : a; }
static inline float maxf(float a, float b) { return a < b ? b : a; }
static inline u8 to_u8(int v) { return (u8)iclamp(v, 0, 255); }

/* --- scales and harmony ------------------------------------------------------ */

typedef struct {
	const char *name;
	int steps[7];
	bool minor;
} ModeDef;
static const ModeDef modes[] = {
	{"ionian", {0, 2, 4, 5, 7, 9, 11}, false},     {"dorian", {0, 2, 3, 5, 7, 9, 10}, true},
	{"phrygian", {0, 1, 3, 5, 7, 8, 10}, true},    {"lydian", {0, 2, 4, 6, 7, 9, 11}, false},
	{"mixolydian", {0, 2, 4, 5, 7, 9, 10}, false}, {"aeolian", {0, 2, 3, 5, 7, 8, 10}, true},
	{"harmonic minor", {0, 2, 3, 5, 7, 8, 11}, true},
};
enum { M_IONIAN, M_DORIAN, M_PHRYGIAN, M_LYDIAN, M_MIXOLYDIAN, M_AEOLIAN, M_HARMONIC };
static const char *const note_names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

/* Chord colours: a chord outside the mode is written as the diatonic chord
 * on the same root degree plus a colour, a local scale (tonic offset, mode,
 * and the local degree of the chord, -1 = the same degree). Every tonal note
 * sounding in that chord's bars is snapped into the local scale, chord tones
 * first (docs/14-audio.md#harmony), so the melody logic stays diatonic while
 * the harmony borrows: bVI / bVII / iv from the parallel minor, V major from
 * harmonic minor, IV major from dorian, bII from phrygian, I major in minor
 * (picardy), and the chromatic mediants III and VI major in a major key. */
enum { C_HOME, C_MINOR, C_MAJOR, C_HARM, C_DORIAN, C_PHRYG, C_MED3, C_MED6, C_COUNT };
typedef struct {
	int offset; /* local tonic, semitones above the key */
	int mode;   /* mode id (-1: the song's mode) */
	int deg;    /* the chord's degree in the local scale (-1: its home degree) */
} ColorDef;
static const ColorDef colors[C_COUNT] = {{0, -1, -1},       {0, M_AEOLIAN, -1}, {0, M_IONIAN, -1}, {0, M_HARMONIC, -1},
                                         {0, M_DORIAN, -1}, {0, M_PHRYGIAN, -1}, {4, M_IONIAN, 0}, {9, M_MIXOLYDIAN, 0}};
static const char *const color_names[C_COUNT] = {"", "b", "M", "M", "M", "b", "III", "VI"};

/* Four-chord progressions as scale degrees (0 = tonic). Uncoloured chords
 * are diatonic triads of the modes in the mask that are major or minor (no
 * diminished chords); coloured ones are major or minor in their local scale. */
typedef struct {
	int mode_mask; /* bit per mode id it fits */
	int deg[4];
	i8 col[4];
	bool pivot; /* centred on the relative key: bridges and breakdowns */
} Progression;
static bool prog_colored(const Progression *p) { return p->col[0] | p->col[1] | p->col[2] | p->col[3]; }
#define BIT(m) (1 << (m))
#define MAJORISH (BIT(M_IONIAN) | BIT(M_MIXOLYDIAN) | BIT(M_LYDIAN))
#define MINORISH (BIT(M_AEOLIAN) | BIT(M_DORIAN) | BIT(M_PHRYGIAN) | BIT(M_HARMONIC))
static const Progression progressions[] = {
	{BIT(M_IONIAN), {0, 4, 5, 3}},                     /* I V vi IV */
	{BIT(M_IONIAN), {0, 3, 5, 4}},                     /* I IV vi V */
	{BIT(M_IONIAN), {5, 3, 0, 4}},                     /* vi IV I V */
	{BIT(M_IONIAN), {0, 5, 3, 4}},                     /* I vi IV V */
	{BIT(M_IONIAN) | BIT(M_MIXOLYDIAN), {0, 3, 0, 4}}, /* I IV I V (V = v in mixolydian) */
	{BIT(M_MIXOLYDIAN), {0, 6, 3, 0}},                 /* I bVII IV I */
	{BIT(M_MIXOLYDIAN), {0, 6, 0, 3}},                 /* I bVII I IV */
	{BIT(M_LYDIAN), {0, 1, 0, 1}},                     /* I II I II */
	{BIT(M_LYDIAN), {0, 1, 4, 0}},                     /* I II V I */
	{BIT(M_AEOLIAN), {0, 5, 2, 6}},                    /* i VI III VII */
	{BIT(M_AEOLIAN), {0, 0, 6, 5}},                    /* i i VII VI (the reference module's bass cycle) */
	{BIT(M_AEOLIAN), {0, 6, 5, 6}},                    /* i VII VI VII */
	{BIT(M_HARMONIC), {0, 5, 3, 4}},                   /* i VI iv V */
	{BIT(M_AEOLIAN) | BIT(M_DORIAN), {0, 3, 6, 2}},    /* i iv VII III (IV in dorian) */
	{BIT(M_DORIAN), {0, 3, 0, 3}},                     /* i IV i IV */
	{BIT(M_DORIAN), {0, 6, 3, 0}},                     /* i VII IV i */
	{BIT(M_PHRYGIAN), {0, 1, 0, 6}},                   /* i bII i bVII */
	{BIT(M_PHRYGIAN), {0, 1, 5, 1}},                   /* i bII bVI bII */
	{BIT(M_HARMONIC), {0, 5, 4, 4}},                   /* i VI V V */
	{BIT(M_HARMONIC), {0, 3, 4, 0}},                   /* i iv V i */
	/* borrowed and chromatic chords */
	{BIT(M_IONIAN) | BIT(M_MIXOLYDIAN) | BIT(M_LYDIAN), {0, 5, 6, 0}, {C_HOME, C_MINOR, C_MINOR, C_HOME}}, /* I bVI bVII I ("epic") */
	{BIT(M_IONIAN) | BIT(M_MIXOLYDIAN), {0, 3, 0, 4}, {C_HOME, C_MINOR, C_HOME, C_HOME}},                /* I iv I V */
	{BIT(M_IONIAN) | BIT(M_MIXOLYDIAN), {3, 3, 0, 0}, {C_HOME, C_MINOR, C_HOME, C_HOME}},                /* IV iv I I */
	{BIT(M_IONIAN), {0, 2, 5, 3}, {C_HOME, C_MED3, C_HOME, C_HOME}},                                     /* I III vi IV (mediant) */
	{BIT(M_IONIAN) | BIT(M_MIXOLYDIAN), {0, 5, 1, 4}, {C_HOME, C_MED6, C_HOME, C_HOME}},                 /* I VI ii V (secondary dominant) */
	{BIT(M_IONIAN), {5, 3, 0, 6}, {C_HOME, C_HOME, C_HOME, C_MINOR}},                                    /* vi IV I bVII */
	{BIT(M_IONIAN) | BIT(M_LYDIAN), {0, 4, 5, 2}, {C_HOME, C_HOME, C_HOME, C_MED3}},                     /* I V vi III */
	{BIT(M_IONIAN), {0, 6, 3, 0}, {C_HOME, C_MINOR, C_HOME, C_HOME}},                                    /* I bVII IV I */
	{BIT(M_AEOLIAN), {0, 6, 5, 4}, {C_HOME, C_HOME, C_HOME, C_HARM}},                                    /* i VII VI V (andalusian) */
	{BIT(M_AEOLIAN) | BIT(M_DORIAN), {0, 3, 4, 0}, {C_HOME, C_HOME, C_HARM, C_HOME}},                    /* i iv V i */
	{BIT(M_AEOLIAN), {0, 5, 3, 4}, {C_HOME, C_HOME, C_HOME, C_HARM}},                                    /* i VI iv V */
	{BIT(M_AEOLIAN), {0, 2, 6, 3}, {C_HOME, C_HOME, C_HOME, C_DORIAN}},                                  /* i III VII IV */
	{BIT(M_AEOLIAN), {5, 6, 0, 0}},                                                                      /* VI VII i i */
	{BIT(M_AEOLIAN), {0, 5, 6, 0}, {C_HOME, C_HOME, C_HOME, C_MAJOR}},                                   /* i VI VII I (picardy) */
	{BIT(M_AEOLIAN) | BIT(M_HARMONIC), {0, 5, 0, 1}, {C_HOME, C_HOME, C_HOME, C_PHRYG}},                 /* i VI i bII (neapolitan) */
	{BIT(M_AEOLIAN) | BIT(M_PHRYGIAN), {0, 3, 5, 4}, {C_HOME, C_HOME, C_HOME, C_HARM}},                  /* i iv VI V */
	/* relative-key centred (bridges and breakdowns) */
	{BIT(M_IONIAN), {5, 2, 3, 0}, {0}, true},                                               /* vi iii IV I */
	{BIT(M_IONIAN) | BIT(M_MIXOLYDIAN), {5, 1, 3, 4}, {0}, true},                           /* vi ii IV V */
	{BIT(M_IONIAN), {5, 3, 2, 2}, {C_HOME, C_HOME, C_HOME, C_MED3}, true},                  /* vi IV III III */
	{BIT(M_AEOLIAN) | BIT(M_DORIAN), {2, 6, 3, 2}, {0}, true},                              /* III VII iv III */
	{BIT(M_AEOLIAN), {2, 5, 6, 2}, {0}, true},                                              /* III VI VII III */
	{BIT(M_AEOLIAN) | BIT(M_HARMONIC), {5, 2, 3, 4}, {C_HOME, C_HOME, C_HOME, C_HARM}, true}, /* VI III iv V */
};
static_assert((MAJORISH & MINORISH) == 0);

typedef struct {
	int row; /* within the 2-bar cell, 0..31 */
	int len; /* rows */
	int deg; /* scale degree relative to the cell's first chord root */
} MotifNote;
typedef struct {
	MotifNote n[32];
	int count;
} Motif;

/* --- styles, grooves, bass patterns ------------------------------------------ */
/* A song's style family sets its tempo, grooves, bass patterns and sounds;
 * the mood picks the family (docs/14-audio.md#styles). */

enum { ST_TECHNO, ST_SYNTHWAVE, ST_PUNK, ST_CHIP, ST_BREAKS, ST_EURODANCE, ST_LOFI, ST_HARDTRANCE, ST_HARDSTYLE, ST_COUNT };
enum { G_FOUR, G_BROKEN, G_ELECTRO, G_BREAKBEAT, G_AMEN, G_HALFTIME, G_SHUFFLE, G_SYNTHWAVE, G_PUNK, G_DBEAT, G_CHIP, G_HARDTRANCE, G_TRANCE, G_HARDSTYLE, G_COUNT };
enum { B_OFFBEAT, B_ROLLING, B_GALLOP, B_OCTAVES, B_HOUSE, B_WALKING, B_SYNCOP, B_LONG, B_PEDAL16, B_BOUNCE, B_PULSE8, B_OCTAVE16, B_COUNT };
static const char *const bass_names[B_COUNT] = {"offbeat", "rolling", "gallop", "octaves", "house", "walking", "syncopated", "long", "pedal", "bounce", "eighths", "octave 16ths"};
enum { MS_RIFF, MS_LEGATO, MS_SYNCOP, MS_ANTHEM };
static const char *const mstyle_names[] = {"riff", "legato", "syncopated", "anthem"};

/* One drum pattern over a bar of 16ths (bit r = row r). */
typedef struct {
	const char *name;
	u16 kick, kick_alt;                /* kick_alt: the last bar of an 8-bar phrase */
	u16 snare, ghost;                  /* backbeat; ghost notes (an intensity layer) */
	u16 hat_closed, hat_open, hat_extra; /* hat_extra: an intensity layer */
	int swing;                         /* ticks on odd 16ths (-1: the song's own) */
} GrooveDef;
static const GrooveDef grooves[G_COUNT] = {
	{"four", 0x1111, 0x5111, 0x1010, 0x0000, 0x1111, 0x4444, 0xAAAA, -1},      /* four-on-the-floor */
	{"broken", 0x1041, 0x1441, 0x1010, 0x8000, 0x1111, 0x4444, 0xAAAA, -1},    /* 3-3-2 kick */
	{"electro", 0x0441, 0x0449, 0x1010, 0x0200, 0x5555, 0x0000, 0xAAAA, 0},    /* kick 1, 2a, 3+ */
	{"breakbeat", 0x0401, 0x0481, 0x1010, 0x8200, 0x1555, 0x4000, 0xAAAA, -1}, /* kick 1, 3+ */
	{"amen", 0x0C05, 0x0405, 0x1010, 0x8280, 0x5555, 0x0000, 0xAAAA, 0},       /* the break: kick 1, 1+, 3+, 3a */
	{"halftime", 0x0801, 0x0841, 0x0100, 0x4008, 0x5555, 0x0000, 0xAAAA, -1},  /* snare on 3 */
	{"shuffle", 0x0901, 0x0941, 0x1010, 0x8080, 0xFFFF, 0x0000, 0x0000, 2},    /* swung 16th hats */
	{"synthwave", 0x0101, 0x0501, 0x1010, 0x0000, 0x5555, 0x0000, 0xAAAA, 0},  /* kick 1 and 3, big snare */
	{"punk", 0x0101, 0x4101, 0x1010, 0x0000, 0x5555, 0x0000, 0x0000, 0},       /* kick-snare on the beats, 8th hats */
	{"d-beat", 0x0141, 0x4141, 0x1010, 0x0000, 0x5555, 0x0000, 0x0000, 0},     /* kick 1, 2+, 3 */
	{"chip", 0x0181, 0x0581, 0x1010, 0x8000, 0x5555, 0x0000, 0xAAAA, -1},      /* kick 1, 2e+, 3 */
	{"hardtrance", 0x1111, 0x9111, 0x1010, 0x0000, 0xBBBB, 0x4444, 0x0000, 0}, /* 16th hats, open off-beats, last-16th kick */
	{"trance", 0x1111, 0x5111, 0x1010, 0x0000, 0x0000, 0x4444, 0xBBBB, 0},     /* open off-beats only */
	/* hardstyle (the frustrated mood's reference mix): the kick on every beat carries
	 * the track, a 16th roll (rows 12, 14, 15) closes each phrase; open off-beat hats,
	 * clap backbeat, a 16th ride layer at high intensity */
	{"hardstyle", 0x1111, 0xD111, 0x1010, 0x0000, 0x0000, 0x4444, 0xAAAA, 0},
};

typedef struct {
	const char *name;
	float bpm_lo, bpm_hi;
	u8 groove_a[3]; /* intro / verse / bridge / outro candidates (the first one twice as likely) */
	u8 groove_b[3]; /* chorus / drop */
	u8 bass[4];
	float swing_p;
} StyleDef;
static const StyleDef styles[ST_COUNT] = {
	{"techno", 116, 128, {G_FOUR, G_BROKEN, G_ELECTRO}, {G_FOUR, G_ELECTRO, G_BROKEN}, {B_OFFBEAT, B_ROLLING, B_HOUSE, B_OCTAVES}, 0.3f},
	{"synthwave", 88, 108, {G_SYNTHWAVE, G_HALFTIME, G_FOUR}, {G_SYNTHWAVE, G_FOUR, G_SYNTHWAVE}, {B_PULSE8, B_PEDAL16, B_OCTAVES, B_PULSE8}, 0.0f},
	{"amiga punk", 152, 172, {G_PUNK, G_DBEAT, G_PUNK}, {G_DBEAT, G_PUNK, G_PUNK}, {B_PULSE8, B_WALKING, B_PULSE8, B_GALLOP}, 0.0f},
	{"chip", 124, 146, {G_CHIP, G_BREAKBEAT, G_FOUR}, {G_CHIP, G_FOUR, G_CHIP}, {B_OCTAVES, B_BOUNCE, B_WALKING, B_PULSE8}, 0.25f},
	{"breaks", 136, 156, {G_BREAKBEAT, G_AMEN, G_HALFTIME}, {G_AMEN, G_BREAKBEAT, G_AMEN}, {B_LONG, B_SYNCOP, B_HOUSE, B_LONG}, 0.2f},
	{"eurodance", 124, 136, {G_FOUR, G_CHIP, G_FOUR}, {G_FOUR, G_FOUR, G_FOUR}, {B_OFFBEAT, B_OCTAVE16, B_ROLLING, B_OCTAVES}, 0.0f},
	{"lo-fi", 82, 100, {G_HALFTIME, G_SHUFFLE, G_BREAKBEAT}, {G_SHUFFLE, G_HALFTIME, G_BREAKBEAT}, {B_LONG, B_WALKING, B_BOUNCE, B_LONG}, 0.7f},
	{"hardtrance", 138, 148, {G_HARDTRANCE, G_TRANCE, G_HARDTRANCE}, {G_HARDTRANCE, G_HARDTRANCE, G_HARDTRANCE}, {B_OFFBEAT, B_GALLOP, B_ROLLING, B_OFFBEAT}, 0.0f},
	/* 148-152 BPM (the reference's hardstyle stretches; uptempo 160-175 in choose_basics) */
	{"hardstyle", 148, 152, {G_HARDSTYLE, G_TRANCE, G_HARDSTYLE}, {G_HARDSTYLE, G_HARDSTYLE, G_HARDSTYLE}, {B_OFFBEAT, B_GALLOP, B_OFFBEAT, B_ROLLING}, 0.0f},
};

/* A bass rhythm over a bar: hits and the role of each hit. */
enum { BR_ROOT, BR_OCTAVE, BR_FIFTH, BR_BELOW, BR_THIRD, BR_APPROACH };
typedef struct {
	u16 mask;
	i8 role[16];
	bool legato; /* notes last until the next hit */
} BassPat;

/* Arpeggio orders over chord tones (0 root, 1 third, 2 fifth, 3 octave, 4 octave +
 * third, 5 octave + fifth; -1 ends a shorter order). */
constexpr int arp_order_count = 16;
static const int arp_order_table[arp_order_count][8] = {
	{0, 1, 2, 3, -1}, {3, 2, 1, 0, -1}, {0, 1, 2, 3, 2, 1, -1}, {0, 2, 1, 3, -1},
	{0, 2, 3, 1, 2, 3, 0, 2}, {0, 3, 1, 3, 2, 3, -1}, {0, 2, 4, 2, -1}, {2, 0, 3, 0, 4, 0, -1},
	{0, 1, 2, -1}, {0, 2, 3, 4, 3, -1}, {0, 2, 1, 3, 2, 4, 3, -1},
	/* added for variety: pendulums, a falling line, broken thirds, a pedal on the fifth */
	{0, 2, 4, 5, 4, 2, -1}, {5, 3, 2, 0, -1}, {0, 2, 1, 3, 2, 4, 3, 5}, {2, 0, 2, 1, 2, 3, 2, 4}, {1, 3, 2, 4, -1}};
/* Wide 4-note cells (degrees above the chord root, one cell per beat): the reference
 * track's first, then inversions and spreads of it. */
constexpr int wide_cell_count = 6;
static const int wide_cell_table[wide_cell_count][4] = {{11, 2, 7, 4}, {7, 0, 4, 11}, {9, 4, 7, 2}, {11, 4, 7, 0}, {14, 7, 4, 9}, {4, 9, 7, 11}};

typedef struct {
	int v[8];
	int n;
} ArpOrder;

static ArpOrder arp_order_of(int i)
{
	ArpOrder o = {0};
	for(int j = 0; j < 8; ++j) {
		if(arp_order_table[i][j] < 0) break;
		o.v[o.n++] = arp_order_table[i][j];
	}
	return o;
}
static bool arp_order_eq(const ArpOrder *a, const ArpOrder *b) { return a->n == b->n && memcmp(a->v, b->v, (size_t)a->n * sizeof a->v[0]) == 0; }

/* --- arrangement: section hand-overs -------------------------------------------- */
/* Section boundaries get one of several hand-overs, picked per seed and
 * per context, so a riser is one option among many (docs/14-audio.md#transitions). */
enum {
	TR_PLAIN,        /* straight cut, crash on the next downbeat */
	TR_FILL,         /* snare fill on the last 2 beats */
	TR_TOMS,         /* tom fill on the last beat */
	TR_ROLL,         /* snare roll build-up (quarters / 8ths, then 16ths) */
	TR_RISER,        /* noise riser (2 bars, 4 out of a breakdown) */
	TR_SHORT_RISER,  /* one-bar sweep up */
	TR_FILTER_CLOSE, /* bus low-pass closes over the last 2 bars */
	TR_FILTER_OPEN,  /* the section starts dark and the low-pass opens across it */
	TR_KICK_OUT,     /* the kick drops out for the last bar */
	TR_STOP,         /* everything but the lead stops for the last beats */
	TR_KICK_RETURN,  /* breakdown: the kick comes back for its last bars, with a roll */
	TR_DRUM_BREAK,   /* the melody, arp and chords drop out for the last 4 bars: drums and bass only */
	/* crescendo devices (builds into a chorus / drop, or out of a breakdown) */
	TR_CLAP_ROLL,   /* accelerating clap roll, crescendo */
	TR_TOM_CASCADE, /* descending tom cascades, 8ths then 16ths */
	TR_LAYER_IN,    /* breakdown / intro: hats, kick, bass, snare enter one by one */
	TR_HAT_RAMP,    /* hats densify and open: 8ths, 16ths, open 8ths, rising */
	TR_ARP_CLIMB,   /* the arpeggio speeds up and climbs an octave per bar */
	TR_STAB_RAMP,   /* chord stabs: quarters, 8ths, 16ths, rising */
	TR_PAD_SWELL,   /* one pad chord swells from silence over the build */
	TR_KICK_DOUBLE, /* the kick doubles: quarters to 8ths, then a 16th kick roll */
	TR_LEAD_CLIMB,  /* the melody ends early; the lead runs up a scale and bends into the downbeat */
	TR_PAUSE,       /* near-silent drop-out: everything stops for the last bar (a tension pause) */
	TR_COUNT
};
static const char *tr_name(u8 k)
{
	static const char *const names[] = {"cut",       "fill",      "toms",       "roll",       "riser",     "sweep",     "filter-close", "filter-open",
	                                    "kick-out",  "stop",      "kick-return", "drum-break", "clap-roll", "tom-cascade", "layer-in",   "hat-ramp",
	                                    "arp-climb", "stab-ramp", "pad-swell",  "kick-double", "lead-climb", "pause"};
	return k < countof(names) ? names[k] : "?";
}

/* --- the composer --------------------------------------------------------------- */

/* A section as planned: its kind plus the harmony it uses. */
typedef struct {
	Section sec;
	const Progression *prog;
	int bars_per_chord;
	int variant;    /* 0 first occurrence, 1 repeat (varied) */
	bool cadence;   /* last section: its last 2 bars are a cadence chord, then the tonic */
	bool end_tonic; /* the last bar's chord shares no tone with the tonic triad: it becomes the tonic */
	int shift;      /* semitones above the key (key change for the last chorus) */
	int pivot_deg;  /* >= 0: the last bar is the next key's dominant (degree and colour) */
	i8 pivot_col;
	/* How this section hands over to the next one (docs/14-audio.md#transitions). */
	u8 out;         /* transition kind (TR_*) */
	bool reverse;   /* reverse cymbal into the next downbeat */
	bool downlift;  /* downlifter at the start of the next section */
	bool impact;    /* impact boom at the start of the next section */
	u8 forced_out;  /* the epic form's fixed hand-overs (255: choose) */
	bool quiet;     /* intro of pads and arps only (no drums or bass) */
	bool build_up;  /* drums and bass enter, layers build (kick + bass, hats, backbeat, melody) */
	u8 orig_next;   /* the next section's kind before an arp solo was inserted (renders as before) */
} PlanItem;

static PlanItem plan_item(void) { return (PlanItem){.sec = {SEC_VERSE, 0, 8}, .bars_per_chord = 1, .pivot_deg = -1, .forced_out = 255, .orig_next = 255}; }

/* The harmony of one bar of the song, for snapping coloured chords. */
typedef struct {
	int root, col, shift;
	u16 scale, chord; /* pitch-class masks of the local scale and chord */
} BarInfo;

typedef struct {
	u32 from, to; /* ticks */
	u32 channels; /* bit per channel */
} Mute;

/* the lead's notes for the current phrase, fitted to the register before emission */
typedef struct {
	u32 bar;
	int row, len, note, vel;
	bool dbl;
	int cell; /* notes of one cell move together */
} LeadNote;

/* Bounds: a form has at most 8 + 2 * 24 sections plus an arp solo and a
 * choir block (58 <= FT_MAX_SECTIONS); a section adds at most 4 mutes; a
 * phrase holds at most 4 cells of 33 notes. */
#define MAX_PLAN FT_MAX_SECTIONS
#define MAX_MUTES (6 * MAX_PLAN)
#define MAX_PHRASE 256

typedef struct {
	Song *s;
	bool oom; /* an allocation failed (or a bound was hit): compose() fails */
	Rng r;
	Rng tr; /* arrangement choices (transitions, development); separate so the basics stay the same per seed */
	/* arp devices (the arp solo, the wide arp shapes): their own generator, so
	 * a seed keeps every other choice it made before they existed */
	Rng sr;
	/* arp variation (orders per section class, inversions, octave shifts, passing
	 * tones, wide cells): its own generator, so every other draw stays the same */
	Rng ar;
	Rng *jit;        /* the generator of add()'s velocity jitter (the solo's own while it renders) */
	bool war;        /* the club arrangement (wartime hardtrance, frustrated hardstyle) */
	bool low_lead;   /* wartime: the low, bass-like lead */
	bool force_solo; /* the main menu: always an arp solo (the draws stay the same) */
	int style;
	int key;
	const ModeDef *mode;
	const Progression *prog_a, *prog_b, *prog_c;
	int bpc_a, bpc_b;
	int swing; /* ticks added to odd rows (the current section's groove) */
	int song_swing;
	bool seventh; /* pads use 7th chords */
	bool power;   /* pads are power chords (root, fifth, octave): punk */
	u8 backbeat;
	u16 ghost_extra, kick_extra;
	int groove_a, groove_b;
	int bass_a, bass_b;
	BassPat bpat_a, bpat_b;
	bool bass_octave_layer;
	ArpOrder arp_order;
	int arp_rate, arp_len;
	/* Arp variation (choose_arp_variation): an order per section class (0 intro /
	 * verse / outro, 1 chorus / drop, 2 bridge / breakdown), a chord-tone rotation
	 * per 2-bar block and an octave shift per 4-bar block (both cycling over 4),
	 * passing tones closing 2-bar blocks, and two wide cells alternating every 8 bars. */
	ArpOrder arp_orders[3];
	int arp_inv[4];
	int arp_oct[4];
	bool arp_pass;
	int wide_cells[2][4];
	bool arp_cont; /* verses: a continuous evolving arp instead of 2-bar answers */
	u16 stab_mask;
	bool chip_chords;
	bool staccato;
	int style_a, style_b;
	Motif motif_a, motif_b;
	PlanItem plan[MAX_PLAN];
	int nplan;
	BarInfo *bars;
	u32 nbars;
	int last_voicing[4];
	int voicing_n;
	/* registers (MIDI notes) */
	int lead_class; /* 0 low, 1 mid, 2 high (wartime: low, bass-like) */
	int pad_lo, pad_hi, lead_lo, lead_hi, lead_base;
	int arp_base, ctr_lo, ctr_hi;
	int cad_deg; /* the final cadence chord (V, bVII or bII by mode) */
	i8 cad_col;
	/* the counter voice: 0 none, 1 guide tones, 2 guide tones + fills, 3 gated supersaw chords (wartime) */
	int counter_mode;
	bool counter_pulse;
	int last_ctr;
	bool euro_lead; /* the lead is the eurodance patch (boomerang.opus reference) */
	bool arp_wide;  /* after an arp solo, the arp keeps its wide cells, pedals and octave jumps */
	u32 solo_bar;   /* the arp solo's first bar (none: ~0) */
	bool no_jitter; /* add() without velocity jitter (events that must not draw random numbers) */
	bool epic;      /* the long form after the reference module */
	int mod_shift;  /* key change */
	u32 mod_bar;
	/* arrangement (tr) */
	int seq_step;  /* diatonic sequence step of the second cell */
	int dev_steps; /* development of the third cell in a repeated phrase */
	bool dev_invert;
	int cad_steps; /* the cadence cell's sequence step */
	int risers_left;
	bool hard_drop;  /* the drop switches to an off-beat bass (hardtrance) */
	bool beat_intro; /* the intro starts with kick, hats and bass */
	u16 gate_mask;   /* trance gate on breakdown pads (0 = none) */
	Mute mutes[MAX_MUTES];
	int nmutes;
	NoteEvent *post; /* device events added after the mutes */
	u32 npost, cap_post;
	char transitions[3072];
	size_t ntransitions;
	LeadNote phrase[MAX_PHRASE];
	int nphrase;
	int cell_id;
	int last_lead; /* the last lead note emitted (for smooth octave choices) */
} Composer;

static void composer_init(Composer *c, Song *s, u64 seed, Mood m, bool force_solo)
{
	memset(c, 0, sizeof *c);
	c->s = s;
	c->r = rng_make(seed);
	c->tr = rng_make(seed ^ 0xA54FF53A5F1D36F1ull);
	c->sr = rng_make(seed ^ 0x510E527FADE682D1ull);
	c->ar = rng_make(seed ^ 0x9B05688C2B3E6C1Full);
	c->jit = &c->r;
	c->force_solo = force_solo;
	c->style = ST_TECHNO;
	c->key = 57;
	c->mode = &modes[0];
	c->bpc_a = 2;
	c->bpc_b = 1;
	c->backbeat = DR_CLAP;
	c->groove_a = c->groove_b = G_FOUR;
	c->bass_a = c->bass_b = B_OFFBEAT;
	c->bass_octave_layer = true;
	c->arp_rate = c->arp_len = 1;
	for(int i = 0; i < 2; ++i) memcpy(c->wide_cells[i], wide_cell_table[0], sizeof c->wide_cells[i]);
	c->lead_class = 1;
	c->pad_lo = 52, c->pad_hi = 69, c->lead_lo = 64, c->lead_hi = 83, c->lead_base = 69;
	c->arp_base = 81, c->ctr_lo = 52, c->ctr_hi = 67;
	c->cad_deg = 4;
	c->cad_col = C_HOME;
	c->last_ctr = -1;
	c->solo_bar = ~0u;
	c->seq_step = 1;
	c->dev_steps = -2;
	c->cad_steps = 2;
	c->last_lead = -1;
	s->seed = seed;
	s->mood = m;
}

static bool big(int k) { return k == SEC_CHORUS || k == SEC_DROP; }

static void transitions_add(Composer *c, const char *str)
{
	const size_t n = strlen(str);
	if(c->ntransitions + n >= sizeof c->transitions) {
		c->oom = true;
		return;
	}
	memcpy(c->transitions + c->ntransitions, str, n + 1);
	c->ntransitions += n;
}

static void add_mute(Composer *c, u32 from, u32 to, u32 channels)
{
	if(c->nmutes == MAX_MUTES) {
		c->oom = true;
		return;
	}
	c->mutes[c->nmutes++] = (Mute){from, to, channels};
}

/* --- pitch helpers --------------------------------------------------------- */
static int pitch(const Composer *c, int base, int deg)
{
	const int oct = deg >= 0 ? deg / 7 : -((6 - deg) / 7);
	return base + 12 * oct + c->mode->steps[deg - 7 * oct];
}
/* Degrees (relative to the tonic) of the chord built on `root`. */
static bool is_chord_tone(int deg, int root)
{
	const int rel = ((deg - root) % 7 + 7) % 7;
	return rel == 0 || rel == 2 || rel == 4;
}
static int nearest_chord_tone(int deg, int root)
{
	for(int d = 0; d <= 3; ++d) {
		if(is_chord_tone(deg + d, root)) return deg + d;
		if(is_chord_tone(deg - d, root)) return deg - d;
	}
	return deg;
}
static int chord_at(const Composer *c, const PlanItem *p, int bar)
{
	const int bars = (int)p->sec.bars;
	if(p->pivot_deg >= 0 && bar == bars - 1) return p->pivot_deg;
	if(p->cadence && bar == bars - 2) return c->cad_deg;
	if((p->cadence || p->end_tonic) && bar >= bars - 1) return 0;
	return p->prog->deg[(bar / p->bars_per_chord) % 4];
}
static int color_at(const Composer *c, const PlanItem *p, int bar)
{
	const int bars = (int)p->sec.bars;
	if(p->pivot_deg >= 0 && bar == bars - 1) return p->pivot_col;
	if(p->cadence && bar == bars - 2) return c->cad_col;
	if((p->cadence || p->end_tonic) && bar >= bars - 1) return C_HOME;
	return p->prog->col[(bar / p->bars_per_chord) % 4];
}
/* Chord identity (root and colour): IV then iv are two chords. */
static int chord_id(const Composer *c, const PlanItem *p, int bar) { return chord_at(c, p, bar) * 16 + color_at(c, p, bar); }

/* Fills bars from the plan: the root, colour and key shift of every bar,
 * and the pitch-class masks used to snap notes in coloured bars. */
static void plan_bars(Composer *c)
{
	c->nbars = c->s->total_bars;
	c->bars = calloc(c->nbars ? c->nbars : 1, sizeof *c->bars);
	if(!c->bars) {
		c->oom = true;
		c->nbars = 0;
		return;
	}
	for(int i = 0; i < c->nplan; ++i) {
		const PlanItem *p = &c->plan[i];
		for(int b = 0; b < (int)p->sec.bars; ++b) {
			BarInfo *bi = &c->bars[p->sec.start_bar + (u32)b];
			bi->root = chord_at(c, p, b);
			bi->col = color_at(c, p, b);
			bi->shift = p->shift;
			const ColorDef *cd = &colors[bi->col];
			const ModeDef *md = cd->mode < 0 ? c->mode : &modes[cd->mode];
			const int tonic = c->key + cd->offset;
			const int ldeg = cd->deg >= 0 ? cd->deg : bi->root;
			for(int k = 0; k < 7; ++k) bi->scale = (u16)(bi->scale | (1u << ((tonic + md->steps[k]) % 12)));
			for(int k = 0; k < (c->seventh ? 4 : 3); ++k) {
				const int d = ((ldeg + 2 * k) % 7 + 7) % 7;
				bi->chord = (u16)(bi->chord | (1u << ((tonic + md->steps[d]) % 12)));
			}
		}
	}
}
static bool in_mask(u16 mask, int n) { return ((mask >> (((n % 12) + 12) % 12)) & 1u) != 0; }
/* A note of the home scale moved into the bar's local scale (coloured
 * chords): kept if it belongs, else a semitone to a chord tone, else down. */
static int snap(const Composer *c, int note, u32 bar)
{
	if(!c->nbars) return note;
	const BarInfo *b = &c->bars[bar < c->nbars - 1 ? bar : c->nbars - 1];
	if(b->col == C_HOME) return note;
	if(in_mask(b->scale, note)) return note;
	if(in_mask(b->chord, note - 1)) return note - 1;
	if(in_mask(b->chord, note + 1)) return note + 1;
	if(in_mask(b->scale, note - 1)) return note - 1;
	if(in_mask(b->scale, note + 1)) return note + 1;
	return note;
}
static int shift_at(const Composer *c, u32 bar) { return !c->nbars ? 0 : c->bars[bar < c->nbars - 1 ? bar : c->nbars - 1].shift; }

/* --- emission -------------------------------------------------------------- */
/* Tonal notes are written in the home key and realised here: snapped into
 * a coloured chord's scale and moved by the key change. A held lead or
 * counter note that crosses into a bar where it would sound different is
 * split there and re-articulated in the new harmony. */
static void push_event(Composer *c, NoteEvent e)
{
	if(!song_add(c->s, e)) c->oom = true;
}
static NoteEvent *last_event(Composer *c)
{
	static NoteEvent sink; /* only after a failed allocation with no events */
	return c->s->nevents ? &c->s->events[c->s->nevents - 1] : &sink;
}
static void realize(const Composer *c, NoteEvent *e, u32 bar, const int *orig)
{
	for(int i = 0; i < e->count; ++i) e->notes[i] = to_u8(snap(c, orig[i], bar) + shift_at(c, bar));
}
static void push_tonal(Composer *c, NoteEvent e, const int *orig)
{
	for(;;) {
		const u32 bar = e.tick / (u32)ticks_per_bar;
		NoteEvent out = e;
		realize(c, &out, bar, orig);
		bool split = false;
		if((e.ch == CH_LEAD || e.ch == CH_COUNTER) && e.fx == FX_NONE) {
			for(u32 b = bar + 1; b * (u32)ticks_per_bar < e.tick + e.len; ++b) {
				NoteEvent t = e;
				realize(c, &t, b, orig);
				if(memcmp(t.notes, out.notes, t.count) == 0) continue;
				const u32 cut = b * (u32)ticks_per_bar;
				out.len = (u16)(cut - e.tick);
				push_event(c, out);
				e.len = (u16)(e.tick + e.len - cut);
				e.tick = cut;
				split = true;
				break;
			}
		}
		if(!split) {
			push_event(c, out);
			return;
		}
	}
}
static void add_n(Composer *c, u32 bar, int row, int ch, const int *notes, int nn, int len_rows, int vel, u8 lay, NoteFx fx)
{
	NoteEvent e = note_event();
	int tick_in_bar = row * ticks_per_row;
	if((row & 1) && ch != CH_KICK) tick_in_bar += c->swing;
	e.tick = bar * (u32)ticks_per_bar + (u32)tick_in_bar;
	e.len = (u16)imax(1, len_rows * ticks_per_row - ((row & 1) ? c->swing : 0));
	e.ch = (u8)ch;
	e.vel = to_u8(iclamp(vel + (c->no_jitter ? 0 : rng_range(c->jit, -5, 5)), 1, 127));
	e.layer = lay;
	e.fx = fx;
	e.count = 0;
	int orig[4] = {0, 0, 0, 0};
	for(int i = 0; i < nn; ++i) {
		if(e.count < 4) {
			orig[e.count] = notes[i];
			e.notes[e.count++] = to_u8(notes[i]);
		}
	}
	if(c->s->patches[ch].inst == INST_KIT) push_event(c, e);
	else push_tonal(c, e, orig);
}
static void add1(Composer *c, u32 bar, int row, int ch, int note, int len_rows, int vel, u8 lay) { add_n(c, bar, row, ch, &note, 1, len_rows, vel, lay, FX_NONE); }
static void add_ticks(Composer *c, u32 tick, int ch, int note, int len_ticks, int vel)
{
	NoteEvent e = note_event();
	e.tick = tick;
	e.len = (u16)imax(1, len_ticks);
	e.ch = (u8)ch;
	e.vel = to_u8(iclamp(vel, 1, 127));
	e.layer = LAYER_ALWAYS;
	e.count = 1;
	e.notes[0] = to_u8(note);
	push_event(c, e);
}

/* weighted pick over n options */
static int pick_w(Rng *g, const float *w, int n)
{
	float total = 0;
	for(int i = 0; i < n; ++i) total += w[i];
	float x = rng_uniform(g) * total;
	for(int i = 0; i < n; ++i) {
		if(w[i] <= 0) continue;
		x -= w[i];
		if(x <= 0) return i;
	}
	for(int i = n - 1; i >= 0; --i) {
		if(w[i] > 0) return i;
	}
	return 0;
}

/* --- song-level choices ------------------------------------------------------ */
static void choose_style(Composer *c)
{
	/* weights per style (techno, synthwave, punk, chip, breaks, eurodance, lo-fi, hardtrance, hardstyle) */
	static const float w[MOOD_COUNT][ST_COUNT] = {
		{1.0f, 1.2f, 0.0f, 0.15f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f}, /* calm: mundane, relaxed (chip rare) */
		{1.2f, 1.4f, 0.6f, 0.15f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f}, /* tense */
		{1.0f, 1.0f, 1.2f, 0.1f, 1.3f, 0.0f, 0.0f, 0.0f, 0.0f},  /* crisis */
		{1.0f, 0.6f, 0.4f, 0.3f, 0.0f, 1.5f, 0.0f, 0.0f, 0.0f},  /* triumph: the upbeat one */
		{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f},  /* wartime */
		{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f},  /* frustrated */
	};
	c->style = pick_w(&c->r, w[c->s->mood], ST_COUNT);
	/* war: the club arrangement (trance forms, kick rolls, gated supersaw, off-beat
	 * drops), shared by hardtrance and hardstyle; low_lead: wartime's bass-like lead */
	c->low_lead = c->s->mood == MOOD_WARTIME;
	c->war = c->low_lead || c->style == ST_HARDSTYLE;
	snprintf(c->s->style, sizeof c->s->style, "%s", styles[c->style].name);
}

typedef struct {
	const Progression *plain[countof(progressions)], *colored[countof(progressions)], *pivot[countof(progressions)];
	int nplain, ncolored, npivot;
} ProgPools;

static const Progression *pick_prog(Composer *c, const ProgPools *pp, float colored_w)
{
	const float wp = (float)pp->nplain, wc = (float)pp->ncolored * colored_w;
	if(pp->ncolored && rng_uniform(&c->r) * (wp + wc) >= wp) return pp->colored[rng_next(&c->r) % (u64)pp->ncolored];
	if(pp->nplain) return pp->plain[rng_next(&c->r) % (u64)pp->nplain];
	return pp->colored[rng_next(&c->r) % (u64)pp->ncolored];
}

static void choose_basics(Composer *c)
{
	Song *s = c->s;
	const Mood m = s->mood;
	const StyleDef *S = &styles[c->style];
	/* Tempo from the style; calm stays relaxed, triumph stays upbeat. */
	float lo = S->bpm_lo, hi = S->bpm_hi;
	if(m == MOOD_CALM) hi = minf(hi, 120.0f), lo = minf(lo, hi - 6.0f);
	if(m == MOOD_TRIUMPH) lo = maxf(lo, 112.0f), hi = maxf(hi, lo + 6.0f);
	s->bpm = minf(hi, roundf(rng_rangef(&c->r, lo, hi + 0.99f))); /* (rounding could give hi + 1) */
	/* hardstyle: a quarter of the songs are uptempo (the reference mix's 170 BPM stretch) */
	if(c->style == ST_HARDSTYLE && rng_chance(&c->r, 0.25f)) s->bpm = roundf(rng_rangef(&c->r, 160.0f, 175.0f));
	c->key = 45 + rng_range(&c->r, 0, 11); /* tonic between A2 and G#3 */
	s->key = c->key;
	static const int calm_modes[] = {M_IONIAN, M_LYDIAN, M_MIXOLYDIAN, M_DORIAN, M_IONIAN};
	static const int tense_modes[] = {M_DORIAN, M_AEOLIAN, M_AEOLIAN, M_DORIAN, M_PHRYGIAN};
	static const int crisis_modes[] = {M_AEOLIAN, M_PHRYGIAN, M_HARMONIC, M_AEOLIAN};
	static const int triumph_modes[] = {M_IONIAN, M_MIXOLYDIAN, M_IONIAN, M_LYDIAN};
	static const int wartime_modes[] = {M_AEOLIAN, M_PHRYGIAN, M_HARMONIC, M_AEOLIAN, M_DORIAN};
	static const int frustrated_modes[] = {M_AEOLIAN, M_AEOLIAN, M_PHRYGIAN, M_HARMONIC}; /* the reference: A and C minor */
	int mode = M_IONIAN;
	switch(m) {
	case MOOD_CALM: mode = rng_pick(&c->r, calm_modes); break;
	case MOOD_TENSE: mode = rng_pick(&c->r, tense_modes); break;
	case MOOD_CRISIS: mode = rng_pick(&c->r, crisis_modes); break;
	case MOOD_TRIUMPH: mode = rng_pick(&c->r, triumph_modes); break;
	case MOOD_WARTIME: mode = rng_pick(&c->r, wartime_modes); break;
	case MOOD_FRUSTRATED: mode = rng_pick(&c->r, frustrated_modes); break;
	default: break;
	}
	s->mode = mode;
	c->mode = &modes[mode];

	/* Lead register, per song: low (baritone, under the pads), mid or high. */
	float cls_w[3] = {0.35f, 0.4f, 0.25f};
	if(m == MOOD_TRIUMPH) cls_w[0] = 0.2f, cls_w[2] = 0.35f;
	if(c->style == ST_CHIP) cls_w[2] += 0.15f;
	if(c->style == ST_BREAKS || c->style == ST_SYNTHWAVE) cls_w[0] += 0.1f;
	if(c->style == ST_PUNK) cls_w[1] += 0.2f;
	if(c->style == ST_HARDSTYLE) cls_w[0] = 0.0f, cls_w[1] = 0.6f, cls_w[2] = 0.4f; /* the screech sits at 400-1500 Hz */
	c->lead_class = c->low_lead ? 0 : pick_w(&c->r, cls_w, 3);
	/* wartime: almost a bass, an octave over the bass line; otherwise a
	 * 19-semitone window from A#2 (low), G3 (mid) or D4 (high) */
	static const int class_lo[3] = {46, 55, 62};
	c->lead_lo = (c->low_lead ? 42 : class_lo[c->lead_class]) + rng_range(&c->r, 0, 2);
	c->lead_hi = c->lead_lo + 19;
	c->lead_base = c->key + 12;
	while(c->lead_base < c->lead_lo - 1) c->lead_base += 12;
	while(c->lead_base > c->lead_lo + 10) c->lead_base -= 12;
	if(c->low_lead) c->pad_lo = 57; /* the supersaw over the low lead */
	else if(c->lead_class == 0) c->pad_lo = 58 + (c->key % 3); /* the chords over a low lead */
	else c->pad_lo = c->key + 7 <= 61 ? c->key + 7 : c->key - 5; /* D3-C#4, under the lead */
	c->pad_hi = c->pad_lo + 17;
	c->arp_base = c->lead_class == 2 || c->war ? c->key + 12 : c->key + 24;
	c->ctr_lo = c->low_lead ? 60 : c->lead_class == 0 ? 62 : 50 + (c->key % 3);
	c->ctr_hi = c->ctr_lo + 15;

	switch(mode) {
	case M_MIXOLYDIAN:
	case M_DORIAN:
	case M_AEOLIAN: c->cad_deg = 6; break;  /* bVII - i */
	case M_PHRYGIAN: c->cad_deg = 1; break; /* bII - i */
	default: c->cad_deg = 4; break;         /* V - I */
	}
	/* minor songs sometimes close on a major V (harmonic minor) instead */
	if((mode == M_AEOLIAN || mode == M_DORIAN) && rng_chance(&c->r, m == MOOD_CALM ? 0.15f : 0.4f)) c->cad_deg = 4, c->cad_col = C_HARM;
	/* Progressions: verse (A), chorus (B, coloured ones likelier outside
	 * calm: borrowed chords and mediants carry the emotion), bridge (C,
	 * often centred on the relative key). */
	ProgPools pp = {0};
	for(size_t i = 0; i < countof(progressions); ++i) {
		const Progression *p = &progressions[i];
		if(!(p->mode_mask & BIT(mode))) continue;
		if(p->pivot) pp.pivot[pp.npivot++] = p;
		else if(prog_colored(p)) pp.colored[pp.ncolored++] = p;
		else pp.plain[pp.nplain++] = p;
	}
	const float emo = m == MOOD_CALM ? 0.4f : 2.0f;
	c->prog_a = pick_prog(c, &pp, m == MOOD_CALM ? 0.3f : 0.8f);
	c->prog_b = pick_prog(c, &pp, emo);
	for(int tries = 0; tries < 4 && c->prog_b == c->prog_a; ++tries) c->prog_b = pick_prog(c, &pp, emo);
	c->prog_c = pp.npivot && rng_chance(&c->r, 0.6f) ? pp.pivot[rng_next(&c->r) % (u64)pp.npivot] : pick_prog(c, &pp, emo);
	c->bpc_a = rng_chance(&c->r, 0.6f) ? 2 : 1;
	c->bpc_b = rng_chance(&c->r, 0.6f) ? 1 : 2;
	if(c->style == ST_SYNTHWAVE || c->style == ST_LOFI) c->bpc_a = 2; /* slow songs: 2 bars a chord in verses */
	c->song_swing = !c->war && rng_chance(&c->r, styles[c->style].swing_p) ? 1 : 0;
	c->swing = c->song_swing;
	c->power = c->style == ST_PUNK;
	c->seventh = !c->power && ((m == MOOD_CALM || m == MOOD_TRIUMPH || c->style == ST_LOFI || c->style == ST_SYNTHWAVE) ? rng_chance(&c->r, 0.6f) : rng_chance(&c->r, 0.25f));
}

/* the hard kicks' end pitch: the key's root between 44 Hz and an octave up */
static float kick_root_hz(int key)
{
	int root = key - 24;
	while(440.0f * exp2f(((float)root - 69.0f) / 12.0f) < 44.0f) root += 12;
	return 440.0f * exp2f(((float)root - 69.0f) / 12.0f);
}

static void choose_instruments(Composer *c)
{
	Song *s = c->s;
	const Mood m = s->mood;
	Patch *P = s->patches;
	MixSettings *X = &s->mix;
	Rng *r = &c->r;
	/* drums */
	P[CH_KICK].inst = INST_KIT;
	P[CH_KICK].kick_pitch = rng_rangef(r, 130.0f, 190.0f);
	P[CH_KICK].kick_end = rng_rangef(r, 44.0f, 54.0f);
	P[CH_KICK].kick_decay = m == MOOD_CALM ? rng_rangef(r, 0.25f, 0.35f) : rng_rangef(r, 0.3f, 0.45f);
	P[CH_KICK].kick_click = rng_rangef(r, 0.2f, 0.7f);
	switch(c->style) {
	case ST_LOFI:
		P[CH_KICK].kick_pitch = rng_rangef(r, 110.0f, 140.0f);
		P[CH_KICK].kick_decay = rng_rangef(r, 0.22f, 0.3f);
		P[CH_KICK].kick_click = 0.2f;
		break;
	case ST_PUNK:
		P[CH_KICK].kick_pitch = rng_rangef(r, 150.0f, 190.0f);
		P[CH_KICK].kick_decay = rng_rangef(r, 0.18f, 0.26f);
		P[CH_KICK].kick_click = 0.7f;
		break;
	case ST_CHIP: P[CH_KICK].kick_decay = rng_rangef(r, 0.2f, 0.3f); break;
	case ST_HARDSTYLE: {
		/* the hardstyle kick (the reference: 46-75 % of the energy below 150 Hz in
		 * kick sections, sharp low-band transients): a punchy "tok" from ~380 Hz,
		 * a long pitched tail on the key's root, driven hard into distortion */
		Patch *k = &P[CH_KICK];
		k->kick_end = kick_root_hz(c->key);
		k->kick_pitch = rng_rangef(r, 340.0f, 420.0f);
		k->kick_sweep = rng_rangef(r, 0.02f, 0.035f);
		k->kick_decay = rng_rangef(r, 0.55f, 0.7f);
		k->kick_click = rng_rangef(r, 0.8f, 1.0f);
		k->kick_drive = rng_rangef(r, 5.0f, 7.0f);
		break;
	}
	case ST_HARDTRANCE: {
		/* the hardtrance kick: a hard, fast pitch drop from ~280 Hz onto
		 * the key's root (45-60 Hz), long body, driven into distortion */
		Patch *k = &P[CH_KICK];
		k->kick_end = kick_root_hz(c->key);
		k->kick_pitch = rng_rangef(r, 250.0f, 320.0f);
		k->kick_sweep = rng_rangef(r, 0.012f, 0.02f);
		k->kick_decay = rng_rangef(r, 0.42f, 0.55f);
		k->kick_click = rng_rangef(r, 0.6f, 0.9f);
		k->kick_drive = rng_rangef(r, 3.0f, 4.2f);
		break;
	}
	default: break;
	}
	P[CH_SNARE].inst = INST_KIT;
	P[CH_SNARE].snare_tone = rng_rangef(r, 170.0f, 230.0f);
	P[CH_SNARE].snare_decay = rng_rangef(r, 0.12f, 0.2f);
	if(c->style == ST_SYNTHWAVE) { /* big, gated-reverb snare */
		P[CH_SNARE].snare_decay = rng_rangef(r, 0.28f, 0.36f);
		P[CH_SNARE].snare_tone = rng_rangef(r, 160.0f, 190.0f);
	}
	if(c->style == ST_PUNK) {
		P[CH_SNARE].snare_decay = rng_rangef(r, 0.12f, 0.16f);
		P[CH_SNARE].snare_tone = rng_rangef(r, 200.0f, 240.0f);
	}
	if(c->style == ST_BREAKS) P[CH_SNARE].snare_tone = rng_rangef(r, 210.0f, 250.0f);
	P[CH_HATS].inst = INST_KIT;
	P[CH_HATS].hat_decay = rng_rangef(r, 0.03f, 0.06f);
	P[CH_HATS].hat_tone = rng_rangef(r, 0.8f, 1.25f);
	P[CH_FX].inst = INST_KIT;
	P[CH_FX].mono = false;

	/* bass */
	Patch *b = &P[CH_BASS];
	int bass_kind = rng_range(r, 0, 3);
	switch(c->style) {
	case ST_SYNTHWAVE: bass_kind = rng_chance(r, 0.6f) ? 3 : 1; break;
	case ST_PUNK:
	case ST_CHIP: bass_kind = rng_chance(r, 0.6f) ? 1 : 2; break;
	case ST_BREAKS: bass_kind = 4; break;
	case ST_LOFI: bass_kind = rng_chance(r, 0.6f) ? 0 : 2; break;
	case ST_HARDTRANCE: bass_kind = rng_range(r, 2, 3); break;
	case ST_HARDSTYLE: bass_kind = rng_range(r, 2, 3); break; /* the off-beat "reverse bass": driven saws */
	default:
		if(m == MOOD_CALM) bass_kind = rng_range(r, 0, 2);
		break;
	}
	/* Sounds aim at late-90s / early-2000s analogue-modelling synths
	 * (JP-8000 supersaws, Virus / Nord Lead style leads and basses, warm
	 * filtered pads, rounded plucks): band-limited oscillators, unison,
	 * 24 dB low-passes with envelopes, soft saturation, chorus. No
	 * bit-crushing and no 8-bit wavetables (docs/14-audio.md#synthesis). */
	b->glide = 0.0f;
	b->poles4 = true;
	switch(bass_kind) {
	case 0: /* warm triangle + sine sub */
		b->w1 = WAVE_TRIANGLE, b->w2 = WAVE_SAW, b->mix2 = 0.25f, b->sub = 0.6f;
		b->cutoff = 800, b->resonance = 0.1f, b->fenv = 1.5f, b->fdecay = 0.12f;
		break;
	case 1: /* analogue square bass */
		b->w1 = WAVE_PULSE, b->duty = 0.5f, b->w2 = WAVE_PULSE, b->mix2 = 0.0f, b->sub = 0.5f;
		b->cutoff = 700, b->resonance = 0.2f, b->fenv = 1.8f, b->fdecay = 0.12f, b->drive = c->style == ST_PUNK ? 0.45f : 0.2f;
		break;
	case 2: /* Virus-style saw bass: two saws, sub, envelope on a 24 dB filter */
		b->w1 = WAVE_SAW, b->w2 = WAVE_SAW, b->mix2 = 0.8f, b->detune = 5, b->sub = 0.5f;
		b->cutoff = 600, b->resonance = 0.25f, b->fenv = 2.2f, b->fdecay = 0.18f, b->drive = 0.2f;
		break;
	case 3: /* resonant saw (acid-lite) */
		b->w1 = WAVE_SAW, b->w2 = WAVE_SAW, b->mix2 = 0.5f, b->detune = 7, b->sub = 0.4f;
		b->cutoff = 500;
		b->resonance = c->style == ST_SYNTHWAVE ? 0.3f : rng_rangef(r, 0.45f, 0.65f);
		b->fenv = 3.0f, b->fdecay = 0.14f, b->drive = 0.4f;
		break;
	default: /* reese: two detuned saws, dark, held */
		b->w1 = WAVE_SAW, b->w2 = WAVE_SAW, b->mix2 = 1.0f;
		b->detune = rng_rangef(r, 12.0f, 18.0f);
		b->sub = 0.6f;
		b->cutoff = 380, b->resonance = 0.2f, b->fenv = 1.2f, b->fdecay = 0.3f, b->drive = 0.3f;
		break;
	}
	b->attack = 0.003f, b->decay = 0.25f, b->sustain = bass_kind == 4 ? 0.85f : 0.6f, b->release = bass_kind == 4 ? 0.12f : 0.06f, b->key_track = 0.3f;

	/* lead */
	Patch *l = &P[CH_LEAD];
	l->poles4 = true;
	if(c->low_lead) {
		/* wartime: a low, almost bass-like lead instead of a high synth */
		switch(rng_range(r, 0, 2)) {
		case 0: /* reese: saws a quarter-tone apart, driven */
			l->w1 = WAVE_SAW, l->unison = 2, l->spread = 22, l->w2 = WAVE_SAW, l->mix2 = 0.8f;
			l->detune = rng_rangef(r, 20.0f, 30.0f);
			l->sub = 0.25f;
			l->cutoff = 900, l->fenv = 2.2f, l->fdecay = 0.3f, l->resonance = 0.3f, l->drive = 0.25f, l->key_track = 0.4f;
			l->gain = 2.1f;
			break;
		case 1: /* hoover-ish: PWM pulse against a detuned saw, gliding */
			l->w1 = WAVE_PULSE, l->duty = 0.3f, l->pwm_depth = 0.2f, l->pwm_rate = 0.9f, l->w2 = WAVE_SAW, l->mix2 = 0.8f, l->detune = 15;
			l->cutoff = 1400, l->fenv = 1.4f, l->fdecay = 0.25f, l->resonance = 0.2f, l->drive = 0.3f, l->key_track = 0.4f;
			l->gain = 1.6f;
			break;
		default: /* acid: one resonant saw, filter envelope, slides */
			l->w1 = WAVE_SAW, l->w2 = WAVE_SAW, l->mix2 = 0.3f, l->detune = 6;
			l->cutoff = 520, l->fenv = 3.2f, l->fdecay = 0.18f, l->resonance = 0.65f, l->drive = 0.5f, l->key_track = 0.3f;
			l->gain = 2.0f;
			break;
		}
		l->attack = 0.005f, l->decay = 0.3f, l->sustain = 0.8f, l->release = 0.12f;
		l->vib_depth = rng_rangef(r, 0.0f, 0.06f);
		l->vib_rate = 5.0f, l->vib_delay = 0.3f;
		l->glide = rng_chance(r, 0.7f) ? 0.05f : 0.0f;
	} else {
		static const int prefs[ST_COUNT][4] = {{0, 1, 2, 3}, {1, 0, 1, 2}, {2, 0, 1, 3}, {0, 2, 3, 1}, {1, 0, 2, 1}, {1, 0, 2, 3}, {0, 2, 3, 0}, {1, 1, 1, 1}, {1, 2, 1, 2}};
		/* 7aa4c09's table: its "chip sample" lead (kind 2) drew one more number */
		static const int prefs_7aa[ST_COUNT][4] = {{0, 1, 2, 3}, {1, 0, 1, 3}, {3, 0, 1, 3}, {2, 0, 2, 3}, {1, 0, 3, 1}, {3, 0, 2, 1}, {0, 2, 3, 0}, {1, 1, 1, 1}, {1, 1, 1, 1}};
		const int pick = rng_range(r, 0, 3);
		const int lead_kind = prefs[c->style][pick];
		const bool old_draw = prefs_7aa[c->style][pick] == 2;
		switch(lead_kind) {
		case 0: /* PWM pulse pair, warm */
			l->w1 = WAVE_PULSE, l->w2 = WAVE_PULSE, l->mix2 = 0.4f, l->detune = 9, l->duty = 0.35f, l->pwm_depth = 0.12f, l->pwm_rate = 0.7f;
			l->cutoff = 2200, l->fenv = 1.0f, l->resonance = 0.15f;
			l->gain = 1.15f;
			break;
		case 1: /* supersaw lead (JP-8000 style) */
			l->w1 = WAVE_SAW, l->unison = 2, l->spread = 18, l->w2 = WAVE_SAW, l->mix2 = 0.5f, l->detune = 7;
			l->cutoff = 2300, l->fenv = 1.2f, l->resonance = 0.2f;
			l->gain = 2.0f;
			break;
		case 2: /* Nord-style lead: saw with a quiet square an octave up, resonant sweep, glide */
			l->w1 = WAVE_SAW, l->w2 = WAVE_PULSE, l->duty = 0.5f, l->mix2 = 0.25f, l->semi2 = 12;
			l->cutoff = 1800, l->fenv = 1.6f, l->fdecay = 0.35f, l->resonance = 0.35f, l->drive = 0.15f;
			l->gain = 1.6f;
			break;
		default: /* square + fifth, organ-like, filtered */
			l->w1 = WAVE_PULSE, l->duty = 0.5f, l->w2 = WAVE_TRIANGLE, l->mix2 = 0.5f, l->semi2 = 7;
			l->cutoff = 1800, l->fenv = 0.8f;
			l->gain = 1.15f;
			break;
		}
		if(old_draw) (void)rng_chance(r, 0.5f); /* was the wavetable choice: kept so later draws stay the same */
		if(c->lead_class == 0) /* a low lead keeps its edge (unison saws need no drive) */
			l->cutoff *= 1.4f, l->key_track = 0.3f, l->drive = l->unison ? l->drive : maxf(l->drive, 0.15f);
		l->attack = 0.015f, l->decay = 0.3f, l->sustain = 0.65f, l->release = 0.15f;
		l->vib_depth = rng_rangef(r, 0.05f, 0.12f);
		l->vib_rate = rng_rangef(r, 4.5f, 5.5f);
		l->vib_delay = 0.25f;
		l->glide = rng_chance(r, lead_kind == 2 ? 0.8f : 0.5f) ? 0.045f : 0.0f;
		if(c->style == ST_HARDSTYLE) {
			/* hardstyle leads (the reference's lead stretches: ~46 % of the energy at
			 * 400-1500 Hz): a driven, resonant "screech" with slides, or a wide
			 * euphoric supersaw with a fast pitch envelope feel (glide) */
			if(lead_kind == 2) { /* screech: saw + square an octave up, high resonance, hard drive */
				l->w1 = WAVE_SAW, l->w2 = WAVE_PULSE, l->duty = 0.3f, l->mix2 = 0.45f, l->semi2 = 12;
				l->cutoff = 1500, l->fenv = 2.4f, l->fdecay = 0.22f, l->resonance = 0.62f, l->drive = 0.7f, l->key_track = 0.5f;
				l->gain = 1.5f;
			} else { /* euphoric supersaw */
				l->unison = 3, l->spread = 26, l->detune = 9, l->cutoff = 2600, l->fenv = 1.4f, l->drive = 0.2f;
				l->gain = 1.9f;
			}
			l->glide = 0.06f;
			l->attack = 0.004f, l->sustain = 0.8f, l->release = 0.1f;
		}
	}
	l->fdecay = maxf(l->fdecay, 0.25f);
	c->staccato = rng_chance(r, c->war ? 0.2f : m == MOOD_CRISIS ? 0.2f : 0.1f);

	/* arp: rounded plucks (a filter envelope on a 24 dB low-pass), or
	 * (slower rates) short sustained notes */
	Patch *a = &P[CH_ARP];
	const int arp_kind = c->style == ST_CHIP ? 2 : rng_range(r, 0, 2); /* (the draws keep 7aa4c09's order: same seed, same song) */
	a->w1 = arp_kind == 0 ? WAVE_PULSE : WAVE_SAW;
	a->duty = rng_chance(r, 0.5f) ? 0.5f : 0.35f;
	if(arp_kind == 2) a->unison = 2, a->spread = 14; /* trance pluck */
	a->w2 = WAVE_PULSE, a->mix2 = 0.0f;
	a->attack = 0.003f;
	a->decay = rng_rangef(r, 0.12f, 0.2f);
	a->sustain = 0.0f, a->release = 0.08f;
	a->cutoff = 900, a->fenv = 2.6f, a->fdecay = 0.12f, a->resonance = 0.25f, a->poles4 = true;
	a->gain = arp_kind == 2 ? 1.5f : 1.1f;

	/* pad / chords */
	Patch *p = &P[CH_PAD];
	p->poles4 = true;
	/* tracker-arpeggio chords only in the (rare) chip family
	 * 7aa4c09 drew chip chords for many families; the same draw now only
	 * keeps them in the chip family (and keeps every later draw in place) */
	float chip_p = m == MOOD_CALM ? 0.25f : 0.4f;
	if(c->style == ST_CHIP) chip_p = 0.75f;
	if(c->style == ST_SYNTHWAVE || c->style == ST_PUNK || c->war) chip_p = 0.0f;
	if(c->style == ST_BREAKS) chip_p = 0.15f;
	const bool old_chip = rng_chance(r, chip_p);
	c->chip_chords = old_chip && c->style == ST_CHIP;
	if(c->chip_chords) { /* arpeggiated chord on a soft square */
		p->w1 = WAVE_PULSE, p->duty = 0.5f, p->w2 = WAVE_PULSE, p->mix2 = 0.0f;
		p->cutoff = 1600, p->resonance = 0.1f;
		p->attack = 0.008f, p->decay = 0.3f, p->sustain = 0.5f, p->release = 0.25f;
	} else if(c->power) { /* a sampled-synth "guitar": driven saw + pulse power chords, chugs in the chorus */
		p->w1 = WAVE_SAW, p->unison = 1, p->spread = 10, p->w2 = WAVE_PULSE, p->duty = 0.4f, p->mix2 = 0.6f, p->detune = 6;
		p->cutoff = 1800, p->resonance = 0.15f, p->fenv = 0.8f, p->fdecay = 0.2f, p->drive = 0.45f;
		if(!old_chip) (void)rng_chance(r, 0.5f); /* was the bit-crush choice: kept so later draws stay the same */
		p->attack = 0.004f, p->decay = 0.3f, p->sustain = 0.75f, p->release = 0.12f;
	} else { /* supersaw pad: unison saws, warm filter, chorus in the mixer */
		p->w1 = WAVE_SAW, p->unison = 2, p->spread = 16, p->w2 = WAVE_SAW, p->mix2 = 0.7f;
		p->detune = old_chip ? 11.0f : rng_rangef(r, 8.0f, 14.0f);
		p->cutoff = m == MOOD_CRISIS ? 900.0f : 1300.0f, p->resonance = 0.15f, p->fenv = 0.6f, p->fdecay = 0.6f;
		p->attack = 0.08f, p->decay = 0.6f, p->sustain = 0.8f, p->release = 0.6f;
		p->gain = 1.3f;
		if(c->war) { /* the JP-8000 supersaw: wide and bright, for pads and stabs */
			p->unison = 4, p->spread = 28;
			p->mix2 = 0.6f;
			p->cutoff = 2100.0f;
			p->fenv = 1.0f;
			p->gain = 1.5f;
		}
		if(c->style == ST_SYNTHWAVE) { /* lush */
			p->unison = 3, p->spread = 18;
			p->detune = rng_rangef(r, 10.0f, 16.0f);
			p->cutoff = 1600.0f, p->attack = 0.25f, p->release = 0.9f, p->sustain = 0.85f;
		}
		p->pwm_depth = 0.0f;
	}
	p->mono = true;
	p->key_track = 0.2f;

	/* counter voice */
	Patch *ct = &P[CH_COUNTER];
	ct->poles4 = true;
	int ctr_kind = 0; /* 0 strings, 1 brass, 2 soft square, 3 supersaw */
	if(c->war) ctr_kind = 3;
	else if(c->style == ST_CHIP || c->style == ST_EURODANCE) ctr_kind = 2;
	else if(c->style == ST_PUNK || c->style == ST_BREAKS || (c->style == ST_TECHNO && m != MOOD_CALM)) ctr_kind = rng_chance(r, 0.6f) ? 1 : 0;
	switch(ctr_kind) {
	case 0: /* string machine */
		ct->w1 = WAVE_SAW, ct->unison = 1, ct->spread = 12, ct->w2 = WAVE_SAW, ct->mix2 = 0.8f, ct->detune = 7;
		ct->attack = 0.2f, ct->decay = 0.5f, ct->sustain = 0.85f, ct->release = 0.5f, ct->cutoff = 1400, ct->fenv = 0.3f, ct->fdecay = 0.5f;
		ct->vib_depth = 0.04f, ct->vib_rate = 5.0f, ct->vib_delay = 0.3f, ct->gain = 1.7f;
		break;
	case 1: /* synth brass */
		ct->w1 = WAVE_SAW, ct->w2 = WAVE_PULSE, ct->duty = 0.4f, ct->mix2 = 0.6f, ct->detune = 5;
		ct->attack = 0.04f, ct->decay = 0.3f, ct->sustain = 0.7f, ct->release = 0.18f, ct->cutoff = 900, ct->fenv = 1.6f, ct->fdecay = 0.25f, ct->drive = 0.15f;
		ct->gain = 1.4f;
		break;
	case 2: /* soft square with a triangle an octave up */
		ct->w1 = WAVE_PULSE, ct->duty = 0.5f, ct->w2 = WAVE_TRIANGLE, ct->semi2 = 12, ct->mix2 = 0.3f;
		ct->attack = 0.012f, ct->decay = 0.25f, ct->sustain = 0.6f, ct->release = 0.15f, ct->cutoff = 1600, ct->fenv = 0.5f;
		ct->vib_depth = 0.06f, ct->vib_rate = 5.0f, ct->vib_delay = 0.3f, ct->gain = 1.1f;
		break;
	default: /* supersaw for the gated wartime chords */
		ct->w1 = WAVE_SAW, ct->unison = 3;
		ct->spread = 0.6f * rng_rangef(r, 22.0f, 30.0f); /* 13-18 cents: wide but in tune */
		ct->w2 = WAVE_SAW, ct->mix2 = 0.5f, ct->detune = 7;
		ct->attack = 0.004f, ct->decay = 0.2f, ct->sustain = 0.85f, ct->release = 0.07f, ct->cutoff = 2400, ct->resonance = 0.1f, ct->fenv = 0.5f, ct->fdecay = 0.2f;
		ct->gain = 1.8f;
		break;
	}
	ct->key_track = 0.3f;
	ct->mono = true;
	if(c->war) c->counter_mode = 3;
	else if(m == MOOD_CALM) c->counter_mode = rng_chance(r, 0.5f) ? 1 : 0;
	else c->counter_mode = rng_chance(r, 0.55f) ? 2 : 1;
	c->counter_pulse = m != MOOD_CALM && rng_chance(r, 0.4f);

	/* mix (levels tuned by ear on offline renders, see docs/14-audio.md) */
	const float g[CH_COUNT] = {c->war ? 0.8f : 0.72f, 0.8f, 0.45f, 0.62f, c->lead_class == 0 ? 0.42f : 0.36f, 0.45f, c->chip_chords ? 0.32f : 0.45f, 0.4f, c->war ? 0.3f : 0.28f, 0.36f};
	static const float pan[CH_COUNT] = {0.0f, 0.05f, 0.3f, 0.0f, 0.15f, -0.4f, -0.05f, 0.0f, -0.25f, 0.0f};
	static const float dly[CH_COUNT] = {0.0f, 0.04f, 0.06f, 0.0f, 0.28f, 0.32f, 0.12f, 0.1f, 0.18f, 0.1f};
	static const float rev[CH_COUNT] = {0.02f, 0.18f, 0.06f, 0.0f, 0.2f, 0.15f, 0.35f, 0.35f, 0.3f, 0.45f};
	const float duck[CH_COUNT] = {0.0f, 0.0f, 0.15f, 0.55f, c->war ? 0.35f : 0.15f, 0.3f, 0.6f, 0.0f, c->war ? 0.6f : 0.35f, 0.3f};
	for(int ch = 0; ch < CH_COUNT; ++ch) {
		X->gain[ch] = g[ch];
		X->pan[ch] = ch == CH_ARP && rng_chance(r, 0.5f) ? -pan[ch] : pan[ch];
		X->delay[ch] = dly[ch];
		X->reverb[ch] = rev[ch] * (m == MOOD_CALM ? 1.3f : 1.0f);
		X->duck[ch] = duck[ch];
	}
	if(X->pan[CH_ARP] > 0) X->pan[CH_LEAD] = -X->pan[CH_LEAD];
	X->pan[CH_COUNTER] = -0.8f * X->pan[CH_LEAD];
	if(c->style == ST_SYNTHWAVE) X->reverb[CH_SNARE] = 0.45f, X->reverb[CH_PAD] = 0.45f;
	/* ensemble chorus on the chords and the counter voice, a touch on the arp */
	X->chorus[CH_PAD] = c->chip_chords ? 0.2f : c->style == ST_SYNTHWAVE ? 0.7f : 0.5f;
	X->chorus[CH_COUNTER] = ctr_kind == 0 ? 0.6f : 0.35f;
	X->chorus[CH_ARP] = 0.2f;
	/* the low end belongs to the kick and bass: high-pass the chords,
	 * arp and counter, and the lead below its lowest notes */
	X->highpass[CH_PAD] = 150.0f;
	X->highpass[CH_ARP] = 180.0f;
	X->highpass[CH_COUNTER] = 140.0f;
	X->highpass[CH_LEAD] = c->low_lead ? 70.0f : 110.0f;
	X->highpass[CH_CHOIR] = 180.0f;
	X->chorus[CH_CHOIR] = 0.6f;
	X->bright[CH_CHOIR] = true;
	/* the choir (used only by a choir block; no random draws): unison
	 * saws through "aah" formants, slow attack, gentle vibrato */
	Patch *ch = &P[CH_CHOIR];
	ch->w1 = WAVE_SAW, ch->unison = 2, ch->spread = 9, ch->w2 = WAVE_SAW, ch->mix2 = 0.7f, ch->detune = 6;
	ch->formant = 1, ch->poles4 = true, ch->cutoff = 5000, ch->resonance = 0.0f, ch->fenv = 0.0f, ch->key_track = 0.0f;
	ch->attack = 0.35f, ch->decay = 0.5f, ch->sustain = 0.9f, ch->release = 0.8f;
	ch->vib_depth = 0.12f, ch->vib_rate = 5.2f, ch->vib_delay = 0.2f;
	ch->gain = 1.6f; /* featured, but under the kick: about -17 dBFS on its own in the choir drop */
	ch->mono = true;
	X->bright[CH_LEAD] = X->bright[CH_ARP] = X->bright[CH_PAD] = X->bright[CH_COUNTER] = true;
	X->delay_rows = rng_chance(r, 0.7f) ? 3.0f : 2.0f;
	X->delay_feedback = rng_rangef(r, 0.3f, 0.45f);
	X->reverb_size = m == MOOD_CALM ? 0.85f : 0.75f;
}

static void bass_set(BassPat *b, const int *rows, int nrows, const int *roles, int nroles)
{
	for(int i = 0; i < nrows; ++i) {
		const int r = rows[i];
		b->mask = (u16)(b->mask | (1u << r));
		b->role[r] = (i8)(i < nroles ? roles[i] : BR_ROOT);
	}
}
#define BASS_SET(b, rows, roles) bass_set(b, rows, (int)countof(rows), roles, (int)countof(roles))

static BassPat make_bass(Composer *c, int id)
{
	Rng *r = &c->r;
	BassPat b = {0};
	switch(id) {
	case B_OFFBEAT: b.mask = 0x4444; break;
	case B_ROLLING:
		b.mask = 0xEEEE;
		for(int i = 3; i < 16; i += 4) b.role[i] = rng_chance(r, 0.3f) ? BR_OCTAVE : BR_ROOT;
		break;
	case B_GALLOP:
		b.mask = 0xCCCC;
		b.role[15] = rng_chance(r, 0.5f) ? BR_OCTAVE : BR_ROOT;
		break;
	case B_OCTAVES:
		b.mask = 0x5555;
		for(int i = 0; i < 16; ++i) b.role[i] = (i8)((i / 2) % 2 ? BR_OCTAVE : BR_ROOT);
		break;
	case B_HOUSE: {
		/* 4-6 hits, always one near the downbeat */
		b.mask = rng_chance(r, 0.5f) ? 0x0001 : 0x0004;
		const int hits = rng_range(r, 4, 6);
		while(__builtin_popcount(b.mask) < hits) b.mask = (u16)(b.mask | (1u << rng_range(r, 1, 15)));
		static const i8 roles[] = {BR_ROOT, BR_ROOT, BR_OCTAVE, BR_FIFTH, BR_ROOT, BR_BELOW};
		for(int i = 2; i < 16; ++i) b.role[i] = rng_pick(r, roles);
		break;
	}
	case B_WALKING: {
		static const int rows[] = {0, 4, 8, 12}, roles[] = {BR_ROOT, BR_THIRD, BR_FIFTH, BR_APPROACH};
		BASS_SET(&b, rows, roles);
		b.legato = true;
		break;
	}
	case B_SYNCOP: {
		static const int rows[] = {0, 3, 6, 8, 11, 14}, roles[] = {BR_ROOT, BR_ROOT, BR_FIFTH, BR_OCTAVE, BR_ROOT, BR_APPROACH};
		BASS_SET(&b, rows, roles);
		break;
	}
	case B_LONG: {
		static const int rows[] = {0, 14}, roles[] = {BR_ROOT, BR_APPROACH};
		static const int rows2[] = {0, 10, 14}, roles2[] = {BR_ROOT, BR_FIFTH, BR_APPROACH};
		BASS_SET(&b, rows, roles);
		if(rng_chance(r, 0.5f)) BASS_SET(&b, rows2, roles2);
		b.legato = true;
		break;
	}
	case B_PEDAL16:
		b.mask = 0xFFFF;
		b.role[14] = BR_OCTAVE;
		break;
	case B_BOUNCE: {
		static const int rows[] = {0, 3, 8, 11}, roles[] = {BR_ROOT, BR_OCTAVE, BR_ROOT, BR_OCTAVE};
		BASS_SET(&b, rows, roles);
		break;
	}
	case B_PULSE8:
		b.mask = 0x5555;
		if(rng_chance(r, 0.5f)) b.role[12] = BR_FIFTH;
		if(rng_chance(r, 0.4f)) b.role[14] = BR_OCTAVE;
		break;
	case B_OCTAVE16:
		b.mask = 0xFFFF;
		for(int i = 0; i < 16; ++i) b.role[i] = (i8)(i % 2 ? BR_OCTAVE : BR_ROOT);
		break;
	default: break;
	}
	return b;
}

/* A 2-bar motif. Rhythm by style: riff (weighted rows, strong beats
 * likelier), legato (few onsets, each held to the next), syncopated
 * (3-3-2 cells), anthem (long notes and wide leaps). Pitch: a stepwise
 * contour with leap recovery, strong beats on chord tones; riffs leave
 * the last 4 rows empty so the answer can breathe. */
static float riff_weight(int row, float density)
{
	if(row % 8 == 0) return 5.0f;
	if(row % 4 == 0) return 3.0f;
	if(row % 2 == 0) return 2.2f;
	return 0.6f * density;
}

static Motif make_motif(Composer *c, float density, int root0, int root1, int st)
{
	Rng *r = &c->r;
	Motif out = {0};
	bool used[32] = {0};
	int span = 28;
	if(st == MS_RIFF) {
		const int target = iclamp((int)roundf(rng_rangef(r, 4.0f, 6.5f) * density), 3, 10);
		const bool repeat_rhythm = rng_chance(r, 0.45f);
		span = repeat_rhythm ? 16 : 28;
		const int first = rng_chance(r, 0.7f) ? 0 : 2;
		used[first] = true;
		int count = 1;
		const int want = repeat_rhythm ? imax(2, target / 2 + 1) : target;
		for(int guard = 0; count < want && guard < 200; ++guard) {
			float total = 0;
			for(int i = 0; i < span; ++i) total += used[i] ? 0.0f : riff_weight(i, density);
			float x = rng_uniform(r) * total;
			for(int i = 0; i < span; ++i) {
				if(used[i]) continue;
				x -= riff_weight(i, density);
				if(x <= 0) {
					used[i] = true;
					++count;
					break;
				}
			}
		}
		if(repeat_rhythm) { /* bar 2 echoes bar 1's rhythm, minus its tail */
			int last = 0;
			for(int i = 0; i < 16; ++i) {
				if(used[i]) last = i;
			}
			for(int i = 0; i < 16; ++i) {
				if(used[i] && !(i == last && last >= 8) && i + 16 < 28) used[i + 16] = true;
			}
		}
	} else {
		static const int legato_r[][6] = {{0, 6, 12, 16, 22, -1}, {0, 8, 14, 20, -1}, {0, 4, 10, 16, 24, -1}, {2, 8, 14, 18, 24, -1}, {0, 10, 16, 22, -1}};
		static const int syncop_r[][8] = {{0, 3, 6, 8, 11, 14, 16, 22}, {0, 3, 6, 10, 12, 16, 19, 22}, {0, 2, 6, 8, 10, 14, 16, 24},
		                                  {0, 3, 6, 12, 16, 19, 22, -1}, {0, 6, 8, 11, 14, 16, 22, -1}};
		static const int anthem_r[][5] = {{0, 6, 8, 16, -1}, {0, 8, 12, 16, 24}, {0, 4, 8, 16, -1}, {0, 12, 16, 22, -1}, {0, 8, 16, -1, -1}};
		const int *rows = nullptr;
		int n = 0;
		if(st == MS_LEGATO) rows = legato_r[rng_next(r) % 5], n = 6;
		else if(st == MS_SYNCOP) rows = syncop_r[rng_next(r) % 5], n = 8;
		else rows = anthem_r[rng_next(r) % 5], n = 5;
		for(int i = 0; i < n && rows[i] >= 0; ++i) used[rows[i]] = true;
	}
	(void)span;
	int rows[32];
	int nrows = 0;
	for(int i = 0; i < 32; ++i) {
		if(used[i]) rows[nrows++] = i;
	}
	int deg = nearest_chord_tone(rng_range(r, 0, 4), root0) - root0; /* relative to root0 */
	int prev_step = 0;
	for(int i = 0; i < nrows; ++i) {
		const int row = rows[i];
		if(i > 0) {
			int step;
			if(abs(prev_step) >= 3) step = prev_step > 0 ? -1 : 1; /* leap recovery */
			else {
				const float x = rng_uniform(r);
				int mag = x < 0.55f ? 1 : x < 0.8f ? 2 : x < 0.9f ? 0 : rng_range(r, 3, 4);
				if(st == MS_ANTHEM) mag = x < 0.3f ? 1 : x < 0.55f ? 2 : x < 0.9f ? rng_range(r, 3, 5) : 0; /* wide, singing intervals */
				if(st == MS_LEGATO && mag == 0) mag = 1;
				step = rng_chance(r, 0.5f) ? mag : -mag;
			}
			deg += step;
			if(deg > 7) deg -= 2 * abs(step);
			if(deg < -3) deg += 2 * abs(step);
			prev_step = step;
		}
		const int root = row < 16 ? root0 : root1;
		if(row % 4 == 0) deg = nearest_chord_tone(deg + root0, root) - root0;
		const int next_row = i + 1 < nrows ? rows[i + 1] : (st == MS_RIFF ? 32 : 30);
		int len = next_row - row;
		if(st == MS_RIFF) len = imin(len, c->staccato ? 2 : 6);
		else if(st == MS_SYNCOP) len = imin(len, 6);
		else len = imin(len, 14); /* legato / anthem: held notes */
		out.n[out.count++] = (MotifNote){row, imax(1, len), deg};
	}
	return out;
}

static int pick_bass(Composer *c)
{
	const StyleDef *S = &styles[c->style];
	int id = S->bass[rng_range(&c->r, 0, 3)];
	if(c->s->mood == MOOD_CALM && (id == B_PEDAL16 || id == B_OCTAVE16 || id == B_ROLLING || id == B_GALLOP)) id = rng_chance(&c->r, 0.5f) ? B_LONG : B_HOUSE;
	return id;
}

static void choose_groove(Composer *c)
{
	const Mood m = c->s->mood;
	const StyleDef *S = &styles[c->style];
	Rng *r = &c->r;
	static const float first_twice[3] = {2.0f, 1.0f, 1.0f};
	c->groove_a = S->groove_a[pick_w(r, first_twice, 3)];
	c->groove_b = rng_chance(r, 0.35f) ? c->groove_a : S->groove_b[pick_w(r, first_twice, 3)];
	if(c->war) c->groove_b = c->style == ST_HARDSTYLE ? G_HARDSTYLE : G_HARDTRANCE;
	/* a song's own extra kick (not in four-on-the-floor trance or punk) */
	if(!c->war && c->style != ST_PUNK && rng_chance(r, 0.35f)) {
		static const int rows[] = {3, 7, 11, 14};
		c->kick_extra = (u16)(1u << rng_pick(r, rows));
	}
	static const u16 ghost_sets[] = {0x0080, 0x8000, 0x8400, 0x4080, 0x0000, 0x8808};
	c->ghost_extra = c->war || c->style == ST_PUNK ? 0 : rng_pick(r, ghost_sets);
	float clap_p = 0.7f;
	switch(c->style) {
	case ST_SYNTHWAVE: clap_p = 0.2f; break;
	case ST_PUNK: clap_p = 0.05f; break;
	case ST_CHIP: clap_p = 0.5f; break;
	case ST_BREAKS: clap_p = 0.25f; break;
	case ST_LOFI: clap_p = 0.3f; break;
	case ST_HARDTRANCE: clap_p = 0.85f; break;
	default: break;
	}
	c->backbeat = rng_chance(r, clap_p) ? DR_CLAP : DR_SNARE;

	/* bass rhythms (verse and chorus may differ); calm avoids the busy ones */
	c->bass_a = pick_bass(c);
	c->bass_b = rng_chance(r, 0.45f) ? c->bass_a : pick_bass(c);
	c->bpat_a = make_bass(c, c->bass_a);
	c->bpat_b = c->bass_b == c->bass_a ? c->bpat_a : make_bass(c, c->bass_b);
	c->bass_octave_layer = c->style == ST_TECHNO || c->style == ST_EURODANCE || c->style == ST_HARDTRANCE || c->style == ST_CHIP;

	/* arpeggio order over chord tones (0 root, 1 third, 2 fifth, 3 octave, 4 octave+third) */
	c->arp_order = arp_order_of((int)(rng_next(r) % arp_order_count));
	/* rate: 16ths, 8ths or dotted 8ths (a 3-row cycle that drifts across the bar) */
	if(m == MOOD_CALM) c->arp_rate = rng_chance(r, 0.5f) ? 2 : 1;
	else if(c->style == ST_CHIP || c->war) c->arp_rate = rng_chance(r, 0.75f) ? 1 : 3;
	else {
		static const int rates[] = {1, 1, 2, 3, 3};
		c->arp_rate = rng_pick(r, rates);
	}
	c->arp_len = c->arp_rate == 1 ? 1 : rng_chance(r, 0.6f) ? c->arp_rate : 1;
	if(c->arp_len > 1) { /* held arp notes: a little sustain */
		c->s->patches[CH_ARP].sustain = 0.3f;
		c->s->patches[CH_ARP].decay = 0.25f;
		c->s->patches[CH_ARP].release = 0.12f;
	}
	c->arp_cont = m != MOOD_CALM && rng_chance(r, 0.5f);

	/* chord stabs */
	switch(c->style) {
	case ST_PUNK: {
		static const u16 st[] = {0x5555, 0x5455, 0x5555};
		c->stab_mask = rng_pick(r, st);
		break;
	}
	case ST_EURODANCE: {
		static const u16 st[] = {0x4444, 0x5494, 0x4444};
		c->stab_mask = rng_pick(r, st);
		break;
	}
	case ST_SYNTHWAVE:
	case ST_LOFI: c->stab_mask = 0; break; /* held chords */
	case ST_BREAKS: {
		static const u16 st[] = {0x0909, 0x2492, 0x0490};
		c->stab_mask = rng_pick(r, st);
		break;
	}
	default: {
		static const u16 stabs[] = {0x5494 /* 2,4,7,10,12,14 (reference) */, 0x4444, 0x1249, 0x4924, 0x2492};
		c->stab_mask = rng_pick(r, stabs);
		break;
	}
	}

	/* motifs: a rhythmic approach per song and per theme */
	static const float mw[MOOD_COUNT][2][4] = {
		/* A: riff, legato, syncop, anthem; B: riff, legato, syncop, anthem */
		{{0.35f, 0.45f, 0.2f, 0.0f}, {0.2f, 0.5f, 0.1f, 0.2f}},   /* calm */
		{{0.35f, 0.3f, 0.35f, 0.0f}, {0.15f, 0.3f, 0.2f, 0.35f}}, /* tense */
		{{0.4f, 0.25f, 0.35f, 0.0f}, {0.3f, 0.3f, 0.0f, 0.4f}},   /* crisis */
		{{0.35f, 0.3f, 0.35f, 0.0f}, {0.2f, 0.3f, 0.0f, 0.5f}},   /* triumph */
		{{0.4f, 0.2f, 0.4f, 0.0f}, {0.0f, 0.35f, 0.35f, 0.3f}},   /* wartime */
		{{0.5f, 0.1f, 0.4f, 0.0f}, {0.1f, 0.2f, 0.2f, 0.5f}},     /* frustrated: riffs, then an anthem lead in the drop */
	};
	c->style_a = pick_w(r, mw[m][0], 4);
	c->style_b = pick_w(r, mw[m][1], 4);
	const float density = m == MOOD_CALM ? 0.8f : m == MOOD_CRISIS ? 1.3f : c->war ? 1.15f : 1.0f;
	c->motif_a = make_motif(c, density, c->prog_a->deg[0], c->prog_a->deg[c->bpc_a == 1 ? 1 : 0], c->style_a);
	c->motif_b = make_motif(c, density * 1.2f, c->prog_b->deg[0], c->prog_b->deg[c->bpc_b == 1 ? 1 : 0], c->style_b);
}

/* --- form ------------------------------------------------------------------ */
typedef struct {
	u8 k;
	int bars;
	u8 out;  /* a fixed hand-over (epic form) */
	u8 flag; /* 1 quiet intro, 2 build-up */
} FormItem;
typedef struct {
	FormItem v[MAX_PLAN];
	int n;
} Form;
#define FI(k, b) {(k), (b), 255, 0}
#define FORM(...) {{__VA_ARGS__}, (int)countof(((FormItem[]){__VA_ARGS__}))}

static int form_total(const Form *f)
{
	int b = 0;
	for(int i = 0; i < f->n; ++i) b += f->v[i].bars;
	return b;
}
static bool form_insert(Form *f, int at, FormItem it)
{
	if(f->n == MAX_PLAN) return false;
	memmove(&f->v[at + 1], &f->v[at], (size_t)(f->n - at) * sizeof f->v[0]);
	f->v[at] = it;
	++f->n;
	return true;
}
static void form_erase(Form *f, int at)
{
	memmove(&f->v[at], &f->v[at + 1], (size_t)(f->n - at - 1) * sizeof f->v[0]);
	--f->n;
}

static void choose_form(Composer *c)
{
	Song *s = c->s;
	Rng *r = &c->r;
	static const Form forms[] = {
		FORM(FI(SEC_INTRO, 8), FI(SEC_VERSE, 16), FI(SEC_CHORUS, 16), FI(SEC_BRIDGE, 8), FI(SEC_BREAKDOWN, 8), FI(SEC_DROP, 16), FI(SEC_OUTRO, 8)),
		FORM(FI(SEC_INTRO, 8), FI(SEC_VERSE, 16), FI(SEC_CHORUS, 16), FI(SEC_BREAKDOWN, 16), FI(SEC_DROP, 16), FI(SEC_OUTRO, 8)),
		FORM(FI(SEC_INTRO, 16), FI(SEC_VERSE, 16), FI(SEC_CHORUS, 16), FI(SEC_BREAKDOWN, 8), FI(SEC_VERSE, 8), FI(SEC_DROP, 16), FI(SEC_OUTRO, 8)),
		FORM(FI(SEC_INTRO, 8), FI(SEC_VERSE, 16), FI(SEC_CHORUS, 8), FI(SEC_VERSE, 8), FI(SEC_CHORUS, 16), FI(SEC_BREAKDOWN, 8), FI(SEC_DROP, 16), FI(SEC_OUTRO, 8)),
		/* song forms: verse / chorus twice, a bridge, the last chorus */
		FORM(FI(SEC_INTRO, 8), FI(SEC_VERSE, 16), FI(SEC_CHORUS, 16), FI(SEC_VERSE, 8), FI(SEC_CHORUS, 16), FI(SEC_BRIDGE, 8), FI(SEC_CHORUS, 16), FI(SEC_OUTRO, 8)),
		FORM(FI(SEC_INTRO, 8), FI(SEC_VERSE, 8), FI(SEC_VERSE, 8), FI(SEC_CHORUS, 16), FI(SEC_BREAKDOWN, 8), FI(SEC_CHORUS, 16), FI(SEC_OUTRO, 8)),
		FORM(FI(SEC_INTRO, 8), FI(SEC_CHORUS, 8), FI(SEC_VERSE, 16), FI(SEC_BRIDGE, 8), FI(SEC_DROP, 16), FI(SEC_OUTRO, 8)),
		/* club form: drop, breakdown, drop */
		FORM(FI(SEC_INTRO, 16), FI(SEC_DROP, 16), FI(SEC_BREAKDOWN, 16), FI(SEC_DROP, 16), FI(SEC_OUTRO, 8)),
	};
	/* wartime: trance forms with long (16/32-bar) breakdowns and builds, always full length */
	static const Form war_forms[] = {
		FORM(FI(SEC_INTRO, 16), FI(SEC_VERSE, 16), FI(SEC_BREAKDOWN, 16), FI(SEC_DROP, 32), FI(SEC_OUTRO, 8)),
		FORM(FI(SEC_INTRO, 8), FI(SEC_VERSE, 16), FI(SEC_CHORUS, 16), FI(SEC_BREAKDOWN, 16), FI(SEC_DROP, 16), FI(SEC_OUTRO, 8)),
		FORM(FI(SEC_INTRO, 16), FI(SEC_CHORUS, 16), FI(SEC_BREAKDOWN, 32), FI(SEC_DROP, 16), FI(SEC_OUTRO, 8)),
		FORM(FI(SEC_INTRO, 8), FI(SEC_VERSE, 16), FI(SEC_BREAKDOWN, 16), FI(SEC_DROP, 16), FI(SEC_BREAKDOWN, 8), FI(SEC_DROP, 16), FI(SEC_OUTRO, 8)),
		FORM(FI(SEC_INTRO, 8), FI(SEC_DROP, 16), FI(SEC_BREAKDOWN, 16), FI(SEC_DROP, 16), FI(SEC_OUTRO, 8)),
		FORM(FI(SEC_INTRO, 16), FI(SEC_VERSE, 8), FI(SEC_CHORUS, 8), FI(SEC_BREAKDOWN, 16), FI(SEC_DROP, 32), FI(SEC_OUTRO, 8)),
	};
	Form form = c->war ? war_forms[rng_next(r) % 6] : forms[rng_next(r) % 8];
	const double bar_s = 240.0 / (double)s->bpm;
	/* Length: 2-3.5 minutes, or (some songs, most often in wartime: trance
	 * tracks run long) a long song of 5-10 minutes built from extra
	 * verse / chorus or breakdown / drop cycles. */
	static const float long_p[MOOD_COUNT] = {0.1f, 0.15f, 0.15f, 0.1f, 0.35f, 0.3f};
	const bool long_song = rng_chance(r, long_p[s->mood]);
	const double target = long_song ? rng_rangef(r, 300.0f, 580.0f) : rng_rangef(r, 140.0f, 195.0f);
	const double max_s = long_song ? 600.0 : 210.0;
	/* About one song in four is a short intermission (30-80 s). Its own
	 * generator, so a full-length song's basics do not depend on it. */
	Rng ir = rng_make(s->seed ^ 0x3C6EF372FE94F82Bull);
	if(rng_chance(&ir, 0.25f) && !c->war) {
		static const Form short_forms[] = {
			FORM(FI(SEC_INTRO, 4), FI(SEC_VERSE, 8), FI(SEC_OUTRO, 4)),
			FORM(FI(SEC_INTRO, 4), FI(SEC_VERSE, 8), FI(SEC_CHORUS, 8), FI(SEC_OUTRO, 4)),
			FORM(FI(SEC_INTRO, 8), FI(SEC_CHORUS, 8), FI(SEC_BREAKDOWN, 8), FI(SEC_DROP, 8), FI(SEC_OUTRO, 4)),
		};
		form = short_forms[rng_next(&ir) % 3];
	}
	const bool intermission = form.v[form.n - 1].bars == 4;
#define SECONDS() (form_total(&form) * bar_s)
	if(intermission) {
		/* 30-80 s at any tempo: double sections at fast tempos, halve them at slow ones */
		for(int guard = 0; guard < 6 && SECONDS() < 32.0; ++guard) {
			for(int i = 0; i < form.n; ++i) {
				FormItem *it = &form.v[i];
				if(it->k != SEC_INTRO && it->k != SEC_OUTRO && it->bars <= 8) {
					it->bars *= 2;
					break;
				}
			}
		}
		for(int guard = 0; guard < 6 && SECONDS() > 80.0; ++guard) {
			for(int i = 0; i < form.n; ++i) {
				FormItem *it = &form.v[i];
				if(it->k != SEC_OUTRO && it->bars >= 8) {
					it->bars /= 2;
					break;
				}
			}
		}
	}
	/* Fit 2-3 minutes. Shorten: halve a 16-bar intro or breakdown (a
	 * 32-bar one in wartime), drop a bridge, halve a 16-bar verse /
	 * chorus / drop, drop a repeated verse. Extend: double an 8-bar verse
	 * or chorus, then add an 8-bar chorus after the last chorus / drop.
	 * The epic long form, after the 16-minute reference module: a pads-
	 * and-arps intro, a build where the kick and bass come in, main
	 * sections between breakdowns (the first with a riser), a long drop,
	 * a percussion-heavy groove section, a main with a stop in the middle,
	 * a near-silent pause before the final main, an outro onto the pads.
	 * Section lengths keep the reference's proportions, scaled to the target. */
	c->epic = long_song && !intermission && rng_chance(r, c->war ? 0.6f : 0.45f);
	if(c->epic) {
		typedef struct {
			u8 k;
			float s;
			u8 out, flag;
		} Bp;
		static const Bp blueprint[] = {
			{SEC_INTRO, 60, 255, 1},  {SEC_VERSE, 75, 255, 2},     {SEC_CHORUS, 60, 255, 0},  {SEC_BREAKDOWN, 30, TR_RISER, 0},
			{SEC_DROP, 150, 255, 0},  {SEC_BREAKDOWN, 30, 255, 0}, {SEC_CHORUS, 60, 255, 0},  {SEC_BREAKDOWN, 45, 255, 0},
			{SEC_GROOVE, 75, 255, 0}, {SEC_BREAKDOWN, 30, 255, 0}, {SEC_DROP, 60, TR_STOP, 0}, {SEC_DROP, 60, TR_PAUSE, 0},
			{SEC_DROP, 90, 255, 0},   {SEC_OUTRO, 37, 255, 0},
		};
		float sum = 0;
		for(size_t i = 0; i < countof(blueprint); ++i) sum += blueprint[i].s;
		form.n = 0;
		for(size_t i = 0; i < countof(blueprint); ++i) {
			const Bp *b = &blueprint[i];
			const double bars = (double)b->s / (double)sum * target / bar_s;
			form.v[form.n++] = (FormItem){b->k, imax(8, (int)lround(bars / 8.0) * 8), b->out, b->flag};
		}
		for(int guard = 0; guard < 40 && SECONDS() > max_s - 2.0; ++guard) { /* trim the longest section */
			int mx = 0;
			for(int i = 1; i < form.n; ++i) {
				if(form.v[mx].bars < form.v[i].bars) mx = i;
			}
			if(form.v[mx].bars <= 8) break;
			form.v[mx].bars -= 8;
		}
	}
	/* long songs: whole cycles before the outro (breakdown -> drop in
	 * club and trance forms, verse -> chorus or bridge -> chorus otherwise) */
	bool club = c->war;
	for(int i = 0; i < form.n && !club; ++i) club = form.v[i].k == SEC_DROP;
	for(int cycle = 0; long_song && !c->epic && !intermission && cycle < 24; ++cycle) {
		FormItem a = FI(SEC_BRIDGE, 8), b = FI(SEC_CHORUS, 16);
		if(club) a = (FormItem)FI(SEC_BREAKDOWN, 16), b = (FormItem)FI(SEC_DROP, c->war ? 32 : 16);
		else if(cycle % 2 == 0) a = (FormItem)FI(SEC_VERSE, 16);
		if((form_total(&form) + a.bars + b.bars) * bar_s > target + 20.0) break;
		const int at = form.n - 1; /* before the outro */
		if(!form_insert(&form, at, b) || !form_insert(&form, at, a)) {
			c->oom = true;
			break;
		}
	}
	const double fit_s = target + 10.0 < max_s - 2.0 ? target + 10.0 : max_s - 2.0; /* std::min */
	for(int guard = 0; guard < 32 && !intermission && !c->epic && !(SECONDS() <= fit_s); ++guard) {
		bool done = false;
		for(int i = 0; i < form.n; ++i) {
			FormItem *it = &form.v[i];
			if((it->bars == 16 && (it->k == SEC_INTRO || (it->k == SEC_BREAKDOWN && !c->war))) || (c->war && it->k == SEC_BREAKDOWN && it->bars == 32)) {
				it->bars /= 2;
				done = true;
				break;
			}
		}
		for(int i = 0; !done && i < form.n; ++i) {
			if(form.v[i].k == SEC_BRIDGE) {
				form_erase(&form, i);
				done = true;
			}
		}
		for(int i = 0; !done && i < form.n; ++i) {
			if(form.v[i].bars >= 16 && form.v[i].k != SEC_OUTRO) {
				form.v[i].bars /= 2;
				done = true;
			}
		}
		for(int i = 3; !done && i + 1 < form.n; ++i) {
			if(form.v[i].k == SEC_VERSE && form.v[i - 1].k != form.v[i + 1].k) {
				form_erase(&form, i);
				done = true;
			}
		}
		if(!done) break;
	}
	for(int guard = 0; guard < 8 && !intermission && !long_song && SECONDS() < target - 15; ++guard) {
		bool done = false;
		for(int i = 0; i < form.n; ++i) {
			FormItem *it = &form.v[i];
			if((it->k == SEC_VERSE || it->k == SEC_CHORUS || it->k == SEC_DROP) && it->bars == 8) {
				it->bars = 16;
				done = true;
				break;
			}
		}
		for(int i = form.n; !done && i-- > 0;) {
			if(form.v[i].k == SEC_DROP) {
				if(!form_insert(&form, i + 1, (FormItem)FI(SEC_CHORUS, 8))) c->oom = true;
				done = true;
			} else if(form.v[i].k == SEC_CHORUS) { /* a bridge, then the last chorus again */
				if(!form_insert(&form, i + 1, (FormItem)FI(SEC_CHORUS, 8)) || !form_insert(&form, i + 1, (FormItem)FI(SEC_BRIDGE, 8))) c->oom = true;
				done = true;
			}
		}
	}
#undef SECONDS
	u32 bar = 0;
	int verses = 0, choruses = 0;
	c->nplan = 0;
	for(int i = 0; i < form.n; ++i) {
		const FormItem *it = &form.v[i];
		PlanItem p = plan_item();
		p.sec = (Section){(SectionKind)it->k, bar, (u32)it->bars};
		switch(it->k) {
		case SEC_INTRO:
		case SEC_VERSE:
		case SEC_GROOVE:
		case SEC_ARP_SOLO:
		case SEC_CHOIR:
		case SEC_OUTRO: p.prog = c->prog_a, p.bars_per_chord = c->bpc_a; break;
		case SEC_CHORUS:
		case SEC_DROP: p.prog = c->prog_b, p.bars_per_chord = c->bpc_b; break;
		case SEC_BRIDGE: p.prog = c->prog_c, p.bars_per_chord = 2; break;
		case SEC_BREAKDOWN:
			p.prog = rng_chance(r, 0.35f) ? c->prog_c : rng_chance(r, 0.5f) ? c->prog_a : c->prog_b;
			p.bars_per_chord = 2;
			break;
		default: break;
		}
		p.forced_out = it->out;
		p.quiet = it->flag == 1;
		p.build_up = it->flag == 2;
		if(it->k == SEC_VERSE) p.variant = verses++ > 0 ? 1 : 0;
		if(it->k == SEC_CHORUS || it->k == SEC_DROP) p.variant = choruses++ > 0 ? 1 : 0;
		c->plan[c->nplan++] = p;
		s->sections[s->nsections++] = p.sec;
		bar += (u32)it->bars;
	}
	s->total_bars = bar;

	/* Key change: the last chorus / drop (and what follows) a step up,
	 * prepared by the new key's dominant in the bar before. */
	const float mod_p = s->mood == MOOD_CALM ? 0.0f : s->mood == MOOD_TRIUMPH ? 0.5f : c->war ? 0.35f : 0.3f;
	int last_big = -1, bigs = 0;
	for(int i = 0; i < c->nplan; ++i) {
		const int k = c->plan[i].sec.kind;
		if(k == SEC_CHORUS || k == SEC_DROP) {
			++bigs;
			if(bigs >= 2 && c->plan[i - 1].sec.kind != SEC_CHORUS && c->plan[i - 1].sec.kind != SEC_DROP) last_big = i;
		}
	}
	if(c->epic) last_big = c->nplan - 2; /* the final main, after the pause */
	if(last_big > 0 && rng_chance(r, mod_p)) {
		static const int shifts[] = {2, 2, 2, 1, 3};
		c->mod_shift = rng_pick(r, shifts);
		c->mod_bar = c->plan[last_big].sec.start_bar;
		for(int i = last_big; i < c->nplan; ++i) c->plan[i].shift = c->mod_shift;
		PlanItem *before = &c->plan[last_big - 1];
		/* the new key's V: +2 -> VI major (secondary dominant), +1 -> bVI, +3 -> bVII */
		before->pivot_deg = c->mod_shift == 3 ? 6 : 5;
		before->pivot_col = c->mod_shift == 2 ? C_MED6 : C_MINOR;
	}
	s->final_key = c->key + c->mod_shift;
}

/* --- arrangement: transitions and melodic development (tr) ------------------- */
/* build length in bars for a crescendo device out of section `p` */
static int build_bars(const Composer *c, const PlanItem *p)
{
	const int bars = (int)p->sec.bars;
	if(p->sec.kind == SEC_BREAKDOWN || p->sec.kind == SEC_INTRO) return imin(c->war ? 8 : 4, bars / 2);
	return imin(c->war ? 4 : 2, bars / 4);
}

static bool ends_on_tonic(const Progression *prog, int idx)
{
	const int last = prog->deg[idx];
	return prog->col[idx] != C_HOME || (!is_chord_tone(0, last) && !is_chord_tone(4, last) && !is_chord_tone(2, last));
}

typedef struct {
	u8 k;
	float w;
} TrOpt;

static void choose_arrangement(Composer *c)
{
	Rng *tr = &c->tr;
	c->seq_step = rng_chance(tr, 0.45f) ? 1 : rng_chance(tr, 0.5f) ? 2 : -1;
	c->dev_invert = rng_chance(tr, 0.5f);
	c->dev_steps = rng_chance(tr, 0.5f) ? -2 : 3;
	c->risers_left = rng_chance(tr, 0.25f) ? 1 : 0; /* most songs have no long riser at all */
	int sweeps_left = rng_chance(tr, 0.5f) ? 1 : 0;
	c->hard_drop = c->war || (c->groove_b == G_FOUR && c->bass_b != B_OFFBEAT && rng_chance(tr, c->s->mood == MOOD_CALM ? 0.25f : 0.45f));
	/* trance gates over a bar of 16ths (bit r = row r) */
	static const u16 gates[] = {0xB6DB /* x.xx.xx.xx.x.xx.x */, 0x6DB6, 0xDDDD /* x.xxx.xx... */, 0x5555, 0xF6F6};
	c->gate_mask = rng_chance(tr, c->war ? 1.0f : 0.4f) ? rng_pick(tr, gates) : 0;
	c->beat_intro = rng_chance(tr, 0.3f); /* 90s style: the intro opens on drums and bass, the chords come in halfway */
	/* this song's taste: every device weight scaled by 0.4-1.6 */
	float taste[TR_COUNT];
	for(int i = 0; i < TR_COUNT; ++i) taste[i] = rng_rangef(tr, 0.4f, 1.6f);
	if(c->war) taste[TR_KICK_DOUBLE] *= 2.5f; /* kick rolls into the drop */
	c->plan[c->nplan - 1].cadence = true;
	c->cad_steps = rng_chance(tr, 0.4f) ? 2 : rng_chance(tr, 0.5f) ? -1 : 1;
	/* Sections end on a chord that holds a tonic-triad tone, so the
	 * closing phrase can land on a stable note: II (lydian), bVII, bII,
	 * ii... at the very end become a modal cadence onto I for one bar. */
	for(int i = 0; i < c->nplan; ++i) {
		PlanItem *p = &c->plan[i];
		if(p->sec.kind == SEC_INTRO || p->cadence || p->pivot_deg >= 0) continue;
		p->end_tonic = ends_on_tonic(p->prog, (((int)p->sec.bars - 1) / p->bars_per_chord) % 4);
	}
	u8 prev = 255;
	for(int i = 0; i + 1 < c->nplan; ++i) {
		PlanItem *p = &c->plan[i];
		const int k = p->sec.kind, next = c->plan[i + 1].sec.kind;
		TrOpt o[24];
		int no = 0;
#define OPTS(...)                                         \
	do {                                                  \
		const TrOpt src_[] = {__VA_ARGS__};               \
		memcpy(o, src_, sizeof src_);                     \
		no = (int)countof(src_);                          \
	} while(0)
		const float R = c->risers_left > 0 ? 1.0f : 0.0f, S = sweeps_left > 0 ? 1.0f : 0.0f;
		const bool melodic = k == SEC_VERSE || k == SEC_CHORUS || k == SEC_DROP || k == SEC_BRIDGE || k == SEC_BREAKDOWN;
		const float L = melodic && p->sec.bars >= 8 ? 1.0f : 0.0f;
		if(k == SEC_BREAKDOWN) {
			OPTS({TR_ROLL, 1.0f}, {TR_CLAP_ROLL, 0.8f}, {TR_RISER, 1.8f * R}, {TR_FILTER_OPEN, 1.0f}, {TR_KICK_RETURN, 1.0f},
			     {TR_SHORT_RISER, 0.9f * S}, {TR_STOP, 0.7f}, {TR_LAYER_IN, 1.2f}, {TR_HAT_RAMP, 0.9f}, {TR_ARP_CLIMB, 0.9f},
			     {TR_PAD_SWELL, 1.0f}, {TR_LEAD_CLIMB, 0.8f}, {TR_TOM_CASCADE, 0.6f}, {TR_STAB_RAMP, 0.7f}, {TR_KICK_DOUBLE, c->war ? 1.0f : 0.0f});
		} else if(next == SEC_CHORUS || next == SEC_DROP) {
			OPTS({TR_ROLL, 1.0f}, {TR_CLAP_ROLL, 0.8f}, {TR_FILL, 1.0f}, {TR_TOMS, 0.4f}, {TR_TOM_CASCADE, 1.0f},
			     {TR_RISER, 1.6f * R}, {TR_SHORT_RISER, 0.8f * S}, {TR_KICK_OUT, 0.7f}, {TR_STOP, 0.5f}, {TR_PLAIN, 0.3f},
			     {TR_DRUM_BREAK, k != SEC_INTRO && p->sec.bars >= 16 ? 0.6f : 0.0f}, {TR_HAT_RAMP, 0.9f}, {TR_ARP_CLIMB, 0.8f},
			     {TR_STAB_RAMP, 0.8f}, {TR_PAD_SWELL, 0.6f}, {TR_KICK_DOUBLE, 1.4f}, {TR_LEAD_CLIMB, 0.9f * L},
			     {TR_LAYER_IN, k == SEC_INTRO ? 1.0f : 0.0f}, {TR_FILTER_OPEN, k == SEC_INTRO ? 0.8f : 0.0f});
		} else if(next == SEC_BREAKDOWN) {
			OPTS({TR_FILTER_CLOSE, 1.6f}, {TR_FILL, 0.7f}, {TR_TOMS, 0.5f}, {TR_STOP, 0.6f}, {TR_PLAIN, 0.6f});
		} else if(next == SEC_OUTRO) {
			OPTS({TR_FILL, 1.0f}, {TR_TOMS, 0.7f}, {TR_PLAIN, 0.8f}, {TR_FILTER_CLOSE, 0.6f}, {TR_KICK_OUT, 0.6f});
		} else if(k == SEC_INTRO) {
			OPTS({TR_FILTER_OPEN, 1.0f}, {TR_FILL, 0.8f}, {TR_TOMS, 0.5f}, {TR_PLAIN, 0.6f}, {TR_KICK_OUT, 0.4f},
			     {TR_LAYER_IN, 0.9f}, {TR_HAT_RAMP, 0.7f}, {TR_ARP_CLIMB, 0.7f}, {TR_PAD_SWELL, 0.5f});
		} else {
			OPTS({TR_FILL, 1.2f}, {TR_TOMS, 0.8f}, {TR_PLAIN, 0.9f}, {TR_KICK_OUT, 0.6f}, {TR_STOP, 0.4f},
			     {TR_DRUM_BREAK, p->sec.bars >= 16 ? 0.6f : 0.0f}, {TR_HAT_RAMP, 0.5f}, {TR_TOM_CASCADE, 0.4f});
		}
#undef OPTS
		for(int j = 0; j < no; ++j) {
			TrOpt *x = &o[j];
			x->w *= taste[x->k];
			if(x->k == prev) x->w = 0.0f; /* never the same hand-over twice in a row */
			if(x->k == TR_LEAD_CLIMB && c->plan[i + 1].shift != p->shift) x->w = 0.0f; /* the key change has its own pivot */
		}
		u8 t;
		if(p->forced_out != 255) t = p->forced_out;
		else {
			t = TR_PLAIN;
			float total = 0;
			for(int j = 0; j < no; ++j) total += o[j].w;
			float x = rng_uniform(tr) * total;
			bool found = false;
			for(int j = 0; j < no && !found; ++j) {
				if(o[j].w <= 0) continue;
				x -= o[j].w;
				if(x <= 0) t = o[j].k, found = true;
			}
			for(int j = 0; j < no && !found; ++j) {
				if(o[j].w > 0) t = o[j].k, found = true;
			}
		}
		if(t == TR_RISER) --c->risers_left;
		if(t == TR_SHORT_RISER) --sweeps_left;
		p->out = t;
		prev = t;
		const bool up = next == SEC_CHORUS || next == SEC_DROP;
		p->reverse = t != TR_RISER && t != TR_PAD_SWELL && rng_chance(tr, t == TR_STOP ? 0.5f : up ? 0.15f : 0.08f);
		p->downlift = !up && next != SEC_VERSE && rng_chance(tr, 0.45f);
		p->impact = next == SEC_DROP && (t == TR_STOP || rng_chance(tr, 0.6f));
		if(c->ntransitions) transitions_add(c, ", ");
		transitions_add(c, tr_name(t));
		if(p->reverse) transitions_add(c, "+reverse");
		if(p->downlift) transitions_add(c, "+downlifter");
	}
}

/* Removes or shortens the events under a stop / kick drop-out. */
static void apply_mutes(Composer *c)
{
	if(!c->nmutes) return;
	Song *s = c->s;
	u32 kept = 0;
	for(u32 i = 0; i < s->nevents; ++i) {
		NoteEvent e = s->events[i];
		bool drop = false;
		for(int j = 0; j < c->nmutes; ++j) {
			const Mute *m = &c->mutes[j];
			if(!((m->channels >> e.ch) & 1u)) continue;
			if(e.tick >= m->from && e.tick < m->to) drop = true;
			else if(e.tick < m->from && e.tick + e.len > m->from && s->patches[e.ch].inst != INST_KIT) e.len = (u16)(m->from - e.tick);
		}
		if(!drop) s->events[kept++] = e;
	}
	s->nevents = kept;
}

/* A roll from `bar0` over `bars` bars, ending `gap_rows` before the
 * boundary, crescendo from soft to accented. It accelerates in musical
 * steps and tops out at 16ths: quarters for the first half of a long
 * build, 8ths, then 16ths for the last quarter (at most two bars); only
 * a build of 2+ bars may end on one beat of 32nds. */
static void snare_roll(Composer *c, u32 bar0, int bars, int gap_rows, int drum)
{
	const int total = bars * 16 - gap_rows;
	const int sixteenths = imin(32, imax(4, total / 4));
	const int quarters = bars >= 4 ? total / 2 : 0;
	const bool thirty_seconds = bars >= 2 && rng_chance(&c->tr, 0.3f);
	for(int r = 0; r < total; ++r) {
		const int left = total - r;
		const int step = r < quarters ? 4 : left > sixteenths ? 2 : 1;
		if(r % step) continue;
		const int vel = 40 + 80 * r / imax(1, total);
		const u32 tick = (bar0 + (u32)(r / 16)) * ticks_per_bar + (u32)((r % 16) * ticks_per_row);
		add_ticks(c, tick, CH_SNARE, drum, 2, vel);
		if(thirty_seconds && left <= 4) add_ticks(c, tick + 3, CH_SNARE, drum, 2, vel - 6);
	}
}

/* Chord voicing with smooth voice leading: of all inversions/octaves
 * inside the pad register, take the one closest to the previous voicing.
 * `sus`: a sus4 for the first half bar, then the chord (a suspension). */
static void emit_chord(Composer *c, u32 bar, int row, int root, int len, int vel, u8 lay, int cut_rows, bool stab, u16 gate, bool sus)
{
	const int tones = c->power ? 3 : c->seventh && !stab ? 4 : 3;
	int pcs[4] = {0};
	for(int i = 0; i < tones; ++i) pcs[i] = pitch(c, c->key, root + 2 * i);
	if(c->power) pcs[1] = pitch(c, c->key, root + 4), pcs[2] = pitch(c, c->key, root) + 12; /* root, fifth, octave */
	int best[4] = {0}, best_cost = 1 << 30;
	const int lo = c->pad_lo, hi = c->pad_hi;
	for(int inv = 0; inv < tones; ++inv) {
		for(int oct = -1; oct <= 1; ++oct) {
			int v[4];
			for(int i = 0; i < tones; ++i) v[i] = pcs[(i + inv) % tones] + 12 * oct + ((i + inv) >= tones ? 12 : 0);
			if(c->power && inv != 0) continue; /* power chords stay in root position */
			bool ok = true;
			for(int i = 0; i < tones; ++i) ok = ok && v[i] >= lo - (c->power ? 7 : 0) && v[i] <= hi + (c->power ? 5 : 0);
			if(!ok) continue;
			int cost = 0;
			if(c->voicing_n == tones) {
				for(int i = 0; i < tones; ++i) cost += abs(v[i] - c->last_voicing[i]);
			} else {
				cost = abs(v[0] - (c->key + 12));
			}
			if(cost < best_cost) {
				best_cost = cost;
				memcpy(best, v, (size_t)tones * sizeof v[0]);
			}
		}
	}
	if(best_cost == (1 << 30)) {
		for(int i = 0; i < tones; ++i) best[i] = pcs[i] + 12;
	}
	if(!stab) {
		memcpy(c->last_voicing, best, (size_t)tones * sizeof best[0]);
		c->voicing_n = tones;
	}
	int l = cut_rows > 0 ? imin(len, cut_rows) : len;
	const NoteFx fx = c->chip_chords ? FX_ARP : stab ? FX_STAB : FX_NONE;
	if(sus && !stab && l > 8) {
		/* the third (or the tone a step above the root) moves up a step: sus4 */
		int s[4];
		memcpy(s, best, sizeof s);
		const int third = pitch(c, c->key, root + 2) % 12;
		for(int i = 0; i < tones; ++i) {
			if(((s[i] % 12) + 12) % 12 == ((third % 12) + 12) % 12) s[i] = pitch(c, c->key, root + 3) + 12 * ((s[i] - pitch(c, c->key, root + 2)) / 12);
		}
		add_n(c, bar, row, CH_PAD, s, tones, 8, vel, lay, fx);
		last_event(c)->gate = gate;
		row += 8;
		l -= 8;
		bar += (u32)(row / 16);
		row %= 16;
	}
	add_n(c, bar, row, CH_PAD, best, tones, l, vel, lay, fx);
	last_event(c)->gate = gate;
}

/* The reference track's arp: a wide 4-note cell per beat (the fifth an
 * octave up, the third low, the root an octave up, the fifth), about
 * 1.5 octaves across; over a chord held into the next bar a pedal (the
 * high fifth on every other step); an added ninth on major chords now
 * and then; and the cell's first note jumps an octave on every other
 * bar's downbeat. `step` counts the arp's notes, `k` its bars. */
static int wide_deg(const Composer *c, const PlanItem *p, int b, int step, int k)
{
	const int root = chord_at(c, p, b);
	const bool held = b + 1 < (int)p->sec.bars && chord_id(c, p, b + 1) == chord_id(c, p, b) && chord_id(c, p, imax(0, b - 1)) == chord_id(c, p, b);
	const bool major = pitch(c, c->key, root + 2) - pitch(c, c->key, root) == 4;
	const int s = step % 4;
	const int *cell = c->wide_cells[(k / 8) % 2]; /* two cells, alternating every 8 bars */
	int deg = root + cell[s];
	if(held && s % 2 == 0) deg = root + 11;                     /* the pedal */
	if(held && s == 3) deg = root + ((step / 4) % 2 ? 9 : 4);   /* the moving voice under it */
	if(major && s == 3 && (step / 4) % 4 == 3) deg = root + 8;  /* the ninth (an octave up) */
	if(s == 0 && (step / 4) % 4 == 0 && k % 2 == 1) deg += 7;   /* octave jump */
	return deg;
}

/* The crescendo devices that need their own events (the rolls, risers
 * and filter sweeps are emitted in render_section's bar loop). */
static void render_build(Composer *c, int index)
{
	Song *s = c->s;
	const PlanItem *p = &c->plan[index];
	if(index + 1 >= c->nplan) return;
	const u8 T = p->out;
	const int bars = (int)p->sec.bars;
	const int nb = imax(1, build_bars(c, p));
	const int from = bars - nb;
	const u32 b0 = p->sec.start_bar;
	const u32 t0 = (b0 + (u32)from) * ticks_per_bar, t1 = (b0 + (u32)bars) * ticks_per_bar;
	const u32 mark = s->nevents; /* everything from here goes to post */
	switch(T) {
	case TR_TOM_CASCADE:
		/* 8ths high-mid in the first bars, then 16th cascades high -> low each beat */
		for(int bi = from; bi < bars; ++bi) {
			const bool last = bi == bars - 1;
			for(int row = 0; row < 16; row += last ? 1 : 2) {
				static const u8 casc[4] = {DR_TOM_HIGH, DR_TOM_HIGH, DR_TOM_MID, DR_TOM_LOW};
				const u8 tom = last ? casc[row % 4] : (u8)(row % 4 == 0 ? DR_TOM_HIGH : DR_TOM_MID);
				add1(c, b0 + (u32)bi, row, CH_FX, tom, 2, 60 + 50 * ((bi - from) * 16 + row) / (nb * 16), LAYER_ALWAYS);
			}
		}
		break;
	case TR_LAYER_IN: {
		/* instruments enter one by one: hats, then kick, then off-beat bass, then a snare roll */
		add_mute(c, t0, t1, (1u << CH_KICK) | (1u << CH_HATS) | (1u << CH_BASS) | (1u << CH_SNARE));
		for(int bi = from; bi < bars; ++bi) {
			const int stage = (bi - from) * 4 / nb;
			const u32 bar = b0 + (u32)bi;
			const int root = chord_at(c, p, bi);
			for(int row = 0; row < 16; row += 2) add1(c, bar, row, CH_HATS, row % 4 == 2 ? DR_HAT_OPEN : DR_HAT_CLOSED, 1, 60 + 6 * stage, LAYER_ALWAYS);
			if(stage >= 1) {
				for(int row = 0; row < 16; row += 4) add1(c, bar, row, CH_KICK, DR_KICK, 2, 104 + 4 * stage, LAYER_ALWAYS);
			}
			if(stage >= 2) {
				for(int row = 2; row < 16; row += 4) add1(c, bar, row, CH_BASS, pitch(c, c->key - 12, root), 2, 100, LAYER_ALWAYS);
			}
		}
		snare_roll(c, b0 + (u32)(bars - imax(1, nb / 4)), imax(1, nb / 4), 0, DR_SNARE);
		break;
	}
	case TR_HAT_RAMP:
		add_mute(c, t0, t1, 1u << CH_HATS);
		for(int bi = from; bi < bars; ++bi) {
			const int stage = (bi - from) * 3 / nb; /* 8ths, 16ths, 16ths + open 8ths */
			for(int row = 0; row < 16; row += stage == 0 ? 2 : 1) {
				const int vel = 50 + 60 * ((bi - from) * 16 + row) / (nb * 16);
				add1(c, b0 + (u32)bi, row, CH_HATS, stage == 2 && row % 2 == 0 ? DR_HAT_OPEN : DR_HAT_CLOSED, 1, vel, LAYER_ALWAYS);
			}
		}
		break;
	case TR_ARP_CLIMB:
		add_mute(c, t0, t1, 1u << CH_ARP);
		for(int bi = from; bi < bars; ++bi) {
			const int k = bi - from;
			const int rate = k * 2 < nb ? 2 : 1; /* 8ths, then 16ths */
			const int root = chord_at(c, p, bi);
			const int shift = 12 * (k * 2 / imax(1, nb)) - 12; /* an octave up halfway */
			int idx = 0;
			for(int row = 0; row < 16; row += rate) {
				const int tone = c->arp_order.v[idx++ % c->arp_order.n];
				const int deg = root + (tone == 3 ? 7 : tone == 4 ? 9 : tone * 2) + (row / 4) % 2;
				const int vel = 56 + 50 * (k * 16 + row) / (nb * 16);
				add1(c, b0 + (u32)bi, row, CH_ARP, pitch(c, c->arp_base + shift, deg), 1, vel, LAYER_ALWAYS);
			}
		}
		break;
	case TR_STAB_RAMP:
		add_mute(c, t0, t1, 1u << CH_PAD);
		for(int bi = from; bi < bars; ++bi) {
			const int stage = (bi - from) * 3 / nb; /* quarters, 8ths, 16ths */
			const int step = stage == 0 ? 4 : stage == 1 ? 2 : 1;
			for(int row = 0; row < 16; row += step) {
				emit_chord(c, b0 + (u32)bi, row, chord_at(c, p, bi), 1, 64 + 50 * ((bi - from) * 16 + row) / (nb * 16), LAYER_ALWAYS, 0, true, 0, false);
			}
		}
		break;
	case TR_PAD_SWELL: {
		/* A crescendo that follows the harmony: the first chord swells
		 * from silence over its own length, each later chord of the build
		 * enters a step louder (it used to hold the first chord across
		 * the build's chord changes). The extra chords draw no random
		 * numbers and leave the voice leading as it was, so the rest of
		 * the song is unchanged. */
		add_mute(c, t0, t1, 1u << CH_PAD);
		int saved[4] = {0, 0, 0, 0};
		int saved_n = 0;
		for(int bi = from; bi < bars;) {
			const int cid = chord_id(c, p, bi);
			int end = bi + 1;
			while(end < bars && chord_id(c, p, end) == cid) ++end;
			const int vel = 60 + 50 * (end - from) / nb;
			if(bi == from) {
				emit_chord(c, b0 + (u32)bi, 0, chord_at(c, p, bi), end >= bars ? nb * 16 : (end - bi) * 16, end >= bars ? 110 : vel, LAYER_ALWAYS, 0, false, 0, false);
				last_event(c)->fx = FX_SWELL;
				memcpy(saved, c->last_voicing, sizeof saved);
				saved_n = c->voicing_n;
				c->no_jitter = true;
			} else {
				emit_chord(c, b0 + (u32)bi, 0, chord_at(c, p, bi), (end - bi) * 16, vel, LAYER_ALWAYS, 0, false, 0, false);
			}
			bi = end;
		}
		c->no_jitter = false;
		memcpy(c->last_voicing, saved, sizeof saved);
		c->voicing_n = saved_n;
		break;
	}
	case TR_KICK_DOUBLE: {
		/* quarters to 8ths, then the kick roll: the last beat in 16ths
		 * (wartime: the last two beats, the hardtrance kick roll) */
		const int kb = imin(nb, 2);
		const int roll_from = c->war ? 8 : 12;
		add_mute(c, t1 - (u32)kb * ticks_per_bar, t1, 1u << CH_KICK);
		for(int bi = bars - kb; bi < bars; ++bi) {
			const bool last = bi == bars - 1;
			for(int row = 0; row < 16; row += (last && row >= roll_from) ? 1 : 2) {
				add1(c, b0 + (u32)bi, row, CH_KICK, DR_KICK, 1, row % 4 == 0 ? 120 : 96 + row, LAYER_ALWAYS);
			}
		}
		break;
	}
	case TR_LEAD_CLIMB: {
		/* the melody stopped nb bars early (render_melody): a scale run up
		 * in 8ths, then the dominant held and bent up a fourth onto the tonic */
		const int run_bars = imax(1, nb - 1);
		const int start = c->last_lead >= 0 ? c->last_lead : pitch(c, c->lead_base, 0);
		int deg = 0;
		int base = pitch(c, c->lead_base, 0);
		while(base > start + 2) base -= 12;
		while(base < start - 9) base += 12;
		const int steps = run_bars * 8;
		for(int i = 0; i < steps; ++i) {
			const int bi = from + i / 8;
			deg = i * 7 / steps; /* one octave over the run */
			const int note = pitch(c, base, deg);
			add1(c, b0 + (u32)bi, (i % 8) * 2, CH_LEAD, imin(note, 96), 2, 70 + 30 * i / steps, LAYER_ALWAYS);
		}
		const int dom = pitch(c, base, 4); /* bends a fourth up, onto the tonic an octave above the run's start */
		const int slide[2] = {dom, dom + 5};
		add_n(c, b0 + (u32)(bars - 1), 0, CH_LEAD, slide, 2, 16, 100, LAYER_ALWAYS, FX_SLIDE_UP);
		break;
	}
	default: break;
	}
	/* move the device's events to post */
	const u32 n = s->nevents - mark;
	if(n) {
		if(c->npost + n > c->cap_post) {
			u32 cap = c->cap_post ? c->cap_post : 256;
			while(cap < c->npost + n) cap *= 2;
			NoteEvent *ev = realloc(c->post, (size_t)cap * sizeof *ev);
			if(!ev) {
				c->oom = true;
				s->nevents = mark;
				return;
			}
			c->post = ev;
			c->cap_post = cap;
		}
		memcpy(c->post + c->npost, s->events + mark, (size_t)n * sizeof(NoteEvent));
		c->npost += n;
	}
	s->nevents = mark;
}

/* --- the arp solo ------------------------------------------------------------ */
/* After the reference track (analysed for docs/14): an
 * 8-bar stretch (16 in wartime and long songs) after the first chorus or
 * drop where the arpeggio carries the melody. A one-bar breath (no kick,
 * no bass) introduces it; kick, off-beat bass, hats and a backbeat come
 * back under it; the pads hold the chords quietly; no lead, no counter.
 * The arp starts in 8ths, runs wide 16th cells from bar 3 with a
 * crescendo (the filter opens with velocity), and its last bar jumps an
 * octave on every beat. It is planned after the arrangement and drawn
 * from its own generator, and the sections around it render exactly as
 * before, so a seed keeps the rest of its song. */
static void rebuild_sections(Composer *c)
{
	c->s->nsections = 0;
	for(int i = 0; i < c->nplan; ++i) c->s->sections[c->s->nsections++] = c->plan[i].sec;
}
static bool plan_insert(Composer *c, int at, PlanItem it)
{
	if(c->nplan == MAX_PLAN) {
		c->oom = true;
		return false;
	}
	memmove(&c->plan[at + 1], &c->plan[at], (size_t)(c->nplan - at) * sizeof c->plan[0]);
	c->plan[at] = it;
	++c->nplan;
	return true;
}

static void insert_arp_solo(Composer *c)
{
	Song *s = c->s;
	static const float p_solo[MOOD_COUNT] = {0.2f, 0.4f, 0.35f, 0.45f, 0.45f, 0.3f};
	c->arp_wide = rng_chance(&c->sr, 0.45f);
	const bool want = rng_chance(&c->sr, p_solo[s->mood]) || c->force_solo;
	const float pick = rng_uniform(&c->sr);
	const bool two = rng_chance(&c->sr, 0.5f);
	if(!want || s->total_bars < 40) return; /* not in intermissions */
	int j = 0;
	if(c->force_solo) { /* the menu: the solo early, after the first section that allows it */
		for(int i = 1; i + 1 < c->nplan && j == 0; ++i) {
			if(c->plan[i].sec.kind != SEC_OUTRO && c->plan[i - 1].pivot_deg < 0 && c->plan[i].shift == c->plan[i - 1].shift) j = i;
		}
	} else {
		for(int i = 1; i + 1 < c->nplan; ++i) {
			if(big(c->plan[i].sec.kind)) {
				j = i + 1;
				break;
			}
		}
	}
	if(j == 0 || c->plan[j].sec.kind == SEC_OUTRO) return;
	PlanItem *prev = &c->plan[j - 1];
	if(prev->pivot_deg >= 0 || c->plan[j].shift != prev->shift) return; /* not into the key change */
	const int bars = c->war || song_duration(s) > 300.0 ? 16 : 8;
	if((s->total_bars + (u32)bars) * 240.0 / (double)s->bpm > 598.0) return;
	/* progressions in the reference's spirit: held chords, the relative
	 * major, dorian ii, a major V, VII held before the return */
	static const Progression solo_progs[] = {
		{MINORISH, {0, 0, 5, 6}},                                                                              /* i i VI VII */
		{BIT(M_AEOLIAN) | BIT(M_DORIAN), {0, 1, 5, 4}, {C_HOME, C_DORIAN, C_HOME, C_HARM}},                   /* i ii VI V */
		{BIT(M_AEOLIAN) | BIT(M_DORIAN) | BIT(M_HARMONIC), {2, 6, 5, 5}},                                     /* III VII VI VI (relative major) */
		{BIT(M_AEOLIAN) | BIT(M_DORIAN), {0, 1, 6, 6}, {C_HOME, C_DORIAN, C_HOME, C_HOME}},                   /* i ii VII VII */
		{BIT(M_AEOLIAN) | BIT(M_PHRYGIAN), {0, 5, 2, 6}},                                                     /* i VI III VII */
		{MAJORISH, {0, 0, 3, 4}},                                                                              /* I I IV V */
		{BIT(M_IONIAN) | BIT(M_MIXOLYDIAN), {5, 3, 0, 4}},                                                    /* vi IV I V */
		{BIT(M_IONIAN) | BIT(M_MIXOLYDIAN) | BIT(M_LYDIAN), {0, 5, 6, 0}, {C_HOME, C_MINOR, C_MINOR, C_HOME}}, /* I bVI bVII I */
	};
	const Progression *fit[countof(solo_progs)];
	size_t nfit = 0;
	for(size_t i = 0; i < countof(solo_progs); ++i) {
		if(solo_progs[i].mode_mask & BIT(s->mode)) fit[nfit++] = &solo_progs[i];
	}
	PlanItem solo = plan_item();
	solo.sec = (Section){SEC_ARP_SOLO, prev->sec.start_bar + prev->sec.bars, (u32)bars};
	if(!nfit) solo.prog = c->prog_b;
	else {
		const size_t at = (size_t)(pick * (float)nfit);
		solo.prog = fit[nfit - 1 < at ? nfit - 1 : at];
	}
	solo.bars_per_chord = bars >= 16 || two ? 2 : 1;
	solo.shift = prev->shift;
	solo.downlift = prev->downlift; /* the section after the solo keeps its entry */
	solo.impact = prev->impact;
	solo.out = TR_PLAIN;
	solo.end_tonic = ends_on_tonic(solo.prog, ((bars - 1) / solo.bars_per_chord) % 4);
	prev->orig_next = (u8)c->plan[j].sec.kind;
	const char *after = section_name(prev->sec.kind);
	if(!plan_insert(c, j, solo)) return;
	c->solo_bar = solo.sec.start_bar;
	for(int i = j + 1; i < c->nplan; ++i) c->plan[i].sec.start_bar += (u32)bars;
	if(c->mod_shift && c->mod_bar >= c->plan[j].sec.start_bar) c->mod_bar += (u32)bars;
	rebuild_sections(c);
	s->total_bars += (u32)bars;
	transitions_add(c, c->ntransitions ? "; " : "");
	transitions_add(c, "arp solo after ");
	transitions_add(c, after);
}

static void render_arp_solo(Composer *c, int index)
{
	const PlanItem *p = &c->plan[index];
	Rng *const keep_jit = c->jit;
	c->jit = &c->sr; /* no draw from the main generators */
	int keep_voicing[4];
	memcpy(keep_voicing, c->last_voicing, sizeof keep_voicing);
	const int keep_n = c->voicing_n;
	c->swing = c->song_swing;
	const bool calm = c->s->mood == MOOD_CALM;
	const int bars = (int)p->sec.bars;
	const u32 b0 = p->sec.start_bar;
	add1(c, b0, 0, CH_FX, DR_CRASH, 16, 84, LAYER_ALWAYS);
	for(int bi = 0; bi < bars; ++bi) {
		const u32 bar = b0 + (u32)bi;
		const int root = chord_at(c, p, bi);
		const bool breath = bi == 0;
		const bool last_bar = bi == bars - 1;
		/* drums: the breath, then four-on-the-floor, hats, a backbeat from bar 3 */
		if(!breath && !(calm && bi < 2)) {
			for(int row = 0; row < 16; row += 4) add1(c, bar, row, CH_KICK, DR_KICK, 2, 116, LAYER_ALWAYS);
		}
		for(int row = 0; row < 16; row += 2) {
			if(row % 4 == 2 && !breath) add1(c, bar, row, CH_HATS, DR_HAT_OPEN, 2, 78, LAYER_ALWAYS);
			else add1(c, bar, row, CH_HATS, DR_HAT_CLOSED, 1, row % 4 == 0 ? 58 : 68, LAYER_ALWAYS);
		}
		if(bi >= 2 && !(last_bar && bars > 1)) {
			add1(c, bar, 4, CH_SNARE, c->backbeat, 2, calm ? 84 : 100, LAYER_ALWAYS);
			add1(c, bar, 12, CH_SNARE, c->backbeat, 2, calm ? 84 : 100, LAYER_ALWAYS);
		}
		if(last_bar) {
			add1(c, bar, 4, CH_SNARE, c->backbeat, 2, 100, LAYER_ALWAYS);
			for(int row = 12; row < 16; ++row) add1(c, bar, row, CH_SNARE, DR_SNARE, 1, 70 + 10 * (row - 12), LAYER_ALWAYS); /* a 16th pickup */
		}
		/* bass: off-beat roots (calm: the root held) */
		if(!breath) {
			if(calm) add1(c, bar, 0, CH_BASS, pitch(c, c->key - 12, root), 14, 96, LAYER_ALWAYS);
			else {
				for(int row = 2; row < 16; row += 4) add1(c, bar, row, CH_BASS, pitch(c, c->key - 12, root), 2, 104, LAYER_ALWAYS);
			}
		}
		/* pads: the chords held, quietly */
		const bool new_chord = bi == 0 || chord_id(c, p, bi - 1) != chord_id(c, p, bi);
		if(new_chord) {
			int span = 1;
			while(bi + span < bars && chord_id(c, p, bi + span) == chord_id(c, p, bi)) ++span;
			emit_chord(c, bar, 0, root, span * 16 - 1, 54, LAYER_ALWAYS, 0, false, 0, false);
		}
	}
	/* the arp as the lead: 8ths for two bars (calm: throughout), then wide
	 * 16th cells, crescendo, the last bar jumping an octave every beat */
	const int base = c->key + 12;
	int step = 0;
	for(int pos = 0; pos < bars * 16;) {
		const int b = pos / 16, row = pos % 16;
		const int rate = calm || b < 2 ? 2 : 1;
		int deg = wide_deg(c, p, b, step, b);
		if(b == bars - 1 && step % 4 == 0) deg += 7;
		int n = pitch(c, base, deg);
		while(n > 90) n -= 12;
		const int vel = 74 + 40 * pos / (bars * 16) + (row % 4 == 0 ? 8 : 0);
		add1(c, b0 + (u32)b, row, CH_ARP, n, rate == 2 ? 2 : 1, imin(127, vel), LAYER_ALWAYS);
		++step;
		pos += rate;
	}
	add1(c, b0 + (u32)bars, 0, CH_FX, DR_CRASH, 16, 100, LAYER_ALWAYS);
	memcpy(c->last_voicing, keep_voicing, sizeof keep_voicing);
	c->voicing_n = keep_n;
	c->jit = keep_jit;
}

/* --- the euro lead ----------------------------------------------------------- */
/* After the fifth reference (data/boomerang.opus, a late-90s eurodance /
 * happy-hardcore hit, ~191 BPM; its sound only, none of its notes): a
 * bright saw with a second saw an octave up (even harmonics: h2 about
 * -1 dB), a little unison detune (its harmonics are 12-15 Hz wide), a
 * 3 ms attack with a short filter blip, then a held, organ-like sustain,
 * light 7 Hz vibrato, no glide, wide stereo chorus. It replaces the lead
 * in most eurodance songs and in some other upbeat ones; drawn after the
 * arrangement, so the compositions are
 * unchanged (only the sound of the lead). Never in wartime. Its own
 * generator, so the solo and choir renders keep their draws. */
static void choose_euro_lead(Composer *c)
{
	Song *s = c->s;
	float p = 0.0f;
	if(c->style == ST_EURODANCE) p = 0.7f;
	else if(s->mood == MOOD_TRIUMPH && (c->style == ST_TECHNO || c->style == ST_CHIP || c->style == ST_SYNTHWAVE)) p = 0.4f;
	else if((s->mood == MOOD_TENSE || s->mood == MOOD_CRISIS) && c->style == ST_TECHNO) p = 0.15f;
	Rng er = rng_make(s->seed ^ 0x1F83D9ABFB41BD6Bull); /* its own generator: the solo / choir renders draw from sr */
	const bool euro = rng_chance(&er, p) && !c->war;
	if(!euro) return;
	c->euro_lead = true;
	Patch *l = &s->patches[CH_LEAD];
	*l = patch_default();
	l->w1 = WAVE_SAW, l->unison = 1, l->spread = 14, l->w2 = WAVE_SAW, l->semi2 = 12, l->mix2 = 0.6f, l->detune = 4;
	l->poles4 = true, l->cutoff = 5500, l->resonance = 0.15f, l->key_track = 0.4f, l->fenv = 0.8f, l->fdecay = 0.05f;
	l->attack = 0.004f, l->decay = 0.06f, l->sustain = 0.72f, l->release = 0.16f;
	l->vib_depth = 0.1f, l->vib_rate = 6.8f, l->vib_delay = 0.15f;
	l->glide = 0.0f;
	l->gain = 1.7f; /* the held-note level of the other leads (measured on a held A4) */
	s->mix.chorus[CH_LEAD] = 0.5f;
	s->mix.reverb[CH_LEAD] = maxf(s->mix.reverb[CH_LEAD], 0.25f);
}

/* --- the choir block ---------------------------------------------------------- */
/* After the fourth reference (data/eclipse.mp3, 160 BPM): late in the
 * track the kick turns to the off-beats ("mini drops"), then drops out
 * but for pickups every two bars while the synth carries on alone; a
 * choir comes in over the bare low end; a roll and a riser; then the
 * choir drop with the kick back. Inserted before the last chorus /
 * drop, from the arp devices' generator, like the arp solo, so the
 * rest of the song is untouched. Not in calm songs. */
static void insert_choir(Composer *c)
{
	Song *s = c->s;
	static const float p_choir[MOOD_COUNT] = {0.0f, 0.25f, 0.3f, 0.3f, 0.4f, 0.2f};
	const bool want = rng_chance(&c->sr, p_choir[s->mood]);
	if(!want || s->total_bars < 40) return;
	int j = 0;
	for(int i = c->nplan; i-- > 2;) {
		if(big(c->plan[i].sec.kind)) {
			j = i;
			break;
		}
	}
	if(j < 2) return;
	PlanItem *prev = &c->plan[j - 1];
	if(prev->pivot_deg >= 0 || prev->shift != c->plan[j].shift || prev->sec.kind == SEC_ARP_SOLO || prev->orig_next != 255) return;
	const int bars = c->war || song_duration(s) > 300.0 ? 24 : 16;
	if((s->total_bars + (u32)bars) * 240.0 / (double)s->bpm > 598.0) return;
	PlanItem blk = plan_item();
	blk.sec = (Section){SEC_CHOIR, prev->sec.start_bar + prev->sec.bars, (u32)bars};
	blk.prog = c->plan[j].prog; /* the harmony of the drop it leads into */
	blk.bars_per_chord = 2;
	blk.shift = prev->shift;
	blk.downlift = prev->downlift;
	blk.impact = prev->impact;
	blk.out = TR_PLAIN;
	blk.end_tonic = ends_on_tonic(blk.prog, ((bars - 1) / 2) % 4);
	prev->orig_next = (u8)c->plan[j].sec.kind;
	const char *before = section_name(c->plan[j].sec.kind);
	if(!plan_insert(c, j, blk)) return;
	for(int i = j + 1; i < c->nplan; ++i) c->plan[i].sec.start_bar += (u32)bars;
	if(c->mod_shift && c->mod_bar >= c->plan[j].sec.start_bar) c->mod_bar += (u32)bars;
	if(c->solo_bar != ~0u && c->solo_bar >= c->plan[j].sec.start_bar) c->solo_bar += (u32)bars;
	rebuild_sections(c);
	s->total_bars += (u32)bars;
	transitions_add(c, c->ntransitions ? "; " : "");
	transitions_add(c, "choir block before ");
	transitions_add(c, before);
}

/* A choir chord in its own register (the third and fifth on top), not
 * touching the pads' voice leading. */
static void choir_chord(Composer *c, u32 bar, int row, int root, int len, int vel, NoteFx fx)
{
	int a = pitch(c, c->key, root);
	while(a < 55) a += 12;
	while(a > 66) a -= 12;
	int b = pitch(c, c->key, root + 2);
	while(b <= a) b += 12;
	int d = pitch(c, c->key, root + 4);
	while(d <= b) d += 12;
	const int notes[4] = {a, b, d, a + 12};
	add_n(c, bar, row, CH_CHOIR, notes, 4, len, vel, LAYER_ALWAYS, fx);
}

static void render_choir(Composer *c, int index)
{
	const PlanItem *p = &c->plan[index];
	Rng *const keep_jit = c->jit;
	c->jit = &c->sr;
	c->swing = 0;
	const int bars = (int)p->sec.bars;
	const u32 b0 = p->sec.start_bar;
	/* phases: mini drops, the synth alone, the choir enters, the choir drop */
	const int q = bars / 4;
	const int p1 = q, p2 = 2 * q, p3 = bars - (bars >= 24 ? 8 : 4); /* 16 bars: 4/4/4/4; 24 bars: 6/6/4/8 */
	int keep_voicing[4];
	memcpy(keep_voicing, c->last_voicing, sizeof keep_voicing);
	const int keep_n = c->voicing_n;
	for(int bi = 0; bi < bars; ++bi) {
		const u32 bar = b0 + (u32)bi;
		const int root = chord_at(c, p, bi);
		const bool drop = bi >= p3;
		const bool new_chord = bi == 0 || chord_id(c, p, bi - 1) != chord_id(c, p, bi);
		/* kick: off-beat 8ths (mini drops) -> out, a pickup every 2 bars -> four-on-the-floor in the drop */
		if(bi < p1) {
			for(int row = 2; row < 16; row += 4) add1(c, bar, row, CH_KICK, DR_KICK, 2, 112, LAYER_ALWAYS);
		} else if(!drop) {
			if(bi % 2 == 1 && bi != p3 - 1)
				for(int row = 12; row < 16; ++row) add1(c, bar, row, CH_KICK, DR_KICK, 1, 84 + 8 * (row - 12), LAYER_ALWAYS);
		} else {
			for(int row = 0; row < 16; row += 4) add1(c, bar, row, CH_KICK, DR_KICK, 2, 120, LAYER_ALWAYS);
		}
		/* hats: thin while the kick is away, 16ths in the drop */
		for(int row = 0; row < 16; row += drop ? 1 : 2)
			add1(c, bar, row, CH_HATS, row % 4 == 2 ? DR_HAT_OPEN : DR_HAT_CLOSED, 1, drop ? (row % 4 == 2 ? 86 : 60) : 54, LAYER_ALWAYS);
		if(drop || bi < p1) {
			add1(c, bar, 4, CH_SNARE, c->backbeat, 2, drop ? 112 : 96, LAYER_ALWAYS);
			add1(c, bar, 12, CH_SNARE, c->backbeat, 2, drop ? 112 : 96, LAYER_ALWAYS);
		}
		/* bass: only with the kick (the choir enters over a bare low end) */
		if(drop || bi < p1) {
			for(int row = 2; row < 16; row += 4) add1(c, bar, row, CH_BASS, pitch(c, c->key - 12, root), 2, 106, LAYER_ALWAYS);
		}
		/* the repeating synth: the arp's wide cells throughout */
		for(int row = 0; row < 16; ++row) {
			int deg = wide_deg(c, p, bi, bi * 16 + row, bi);
			int n = pitch(c, c->key + 12, deg);
			while(n > 88) n -= 12;
			add1(c, bar, row, CH_ARP, n, 1, (bi >= p1 && bi < p2 ? 96 : 76) + (row % 4 == 0 ? 8 : 0), LAYER_ALWAYS);
		}
		/* the choir: swells in from the third phase, held chords, full in the drop */
		if(bi >= p2 && (new_chord || bi == p3)) {
			int span = 1;
			while(bi + span < bars && chord_id(c, p, bi + span) == chord_id(c, p, bi) && !(bi < p3 && bi + span == p3)) ++span;
			choir_chord(c, bar, 0, root, span * 16 - 1, drop ? 118 : 92, bi == p2 ? FX_SWELL : FX_NONE);
		}
		if(drop && new_chord) emit_chord(c, bar, 0, root, 16 * imax(1, imin(2, bars - bi)) - 1, 60, LAYER_ALWAYS, 0, false, 0, false);
		/* the build into the drop: a 16th snare roll and a riser */
		if(bi == p3 - 1) {
			for(int row = 0; row < 16; ++row) add1(c, bar, row, CH_SNARE, DR_SNARE, 1, 60 + 3 * row, LAYER_ALWAYS);
			add1(c, bar, 0, CH_FX, DR_RISER, 16, 96, LAYER_ALWAYS);
		}
		if(bi == p3) {
			add1(c, bar, 0, CH_FX, DR_CRASH, 16, 110, LAYER_ALWAYS);
			add1(c, bar, 0, CH_FX, DR_IMPACT, 16, 104, LAYER_ALWAYS);
		}
	}
	memcpy(c->last_voicing, keep_voicing, sizeof keep_voicing);
	c->voicing_n = keep_n;
	c->jit = keep_jit;
}

/* --- melody -------------------------------------------------------------------- */
/* A 2-bar cell of a motif, placed on the harmony from bar `bi`: degrees
 * shift with the chord root (diatonic sequence, wrapped to +-3 steps),
 * plus `steps` (melodic sequence); strong-beat notes snap to chord tones;
 * the ending is chosen by phrase position. Notes go to the phrase, which
 * flush_phrase() fits into the lead's register. */
enum { END_KEEP, END_OPEN, END_CLOSE };
enum {
	OP_KEEP,       /* as written */
	OP_INVERT,     /* contour mirrored around the first note */
	OP_RETRO_TAIL, /* bar 2 sings bar 2's pitches backwards */
	OP_CADENCE,    /* bar 1 of the motif, then a long resolving note */
};
typedef struct {
	int steps;
	int op;
	int end;
	bool vary; /* drop an inner note */
} Cell;

/* A stable resolution near `deg` that belongs to the chord on `root`:
 * the tonic, else the key's fifth, else its third (else any chord tone). */
static int resolve(int deg, int root, bool final)
{
	int best = nearest_chord_tone(deg, root), best_v = -100;
	for(int d = -4; d <= 4; ++d) {
		const int c = deg + d;
		if(!is_chord_tone(c, root)) continue;
		const int pc = ((c % 7) + 7) % 7;
		const int stable = pc == 0 ? (final ? 5 : 3) : pc == 4 ? 2 : pc == 2 ? 1 : 0;
		if(!stable) continue;
		const int v = stable * 2 - abs(d);
		if(v > best_v) best_v = v, best = c;
	}
	return best;
}

static bool open_ok(int x, int croot, int prev) { return is_chord_tone(x, croot) && ((x - croot) % 7 + 7) % 7 != 0 && x != prev; }

static void place_motif(Composer *c, const Motif *mt, int motif_root, const PlanItem *p, int bi, int vel, Cell cl, int stretch, bool dbl)
{
	const int bars = (int)p->sec.bars;
	if(bi >= bars || !mt->count) return;
	const int root = chord_at(c, p, bi);
	int shift = ((root - motif_root) % 7 + 7) % 7;
	if(shift > 3) shift -= 7;
	typedef struct {
		int row, len, deg; /* row from the cell start (stretched) */
	} Pn;
	Pn notes[countof(mt->n) + 1];
	int nn = 0;
	int tail[countof(mt->n)];
	int ntail = 0;
	for(int i = 0; i < mt->count; ++i) {
		if(mt->n[i].row >= 16) tail[ntail++] = mt->n[i].deg;
	}
	int tail_i = ntail;
	int last_row = -1;
	for(int i = 0; i < mt->count; ++i) {
		const MotifNote *mn = &mt->n[i];
		if(cl.vary && i == mt->count / 2 && mt->count > 3) continue;
		if(cl.op == OP_CADENCE && mn->row >= 16) break;
		int d = mn->deg;
		if(cl.op == OP_INVERT && i > 0) d = 2 * mt->n[0].deg - d;
		if(cl.op == OP_RETRO_TAIL && mn->row >= 16 && tail_i > 0) d = tail[--tail_i];
		const int row = mn->row * stretch;
		if(bi + row / 16 >= bars) break; /* the section ends: the last placed note takes the ending */
		notes[nn++] = (Pn){row, mn->len * stretch, d + motif_root + shift + cl.steps};
		last_row = mn->row;
	}
	/* A transformed cell stays in the register of the plain one: an
	 * inversion or a sequence never drifts by more than half an octave. */
	if(nn) {
		float mean = 0, ref = 0;
		for(int i = 0; i < nn; ++i) mean += (float)notes[i].deg;
		for(int i = 0; i < mt->count; ++i) ref += (float)(mt->n[i].deg + motif_root + shift);
		mean /= (float)nn;
		ref /= (float)mt->count;
		const int oct = mean - ref > 3.5f ? -7 : mean - ref < -3.5f ? 7 : 0;
		for(int i = 0; i < nn; ++i) notes[i].deg += oct;
	}
	if(cl.op == OP_CADENCE) {
		const int r_end = last_row <= 13 ? 16 : 20;
		if(bi + (r_end * stretch) / 16 < bars) {
			const int prev = !nn ? motif_root + shift + cl.steps : notes[nn - 1].deg;
			if(nn) notes[nn - 1].len = imin(notes[nn - 1].len, r_end * stretch - notes[nn - 1].row);
			notes[nn++] = (Pn){r_end * stretch, (30 - r_end) * stretch, prev};
		}
	}
	for(int i = 0; i < nn; ++i) {
		Pn *n = &notes[i];
		const int croot = chord_at(c, p, bi + n->row / 16);
		const bool strong = (n->row % 16) % 4 == 0;
		if(strong) n->deg = nearest_chord_tone(n->deg, croot);
		/* augmented cells: no repeated pitch, move to a neighbour / another chord tone */
		if(stretch > 1 && i > 0 && n->deg == notes[i - 1].deg) {
			const int dir = i + 1 < nn && notes[i + 1].deg < n->deg ? -1 : 1;
			n->deg = strong ? nearest_chord_tone(n->deg + 2 * dir, croot) : n->deg + dir;
		}
		if(i + 1 == nn && cl.end != END_KEEP) {
			const bool section_end = bi + (n->row + n->len) / 16 >= bars - 1;
			if(cl.end == END_CLOSE) {
				n->deg = resolve(n->deg, croot, section_end);
				n->len = imax(n->len, 30 * stretch - n->row); /* hold the resolution to the cell's end */
			} else {
				/* a non-root chord tone leaves the phrase open; not the note just sung */
				const int prev = i > 0 ? notes[i - 1].deg : 1000;
				for(int d = 0; d <= 3; ++d) {
					const int a = n->deg + d, b = n->deg - d;
					if(open_ok(a, croot, prev)) {
						n->deg = a;
						break;
					}
					if(open_ok(b, croot, prev)) {
						n->deg = b;
						break;
					}
				}
				n->len = imax(n->len, imin(8 * stretch, 30 * stretch - n->row));
			}
		}
		const int room = (bars - bi) * 16 - n->row; /* never past the section's end */
		n->len = iclamp(n->len, 1, imax(1, room));
	}
	++c->cell_id;
	for(int i = 0; i < nn; ++i) {
		if(c->nphrase == MAX_PHRASE) {
			c->oom = true;
			return;
		}
		const Pn *n = &notes[i];
		c->phrase[c->nphrase++] = (LeadNote){p->sec.start_bar + (u32)(bi + n->row / 16), n->row % 16, n->len, pitch(c, c->lead_base, n->deg), vel, dbl, c->cell_id};
	}
}

/* Emits the phrase on the lead: the whole phrase moves by octaves into the
 * lead's window (its contour stays), single stray notes fold in by an
 * octave. `high` asks for the window an octave up (second half of a
 * drop) for low leads; never in wartime (the lead stays low). */
static void flush_phrase(Composer *c, bool high)
{
	if(!c->nphrase) return;
	/* Each cell takes the octave that keeps its notes in the window and
	 * joins the previous note smoothly; a phrase never jumps registers
	 * in the middle of a cell. */
	const int lo = c->lead_lo, hi = c->lead_hi;
	int i = 0;
	int mx = 0;
	while(i < c->nphrase) {
		int j = i;
		while(j < c->nphrase && c->phrase[j].cell == c->phrase[i].cell) ++j;
		int best = 0, best_cost = 1 << 30;
		static const int shifts[] = {0, 12, -12, 24, -24};
		for(size_t si = 0; si < countof(shifts); ++si) {
			const int sh = shifts[si];
			int cost = abs(sh) / 12;
			for(int k = i; k < j; ++k) cost += 100 * (c->phrase[k].note + sh < lo - 2 || c->phrase[k].note + sh > hi + 3);
			if(c->last_lead >= 0) cost += 3 * imax(0, abs(c->phrase[i].note + sh - c->last_lead) - 7);
			if(cost < best_cost) best_cost = cost, best = sh;
		}
		for(int k = i; k < j; ++k) {
			int *note = &c->phrase[k].note;
			*note += best;
			while(*note < lo - 2) *note += 12; /* a stray note folds in */
			while(*note > hi + 3) *note -= 12;
			mx = imax(mx, *note);
		}
		c->last_lead = c->phrase[j - 1].note;
		i = j;
	}
	const int up = high && !c->war && c->lead_class == 0 && mx + 12 <= 80 ? 12 : 0; /* the drop's second half, an octave up */
	for(int k = 0; k < c->nphrase; ++k) {
		const LeadNote *n = &c->phrase[k];
		const int note = n->note + up;
		if(n->dbl && !c->war && note + 12 <= 86) {
			const int two[2] = {note, note + 12};
			add1(c, n->bar, n->row, CH_LEAD, note, n->len, n->vel, LAYER_MAX_HIGH);
			add_n(c, n->bar, n->row, CH_LEAD, two, 2, n->len, n->vel, LAYER_MIN_HIGH, FX_NONE);
		} else {
			add1(c, n->bar, n->row, CH_LEAD, note, n->len, n->vel, LAYER_ALWAYS);
		}
	}
	if(up) c->last_lead = -1;
	c->nphrase = 0;
}

/* Arp variation per song (ar): the verse keeps the song's order, the chorus and
 * the bridge get their own; chord-tone rotations every 2 bars, octave shifts every
 * 4, passing tones, two wide cells. Before, one order and one cell ran through
 * every section of every song. */
static void choose_arp_variation(Composer *c)
{
	Rng *ar = &c->ar;
	c->arp_orders[0] = c->arp_order;
	for(int k = 1; k < 3; ++k) {
		for(int tries = 0; tries < 6; ++tries) {
			c->arp_orders[k] = arp_order_of((int)(rng_next(ar) % arp_order_count));
			if(!arp_order_eq(&c->arp_orders[k], &c->arp_orders[0]) && !arp_order_eq(&c->arp_orders[k], &c->arp_orders[k - 1])) break;
		}
	}
	static const int inversions[][4] = {{0, 0, 0, 0}, {0, 1, 0, 1}, {0, 1, 2, 1}, {0, 0, 1, 1}, {0, 2, 1, 0}, {1, 0, 2, 0}};
	memcpy(c->arp_inv, inversions[rng_next(ar) % 6], sizeof c->arp_inv);
	/* octave shifts: never below the song's arp register when it already sits low */
	static const int octaves[][4] = {{0, 0, 0, 0}, {0, 0, 12, 0}, {0, 12, 0, 12}, {0, 0, -12, 0}, {0, -12, 0, 12}};
	const bool low = c->arp_base < c->key + 24;
	int oi = (int)(rng_next(ar) % 5);
	if(low && (oi == 3 || oi == 4)) oi = 1;
	memcpy(c->arp_oct, octaves[oi], sizeof c->arp_oct);
	c->arp_pass = rng_chance(ar, 0.5f);
	const int c0 = (int)(rng_next(ar) % wide_cell_count);
	int c1 = (int)(rng_next(ar) % wide_cell_count);
	if(c1 == c0) c1 = (c0 + 1) % wide_cell_count;
	memcpy(c->wide_cells[0], wide_cell_table[c0], sizeof c->wide_cells[0]);
	memcpy(c->wide_cells[1], wide_cell_table[c1], sizeof c->wide_cells[1]);
}

static int arp_class(int k)
{
	switch(k) {
	case SEC_CHORUS:
	case SEC_DROP: return 1;
	case SEC_BRIDGE:
	case SEC_BREAKDOWN: return 2;
	default: return 0;
	}
}

/* Arpeggio over the chord tones, `bars` bars from `bi`, starting from
 * `start_row`, every arp_rate rows (a 3-row rate drifts across the
 * bar). `evolve`: a long line in 4-bar arcs, the pattern rotating every
 * 2 bars and climbing a chord tone in the second half of each arc. */
static void place_arp(Composer *c, const PlanItem *p, int bi, int bars, int start_row, int vel, u8 lay, int base_shift, bool evolve)
{
	const int nb = (int)p->sec.bars;
	const ArpOrder *order = !c->arp_orders[arp_class(p->sec.kind)].n ? &c->arp_order : &c->arp_orders[arp_class(p->sec.kind)];
	const int len = order->n;
	const int rot = p->variant > 0 ? 1 : 0; /* a repeated section starts the order elsewhere */
	int idx = 0;
	for(int pos = start_row;; pos += c->arp_rate) {
		const int b = bi + pos / 16, row = pos % 16;
		if(b >= bi + bars || b >= nb) break;
		const int k = b - bi;
		const int root = chord_at(c, p, b);
		int tone = order->v[(idx + rot + (evolve ? k / 2 : 0)) % len];
		if(evolve && k % 4 >= 2) tone = imin(4, tone + 1);
		tone = imin(5, tone + c->arp_inv[(k / 2) % 4]); /* chord-tone rotation per 2 bars */
		const int step = idx;
		++idx;
		int deg = root + (tone % 3) * 2 + 7 * (tone / 3);
		/* a passing tone (the scale step above) closes each 2-bar block */
		if(c->arp_pass && k % 2 == 1 && row + c->arp_rate >= 16) deg += 1;
		if(c->arp_wide && p->sec.start_bar > c->solo_bar) deg = wide_deg(c, p, b, step, b - bi); /* after the solo, the arp keeps its new figure */
		int n = pitch(c, c->arp_base + base_shift + c->arp_oct[(k / 4) % 4], deg);
		while(n > 88) n -= 12;
		while(n < 40) n += 12;
		const int accent = row % 4 == 0 ? 12 : row % 2 == 0 ? 4 : 0;
		const int arc = evolve ? 4 * (k % 4) : 0;
		add1(c, p->sec.start_bar + (u32)b, row, CH_ARP, n, c->arp_len, vel + accent + arc, lay);
	}
}

/* Phrase plans. A 4-cell (8-bar) phrase is statement, sequence,
 * restatement or development, cadence; the last cell of a section always
 * resolves on a held stable tone. */
static void render_melody(Composer *c, int index)
{
	const PlanItem *p = &c->plan[index];
	const bool calm = c->s->mood == MOOD_CALM;
	/* before a drum break the melody ends (with its cadence) 4 bars early */
	const bool more = index + 1 < c->nplan;
	const int bars = (int)p->sec.bars - (more && p->out == TR_DRUM_BREAK ? 4 : more && p->out == TR_LEAD_CLIMB ? imax(1, build_bars(c, p)) : 0);
	const int root_a = c->prog_a->deg[0], root_b = c->prog_b->deg[0];
	const Cell develop = c->dev_invert ? (Cell){0, OP_INVERT, END_KEEP, false} : (Cell){c->dev_steps, OP_RETRO_TAIL, END_KEEP, false};
	const Cell cadence = {c->cad_steps, OP_CADENCE, END_CLOSE, false};
	const int under = c->arp_base >= c->key + 24 ? -12 : 0; /* chorus arps sit under the lead */
	switch(p->sec.kind) {
	case SEC_GROOVE: break; /* drums, bass and percussion only */
	case SEC_ARP_SOLO:      /* rendered by render_arp_solo */
	case SEC_CHOIR: break;  /* rendered by render_choir */
	case SEC_INTRO:
		if(p->quiet) {
			place_arp(c, p, 0, bars, 0, 52, LAYER_ALWAYS, 0, true); /* pads and a long arp, no drums */
		} else if(calm) {
			if(bars >= 8) place_arp(c, p, bars - 4, 4, 0, 58, LAYER_MIN_MID, 0, false);
		} else if(bars >= 4) {
			place_arp(c, p, bars / 2, bars - bars / 2, 0, 54, LAYER_ALWAYS, 0, true);
		}
		break;
	case SEC_VERSE:
		/* call (lead, bars 1-2) and response (arp, bars 3-4), or a
		 * continuous evolving arp under the lead */
		if(c->arp_cont) place_arp(c, p, 0, bars, 0, 50, LAYER_ALWAYS, under, true);
		for(int ph = 0; ph < bars; ph += 4) {
			const int k = (ph / 4) % 4;
			const bool last = ph + 4 >= bars;
			Cell cl = {0, OP_KEEP, END_KEEP, false};
			switch(k) {
			case 0: cl = (Cell){0, OP_KEEP, END_OPEN, false}; break;
			case 1: cl = (Cell){0, OP_KEEP, END_CLOSE, p->variant == 1}; break;
			case 2: cl = (Cell){c->seq_step, OP_KEEP, END_OPEN, false}; break;
			default: cl = cadence; break;
			}
			if(last) cl = cadence;
			if(p->build_up && ph < bars / 2) continue; /* (kept for the random sequence: the build's lead is muted below) */
			place_motif(c, &c->motif_a, root_a, p, ph, 96, cl, 1, false);
			if(!c->arp_cont) place_arp(c, p, ph + 2, 2, 0, 66, LAYER_ALWAYS, 0, false);
			if(k % 2 == 1 || last) flush_phrase(c, false);
		}
		break;
	case SEC_CHORUS:
	case SEC_DROP:
		/* 8-bar melody: motif, its sequence (open), motif or its development, cadence */
		for(int ph = 0; ph < bars; ph += 8) {
			const bool high = p->sec.kind == SEC_DROP && ph >= 8;
			const bool second = ph >= 8 || p->variant == 1;
			Cell cells[4] = {{0, OP_KEEP, END_KEEP, false}, {c->seq_step, OP_KEEP, END_OPEN, true},
			                 second ? develop : (Cell){0, OP_KEEP, END_KEEP, false}, cadence};
			const int n = iclamp((bars - ph) / 2, 0, 4); /* fewer cells before a drum break */
			if(n > 0 && n < 4) cells[n - 1] = cadence;    /* ... and the last one still resolves */
			for(int k = 0; k < n; ++k) place_motif(c, &c->motif_b, root_b, p, ph + 2 * k, k % 2 ? 94 : 100, cells[k], 1, true);
			flush_phrase(c, high);
			place_arp(c, p, ph, imin(8, bars - ph), 0, calm ? 50 : 54, calm ? LAYER_MIN_MID : LAYER_ALWAYS, under, true);
		}
		break;
	case SEC_BRIDGE:
		/* the arp takes the call, the lead answers with sequences of motif A */
		for(int ph = 0; ph < bars; ph += 4) {
			place_arp(c, p, ph, 2, 0, 70, LAYER_ALWAYS, 0, false);
			const bool last = ph + 4 >= bars;
			const int j = ph / 4;
			const Cell cl = last ? cadence : j % 2 == 0 ? (Cell){c->seq_step, OP_KEEP, END_OPEN, true} : (Cell){c->dev_steps, OP_INVERT, END_OPEN, false};
			place_motif(c, &c->motif_a, root_a, p, ph + 2, 88, cl, 1, false);
			if(j % 2 == 1 || last) flush_phrase(c, false);
		}
		break;
	case SEC_BREAKDOWN: {
		/* the theme of the harmony it borrows, augmented: a statement,
		 * then a sequence a third higher that resolves */
		const bool b_theme = p->prog == c->prog_b;
		const Motif *mt = b_theme ? &c->motif_b : &c->motif_a;
		const int mroot = b_theme ? root_b : root_a;
		const int start = bars >= 16 ? 8 : 0;
		for(int ph = start; ph + 4 <= bars; ph += 4) {
			const int j = (ph - start) / 4;
			const bool last = ph + 8 > bars;
			/* long (wartime) breakdowns keep developing: plain, sequence, inversion, ... */
			static const int steps[6] = {0, 0, -1, 2, 1, 0};
			const int st = j == 1 ? c->seq_step : steps[j % 6];
			const int op = j % 3 == 2 ? OP_INVERT : j % 3 == 0 && j > 0 ? OP_RETRO_TAIL : OP_KEEP;
			const Cell cl = last ? (Cell){j == 0 ? 0 : 2, OP_CADENCE, END_CLOSE, false} : (Cell){st, op, END_OPEN, false};
			place_motif(c, mt, mroot, p, ph, 82 + imin(j, 4) * 3, cl, 2, false);
			if(j % 2 == 1) flush_phrase(c, false);
		}
		flush_phrase(c, false);
		if(bars >= 16) place_arp(c, p, 0, 8, 0, 54, LAYER_MIN_MID, 0, !calm);
		else if(!calm && bars >= 8) place_arp(c, p, 0, bars / 2, 0, 48, LAYER_MIN_MID, 0, true);
		break;
	}
	case SEC_OUTRO: {
		place_arp(c, p, 0, imax(0, bars - 4), 0, 56, LAYER_MIN_MID, 0, false);
		if(bars < 4) break;
		/* a last statement, then the cadence: a step onto the tonic, held */
		place_motif(c, &c->motif_a, root_a, p, bars - 4, 80, (Cell){0, OP_KEEP, END_OPEN, false}, 1, false);
		const int prev_note = !c->nphrase ? pitch(c, c->lead_base, 0) : c->phrase[c->nphrase - 1].note;
		int approach = c->cad_deg + 2; /* the cadence chord's tone nearest the tonic by step */
		static const int cands[] = {1, -1, 3, 4};
		for(size_t i = 0; i < countof(cands); ++i) {
			const int d = cands[i];
			if(is_chord_tone(d, c->cad_deg) && ((pitch(c, c->lead_base, d) - prev_note) % 12 + 12) % 12 != 0) {
				approach = d;
				break;
			}
		}
		const int final_deg = approach == 3 || approach == 4 ? 2 : 0;
		int final_note = pitch(c, c->lead_base, final_deg);
		while(final_note < prev_note - 6) final_note += 12;
		while(final_note > prev_note + 6) final_note -= 12;
		int approach_note = pitch(c, c->lead_base, approach) + 12 * ((final_note - pitch(c, c->lead_base, final_deg)) / 12);
		if(approach_note - final_note > 6) approach_note -= 12;
		if(final_note - approach_note > 6) approach_note += 12;
		const u32 b = p->sec.start_bar + (u32)bars;
		++c->cell_id;
		if(c->nphrase + 2 > MAX_PHRASE) {
			c->oom = true;
			break;
		}
		c->phrase[c->nphrase++] = (LeadNote){b - 2, 0, 14, approach_note, 84, false, c->cell_id};
		c->phrase[c->nphrase++] = (LeadNote){b - 1, 0, 16, final_note, 88, false, c->cell_id};
		flush_phrase(c, false);
		break;
	}
	default: break;
	}
}

/* The counter voice: guide tones (the chords' thirds, fifths and
 * sevenths, moving by the smallest step, with suspensions resolving down
 * over the bar line) in choruses, drops, bridges and repeated verses;
 * fills into each 4-bar phrase in verses; in wartime, 16th-gated
 * supersaw chords. */
static int counter_span(const Composer *c, const PlanItem *p, int bi, int stop)
{
	int rows = 0;
	const int cid = chord_id(c, p, bi);
	for(int b = bi; b < stop && (b == bi || (b % p->bars_per_chord != 0 && chord_id(c, p, b) == cid)); ++b) rows += 16;
	return rows;
}
static bool counter_new_chord(const Composer *c, const PlanItem *p, int bi) { return bi == 0 || bi % p->bars_per_chord == 0 || chord_id(c, p, bi - 1) != chord_id(c, p, bi); }
static int counter_choose(const Composer *c, int root, u32 bar, int avoid)
{
	static const int tones[] = {2, 4, 6, 0};
	int best = -1, best_cost = 1 << 30;
	for(size_t i = 0; i < countof(tones); ++i) {
		const int t = tones[i];
		if(t == 6 && !c->seventh) continue;
		for(int oct = -2; oct <= 3; ++oct) {
			const int n = snap(c, pitch(c, c->key, root + t) + 12 * oct, bar);
			if(n < c->ctr_lo || n > c->ctr_hi || n == avoid) continue;
			const int cost = c->last_ctr < 0 ? abs(n - (c->ctr_lo + 7)) : 2 * abs(n - c->last_ctr) + (n == c->last_ctr ? 3 : 0) + (t == 0 ? 2 : 0);
			if(cost < best_cost) best_cost = cost, best = n;
		}
	}
	return best;
}

static void render_counter(Composer *c, int index)
{
	if(c->counter_mode == 0) return;
	const PlanItem *p = &c->plan[index];
	const int k = p->sec.kind;
	const bool calm = c->s->mood == MOOD_CALM;
	const bool more = index + 1 < c->nplan;
	const int stop = (int)p->sec.bars - (more && p->out == TR_DRUM_BREAK ? 4 : 0);
	const u32 b0 = p->sec.start_bar;
	if(c->counter_mode == 3) {
		if(!(big(k) || k == SEC_VERSE)) return;
		const u16 gate = c->gate_mask ? c->gate_mask : 0xB6DB;
		for(int bi = 0; bi < stop; ++bi) {
			if(!counter_new_chord(c, p, bi)) continue;
			const int root = chord_at(c, p, bi);
			int a = pitch(c, c->key, root);
			while(a < c->ctr_lo) a += 12;
			while(a > c->ctr_lo + 11) a -= 12;
			int b = pitch(c, c->key, root + 4);
			while(b <= a) b += 12;
			int d = pitch(c, c->key, root + 2) + 12;
			while(d <= b) d += 12;
			const int notes[3] = {a, b, d};
			add_n(c, b0 + (u32)bi, 0, CH_COUNTER, notes, 3, counter_span(c, p, bi, stop) - 1, k == SEC_DROP ? 96 : 78, k == SEC_VERSE ? LAYER_MIN_HIGH : LAYER_ALWAYS, FX_NONE);
			last_event(c)->gate = gate;
		}
		return;
	}
	const bool guide = big(k) || k == SEC_BRIDGE || (k == SEC_VERSE && !calm && p->variant == 1);
	if(guide) {
		const u8 lay = calm || k == SEC_VERSE ? LAYER_MIN_MID : LAYER_ALWAYS;
		const int vel = calm ? 64 : 76;
		for(int bi = 0; bi < stop; ++bi) {
			if(!counter_new_chord(c, p, bi)) continue;
			const u32 bar = b0 + (u32)bi;
			const int root = chord_at(c, p, bi);
			const int rows = counter_span(c, p, bi, stop);
			const int n = counter_choose(c, root, bar, -1);
			if(n < 0) continue;
			int row = 0;
			if(!calm && bi > 0 && c->last_ctr - n >= 1 && c->last_ctr - n <= 2 && rng_chance(&c->r, 0.5f)) {
				add1(c, bar, 0, CH_COUNTER, c->last_ctr, 4, vel + 6, lay); /* the suspension */
				row = 4;
			}
			if(c->counter_pulse && rows >= 16) {
				add1(c, bar, row, CH_COUNTER, n, 8 - row, vel, lay);
				c->last_ctr = n;
				const int m2 = counter_choose(c, root, bar, n);
				if(m2 >= 0) {
					add1(c, bar, 8, CH_COUNTER, m2, rows - 9, vel - 4, lay);
					c->last_ctr = m2;
				}
			} else {
				add1(c, bar, row, CH_COUNTER, n, rows - row - 1, vel, lay);
				c->last_ctr = n;
			}
		}
	}
	if(k == SEC_VERSE && c->counter_mode == 2) {
		/* a 4-note run in 8ths at the end of every 4-bar phrase, falling
		 * stepwise onto the next chord's third */
		for(int bi = 3; bi < stop - 1; bi += 4) {
			const int target = chord_at(c, p, bi + 1) + 2;
			int base = c->key;
			while(pitch(c, base, target + 3) < c->ctr_lo) base += 12;
			while(pitch(c, base, target + 3) > c->ctr_hi) base -= 12;
			for(int i = 0; i < 4; ++i) add1(c, b0 + (u32)bi, 8 + 2 * i, CH_COUNTER, pitch(c, base, target + 4 - i), 2, 66 + 4 * i, LAYER_ALWAYS);
			c->last_ctr = pitch(c, base, target + 1);
		}
	}
}

/* --- section rendering ----------------------------------------------------------- */
static void render_section(Composer *c, int index)
{
	Song *s = c->s;
	const PlanItem *p = &c->plan[index];
	const int k = p->sec.kind;
	const int next = p->orig_next != 255 ? p->orig_next : index + 1 < c->nplan ? (int)c->plan[index + 1].sec.kind : SEC_OUTRO;
	const bool last_section = index + 1 == c->nplan;
	const int bars = (int)p->sec.bars;
	const u32 b0 = p->sec.start_bar;
	const bool into_big = big(next);
	const u8 T = last_section ? (u8)TR_PLAIN : p->out;
	const PlanItem *prev = index > 0 ? &c->plan[index - 1] : nullptr;
	const u32 end_tick = (b0 + (u32)bars) * ticks_per_bar;
	constexpr u32 all_but_lead_fx = (1u << CH_KICK) | (1u << CH_SNARE) | (1u << CH_HATS) | (1u << CH_BASS) | (1u << CH_ARP) | (1u << CH_PAD) | (1u << CH_COUNTER);
	const bool group_b = big(k);
	const GrooveDef *G = &grooves[group_b ? c->groove_b : c->groove_a];
	const BassPat *BP = group_b ? &c->bpat_b : &c->bpat_a;
	c->swing = G->swing >= 0 ? G->swing : c->song_swing;
	const bool calm = s->mood == MOOD_CALM;

	/* the hand-over at the end of this section */
	int gap_rows = 0; /* silence (but the lead and fx tails) before the next downbeat */
	int roll_bars = 0;
	if(!last_section) {
		switch(T) {
		case TR_ROLL:
		case TR_CLAP_ROLL: roll_bars = imax(1, build_bars(c, p)); break;
		case TR_RISER: roll_bars = k == SEC_BREAKDOWN ? 2 : 0; break;
		case TR_FILTER_OPEN:
		case TR_KICK_RETURN: roll_bars = k == SEC_BREAKDOWN ? imin(2, bars / 2) : 0; break;
		default: break;
		}
		if(k == SEC_BREAKDOWN && (T == TR_ROLL || T == TR_CLAP_ROLL || T == TR_RISER)) gap_rows = 4;
		if(T == TR_STOP) gap_rows = bars >= 16 || k == SEC_BREAKDOWN ? 16 : 8;
		if(gap_rows > 0) add_mute(c, end_tick - (u32)(gap_rows * ticks_per_row), end_tick, all_but_lead_fx);
		if(T == TR_DRUM_BREAK) add_mute(c, end_tick - 4 * ticks_per_bar, end_tick, (1u << CH_LEAD) | (1u << CH_ARP) | (1u << CH_PAD) | (1u << CH_COUNTER));
		if(T == TR_KICK_OUT) add_mute(c, end_tick - ticks_per_bar, end_tick, (1u << CH_KICK) | (bars >= 16 ? (1u << CH_BASS) : 0u));
		if(T == TR_PAUSE) add_mute(c, end_tick - ticks_per_bar, end_tick, all_but_lead_fx | (1u << CH_LEAD));
	}
	if(p->build_up) {
		/* The build is drums, bass and arps layering in; the melody waits
		 * for the first main section. (A low lead entering over the bare
		 * gallop bass muddied it. Muting keeps every random draw, so the
		 * rest of the song is unchanged.) */
		add_mute(c, b0 * ticks_per_bar, end_tick, 1u << CH_LEAD);
	}
	const int kick_return_from = T == TR_KICK_RETURN ? bars - imin(4, bars / 2) : bars;

	for(int bi = 0; bi < bars; ++bi) {
		const u32 bar = b0 + (u32)bi;
		const bool last_bar = bi == bars - 1;
		const bool final_bar = last_section && last_bar;
		const bool phrase_end = (bi % 8) == 7;
		const int root = chord_at(c, p, bi);
		const int cid = chord_id(c, p, bi);
		const int next_root = last_bar ? (last_section ? 0 : chord_at(c, &c->plan[index + 1], 0)) : chord_at(c, p, bi + 1);
		const int next_cid = last_bar ? (last_section ? 0 : chord_id(c, &c->plan[index + 1], 0)) : chord_id(c, p, bi + 1);
		const bool chord_change = next_cid != cid;
		const bool fill = last_bar && !last_section && (T == TR_FILL || T == TR_TOMS) && k != SEC_BREAKDOWN;
		const bool rolling = k != SEC_BREAKDOWN && roll_bars > 0 && bi >= bars - roll_bars;

		/* build-up: kick and bass, then hats, then the backbeat, then everything */
		const int stage = p->build_up ? bi * 4 / imax(1, bars) : 3;
		const bool drums = !p->quiet;
		/* kick (the groove's pattern; its variant on a phrase's last bar) */
		bool kick = drums && (k != SEC_BREAKDOWN || bi >= kick_return_from);
		if(k == SEC_INTRO && calm && bi < bars / 2 && !c->beat_intro) kick = false;
		if(k == SEC_OUTRO && bi >= bars - 1) kick = false;
		if(kick) {
			u16 km = k == SEC_BREAKDOWN ? 0x1111 : phrase_end && !last_bar ? G->kick_alt : G->kick;
			if(k != SEC_BREAKDOWN && !c->war && (bi % 2) == 1) km = (u16)(km | c->kick_extra);
			for(int row = 0; row < 16; ++row) {
				if(!(km & (1u << row))) continue;
				if(fill && row >= 12 && into_big) continue; /* leave room for the fill */
				const int vel = k == SEC_BREAKDOWN ? 96 + 6 * (bi - kick_return_from) : row % 4 == 0 ? 120 : 108;
				add1(c, bar, row, CH_KICK, DR_KICK, 2, vel, LAYER_ALWAYS);
			}
		}
		if(final_bar) add1(c, bar, 0, CH_KICK, DR_KICK, 2, 120, LAYER_ALWAYS);

		/* backbeat, ghosts, fills */
		const bool backbeat = drums && stage >= 2 && (k != SEC_INTRO || bi >= bars / 2) && !rolling;
		if(backbeat && k != SEC_BREAKDOWN && !final_bar) {
			for(int row = 0; row < 16; ++row) {
				if(!(G->snare & (1u << row))) continue;
				if(fill && row >= 12) continue;
				add1(c, bar, row, CH_SNARE, c->backbeat, 2, 112, LAYER_ALWAYS);
				if(big(k)) add1(c, bar, row, CH_SNARE, c->backbeat == DR_CLAP ? DR_SNARE : DR_CLAP, 2, 80, LAYER_MIN_HIGH); /* layered at high intensity */
			}
			const u16 gm = (u16)((G->ghost | c->ghost_extra) & ~G->snare);
			for(int row = 0; row < 16; ++row) {
				if((gm & (1u << row)) && !(fill && row >= 8)) add1(c, bar, row, CH_SNARE, DR_SNARE, 1, 42, LAYER_MIN_MID);
			}
			/* a small fill at the end of 4-bar phrases (active moods) */
			if(!calm && !c->war && (bi % 4) == 3 && !last_bar && !phrase_end && rng_chance(&c->r, 0.35f)) {
				for(int row = 13; row <= 15; ++row) add1(c, bar, row, CH_SNARE, DR_SNARE, 1, 58 + 8 * (row - 13), LAYER_MIN_MID);
			}
		}
		if(fill) {
			if(T == TR_FILL) {
				for(int row = 8; row < 16; ++row) {
					if(row < 12 && (row & 1)) continue;
					add1(c, bar, row, CH_SNARE, DR_SNARE, 1, 60 + (row - 8) * 8, LAYER_ALWAYS);
				}
			} else {
				static const int toms[4] = {DR_TOM_HIGH, DR_TOM_HIGH, DR_TOM_MID, DR_TOM_LOW};
				for(int i = 0; i < 4; ++i) add1(c, bar, 12 + i, CH_FX, toms[i], 2, 85 + i * 6, LAYER_ALWAYS);
			}
		}
		if(T == TR_KICK_OUT && last_bar && !last_section) {
			for(int row = 12; row < 16; ++row) add1(c, bar, row, CH_SNARE, DR_SNARE, 1, 70 + (row - 12) * 10, LAYER_ALWAYS);
		}
		if(roll_bars > 0 && bi == bars - roll_bars) snare_roll(c, bar, roll_bars, gap_rows, T == TR_CLAP_ROLL ? DR_CLAP : DR_SNARE);

		/* hats */
		const bool hats = drums && stage >= 1 && k != SEC_BREAKDOWN && !(k == SEC_OUTRO && bi >= bars - 2) && !final_bar;
		if(hats) {
			const bool open = big(k) || k == SEC_BRIDGE || (k == SEC_VERSE && p->variant == 1);
			for(int row = 0; row < 16; ++row) {
				const u16 bitm = (u16)(1u << row);
				if(fill && row >= 12) continue;
				if(G->hat_open & bitm) {
					if(open || k == SEC_VERSE) add1(c, bar, row, CH_HATS, DR_HAT_OPEN, 2, 92, open ? LAYER_ALWAYS : LAYER_MIN_MID);
					if(!open) add1(c, bar, row, CH_HATS, DR_HAT_CLOSED, 1, 85, k == SEC_VERSE ? LAYER_MAX_MID : LAYER_ALWAYS);
				} else if(G->hat_closed & bitm) {
					const int vel = G->hat_closed == 0xFFFF || G->hat_closed == 0xBBBB ? (row % 4 == 0 ? 84 : row % 2 == 0 ? 70 : 56) : (row % 4 == 0 ? 70 : 82);
					add1(c, bar, row, CH_HATS, DR_HAT_CLOSED, 1, vel, LAYER_ALWAYS);
				} else if(G->hat_extra & bitm) {
					add1(c, bar, row, CH_HATS, DR_HAT_CLOSED, 1, 52, k == SEC_INTRO ? LAYER_MIN_HIGH : LAYER_MIN_MID);
				}
			}
			if(big(k)) {
				for(int row = 0; row < 16; ++row) add1(c, bar, row, CH_FX, row % 4 == 2 ? DR_RIM : DR_SHAKER, 1, row % 4 == 2 ? 55 : 40, LAYER_MIN_HIGH);
			}
			if(c->style == ST_PUNK && big(k) && bi % 2 == 0) add1(c, bar, 0, CH_FX, DR_CRASH, 16, 84, LAYER_ALWAYS); /* crash-ride punk */
			if(k == SEC_GROOVE) {
				/* percussion: shaker 16ths, rim on the off-8ths, a tom figure every other bar */
				for(int row = 0; row < 16; ++row) add1(c, bar, row, CH_FX, row % 4 == 2 ? DR_RIM : DR_SHAKER, 1, row % 4 == 2 ? 70 : row % 2 ? 44 : 56, LAYER_ALWAYS);
				static const int tom_rows[2][4] = {{3, 6, 10, 13}, {6, 10, 12, 14}};
				static const u8 toms[4] = {DR_TOM_HIGH, DR_TOM_MID, DR_TOM_MID, DR_TOM_LOW};
				for(int i = 0; i < 4; ++i) add1(c, bar, tom_rows[bi % 2][i], CH_FX, toms[i], 2, 72 + 6 * i, LAYER_ALWAYS);
			}
		}
		if(k == SEC_BREAKDOWN && (bi < bars - 2 || bi >= kick_return_from) && !calm) {
			for(int row = 2; row < 16; row += 4) add1(c, bar, row, CH_HATS, DR_HAT_CLOSED, 1, 45, LAYER_MIN_MID);
		}

		/* bass (the hardtrance drop: off-beat roots, octave on top at high intensity) */
		const bool offbeat_drop = k == SEC_DROP && c->hard_drop && !(c->war && c->bass_b != B_OFFBEAT && p->variant == 0);
		static const BassPat ob = {.mask = 0x4444};
		const BassPat *B = offbeat_drop ? &ob : BP;
		bool bass = drums && (k != SEC_INTRO || (bars >= 16 && bi >= bars - 4) || c->beat_intro);
		if(k == SEC_BREAKDOWN || (k == SEC_OUTRO && bi >= bars - 3)) bass = false;
		/* a walking run into the next chord at the end of an 8-bar phrase (active moods) */
		const bool bass_run = bass && !calm && !c->war && phrase_end && !last_bar && !offbeat_drop && c->style != ST_EURODANCE;
		if(bass) {
			for(int row = 0; row < 16; ++row) {
				if(!(B->mask & (1u << row))) continue;
				if(fill && row >= 12 && into_big) continue;
				if(bass_run && row >= 12) continue;
				int next_hit = row + 1;
				while(next_hit < 16 && !(B->mask & (1u << next_hit))) ++next_hit;
				const int len = B->legato ? imax(1, next_hit - row) : imax(1, imin(next_hit - row, 4) * 3 / 4);
				int role = B->role[row];
				/* the second bar of a chord moves: the fifth or octave on beat 3 */
				if(!calm && !B->legato && role == BR_ROOT && (bi % 2) == 1 && row == 8 && B->mask != 0x4444) role = (bi / 2) % 2 ? BR_FIFTH : BR_OCTAVE;
				int deg = root;
				int oct = 0;
				switch(role) {
				case BR_OCTAVE: oct = 12; break;
				case BR_FIFTH: deg = root + 4; break;
				case BR_BELOW: deg = root - 1; break;
				case BR_THIRD: deg = root + 2; break;
				case BR_APPROACH: deg = chord_change ? next_root + (next_root > root ? -1 : 1) : root + 4; break;
				default: break;
				}
				if(chord_change && next_hit >= 16 && row >= 10 && !offbeat_drop && role != BR_APPROACH) deg = next_root + (next_root > root ? -1 : 1); /* approach tone */
				const int n = pitch(c, c->key - 12, deg) + oct;
				/* at high intensity root notes gain an octave (an alternative event, not an extra one) */
				const bool root_hit = role == BR_ROOT && c->bass_octave_layer;
				add1(c, bar, row, CH_BASS, n, len, row % 4 == 0 ? 112 : 104, root_hit ? LAYER_MAX_HIGH : LAYER_ALWAYS);
				if(root_hit) {
					const int two[2] = {n, n + 12};
					add_n(c, bar, row, CH_BASS, two, 2, len, 104, LAYER_MIN_HIGH, FX_NONE);
				}
			}
			if(bass_run) {
				const int target = next_root + (next_root <= root ? 7 : 0);
				for(int i = 0; i < 4; ++i) add1(c, bar, 12 + i, CH_BASS, pitch(c, c->key - 12, target - 4 + i), 1, 96 + 4 * i, LAYER_ALWAYS);
			}
		}
		if(k == SEC_BREAKDOWN && bi < bars - 1 && (bi % p->bars_per_chord) == 0) {
			/* a soft sub note under the breakdown pads */
			add1(c, bar, 0, CH_BASS, pitch(c, c->key - 12, root), p->bars_per_chord * 16 - 2, 70, LAYER_MIN_MID);
		}
		if(k == SEC_OUTRO && bi == bars - 2) add1(c, bar, 0, CH_BASS, pitch(c, c->key - 12, root), 14, 96, LAYER_ALWAYS); /* the cadence chord's root */
		if(final_bar) add1(c, bar, 0, CH_BASS, pitch(c, c->key - 12, 0), 16, 110, LAYER_ALWAYS);

		/* pads (a new chord at each chord change; gated in the breakdown when the song has a gate) */
		const bool pad_sustain = (k == SEC_INTRO && !(c->beat_intro && bi < bars / 2 && !p->quiet)) || k == SEC_BREAKDOWN || k == SEC_BRIDGE || k == SEC_OUTRO ||
		                         (k == SEC_VERSE && stage >= 1) || (big(k) && c->stab_mask == 0);
		const bool new_chord = bi % p->bars_per_chord == 0 || chord_id(c, p, bi - 1) != cid;
		int chord_rows = 0;
		for(int b = bi; b < bars && (b == bi || (b % p->bars_per_chord != 0 && chord_id(c, p, b) == cid)); ++b) chord_rows += 16;
		/* suspensions: a sus4 resolving to the triad on phrase starts and cadences
		 * (never on the song's very first chord: an opening suspension sounds like a wrong chord;
		 * tested after the draw so the random sequence stays the same) */
		const bool sus_drawn = new_chord && chord_rows >= 16 && !c->power && color_at(c, p, bi) == C_HOME && (bi % 8 == 0 || root == c->cad_deg) && rng_chance(&c->r, calm ? 0.15f : 0.4f);
		const bool sus = sus_drawn && !(index == 0 && bi == 0);
		if(sus_drawn && !sus && !final_bar && new_chord && pad_sustain) (void)rng_range(&c->r, -5, 5); /* the dropped sus chord's velocity draw */
		if(final_bar) {
			emit_chord(c, bar, 0, 0, 16, 92, LAYER_ALWAYS, 0, false, 0, false);
		} else if(new_chord && pad_sustain) {
			const int vel = k == SEC_VERSE ? 62 : k == SEC_INTRO ? 70 : 85;
			const u8 lay = k == SEC_VERSE && c->stab_mask != 0 ? LAYER_MAX_HIGH : LAYER_ALWAYS;
			const u16 gate = k == SEC_BREAKDOWN ? c->gate_mask : 0;
			emit_chord(c, bar, 0, root, chord_rows - 1, vel, lay, 0, false, gate, sus);
		}
		if(!final_bar && c->stab_mask != 0 && (big(k) || (k == SEC_VERSE && p->variant == 1))) {
			for(int row = 0; row < 16; ++row) {
				if(!(c->stab_mask & (1u << row))) continue;
				if(fill && row >= 12) continue;
				emit_chord(c, bar, row, root, 1, 92, k == SEC_VERSE ? LAYER_MIN_HIGH : LAYER_ALWAYS, 0, true, 0, false);
			}
			if(k != SEC_VERSE && new_chord) emit_chord(c, bar, 0, root, chord_rows - 1, 50, LAYER_MAX_MID, 0, false, offbeat_drop ? c->gate_mask : 0, false);
		}

		/* fx: crashes, impacts, risers, sweeps */
		const bool entry = bi == 0 && (big(k) || k == SEC_GROOVE || (k == SEC_VERSE && index > 1) || k == SEC_OUTRO || (prev && prev->downlift));
		if(entry) add1(c, bar, 0, CH_FX, DR_CRASH, 16, 100, LAYER_ALWAYS);
		if(bi == 0 && prev && prev->impact) add1(c, bar, 0, CH_FX, DR_IMPACT, 16, 110, LAYER_ALWAYS);
		if(bi == 0 && prev && prev->downlift) add1(c, bar, 0, CH_FX, DR_DOWNLIFTER, 32, 96, LAYER_ALWAYS);
		if(bi == 0 && T == TR_FILTER_OPEN && !last_section) add1(c, bar, 0, CH_FX, DR_LP_OPEN, bars * 16 - 1, 100, LAYER_ALWAYS);
		if(!last_section && last_bar) {
			if(T == TR_SHORT_RISER) add1(c, bar, 0, CH_FX, DR_RISER, 16, 92, LAYER_ALWAYS);
			if(T == TR_PAUSE) add1(c, bar, 8, CH_FX, DR_SWELL, 8, 72, LAYER_ALWAYS); /* only a faint swell into the downbeat */
			if(p->reverse) add1(c, bar, 16 - imax(8, gap_rows), CH_FX, DR_SWELL, imax(8, gap_rows), 96, LAYER_ALWAYS);
		}
		if(T == TR_RISER && !last_section) {
			const int len_bars = k == SEC_BREAKDOWN ? imin(4, bars) : 2;
			if(bi == bars - len_bars) add1(c, bar, 0, CH_FX, DR_RISER, len_bars * 16, k == SEC_BREAKDOWN ? 100 : 88, LAYER_ALWAYS);
		}
		if(T == TR_FILTER_CLOSE && !last_section) {
			const int len_bars = bars >= 8 ? 2 : 1;
			if(bi == bars - len_bars) add1(c, bar, 0, CH_FX, DR_LP_CLOSE, len_bars * 16, 100, LAYER_ALWAYS);
		}
		if(final_bar) add1(c, bar, 0, CH_FX, DR_CRASH, 16, 105, LAYER_ALWAYS);
	}

	render_melody(c, index);
	render_counter(c, index);
	render_build(c, index);
}

/* --- description ------------------------------------------------------------------ */
typedef struct {
	char *buf;
	size_t cap, len;
} Str;

static void str_add(Str *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void str_add(Str *s, const char *fmt, ...)
{
	if(s->len + 1 >= s->cap) return;
	va_list ap;
	va_start(ap, fmt);
	const int n = vsnprintf(s->buf + s->len, s->cap - s->len, fmt, ap);
	va_end(ap);
	if(n > 0) s->len = s->len + (size_t)n < s->cap ? s->len + (size_t)n : s->cap - 1;
}

static void prog_str(char *out, size_t cap, const Progression *pr)
{
	Str s = {out, cap, 0};
	out[0] = 0;
	for(int i = 0; i < 4; ++i) {
		str_add(&s, "%s%s", i ? "-" : "", color_names[pr->col[i]]);
		if(pr->col[i] != C_MED3 && pr->col[i] != C_MED6) str_add(&s, "%d", pr->deg[i] + 1);
	}
}

static void describe(Composer *c)
{
	Song *s = c->s;
	char buf[512], pa[64], pb[64], pcc[64], mod[96] = "";
	const int pc = ((c->key % 12) + 12) % 12;
	prog_str(pa, sizeof pa, c->prog_a);
	prog_str(pb, sizeof pb, c->prog_b);
	prog_str(pcc, sizeof pcc, c->prog_c);
	static const char *const reg[] = {"low", "mid", "high"};
	if(c->mod_shift) snprintf(mod, sizeof mod, ", modulates +%d at bar %u", c->mod_shift, c->mod_bar);
	snprintf(buf, sizeof buf, "seed %llu, %s, %s, %.0f BPM, %s %s, prog %s / %s / %s, groove %s/%s, bass %s/%s, %s lead, motifs %s/%s%s%s%s, %u bars (%.0f s): ",
	         (unsigned long long)s->seed, mood_name(s->mood), s->style, (double)s->bpm, note_names[pc], c->mode->name, pa, pb, pcc, grooves[c->groove_a].name,
	         grooves[c->groove_b].name, bass_names[c->bass_a], bass_names[c->bass_b], reg[c->lead_class], mstyle_names[c->style_a], mstyle_names[c->style_b],
	         c->song_swing ? ", swing" : "", c->chip_chords ? ", chip chords" : c->euro_lead ? ", euro lead" : "", mod, s->total_bars, song_duration(s));
	Str d = {s->description, sizeof s->description, 0};
	s->description[0] = 0;
	str_add(&d, "%s", buf);
	for(u32 i = 0; i < s->nsections; ++i) str_add(&d, "%s%s%u", i ? " " : "", section_name(s->sections[i].kind), s->sections[i].bars);
	str_add(&d, "; transitions: %s", c->ntransitions ? c->transitions : "none");
}

/* --- the pipeline ------------------------------------------------------------------ */
static bool run(Song *out, u64 seed, Mood m, bool force_solo)
{
	song_init(out);
	out->ntracks = CH_COUNT;
	for(int i = 0; i < CH_COUNT; ++i) snprintf(out->track_name[i], sizeof out->track_name[i], "%s", channel_name(i));
	Composer *c = malloc(sizeof *c);
	if(!c) return false;
	composer_init(c, out, seed, m, force_solo);
	choose_style(c);
	choose_basics(c);
	choose_instruments(c);
	choose_groove(c);
	choose_arp_variation(c);
	choose_form(c);
	choose_arrangement(c);
	insert_arp_solo(c);
	insert_choir(c);
	choose_euro_lead(c);
	plan_bars(c);
	for(int i = 0; i < c->nplan; ++i) {
		if(c->plan[i].sec.kind == SEC_ARP_SOLO) render_arp_solo(c, i);
		else if(c->plan[i].sec.kind == SEC_CHOIR) render_choir(c, i);
		else render_section(c, i);
	}
	apply_mutes(c);
	for(u32 i = 0; i < c->npost; ++i) push_event(c, c->post[i]);
	song_sort(out);
	describe(c);
	const bool ok = !c->oom;
	free(c->bars);
	free(c->post);
	free(c);
	if(!ok) song_free(out);
	return ok;
}

bool compose(Song *out, u64 seed, Mood m) { return run(out, seed, m, false); }

bool compose_menu(Song *out, u64 seed)
{
	/* A wartime song with an arp solo; songs whose form leaves no room for one
	 * (intermissions, a key change at the spot) pass to the next seed. */
	for(int k = 0; k < 32; ++k) {
		if(!run(out, seed, MOOD_WARTIME, true)) return false;
		for(u32 i = 0; i < out->nsections; ++i) {
			if(out->sections[i].kind == SEC_ARP_SOLO) return true;
		}
		song_free(out);
		seed = next_seed(seed);
	}
	return run(out, seed, MOOD_WARTIME, true);
}
