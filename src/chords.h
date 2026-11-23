/* Chords for fretted instruments (docs/guitar.md#chords).
 *
 * The library is pre-generated for a tuning: every root x quality is voiced
 * by a search over the fretboard (4-fret windows from the nut to the 12th
 * fret, open strings allowed), keeping the voicings a hand can play (at
 * most 4 fingers, a barre counting as one, a span of 4 frets, the root in
 * the bass, every essential chord tone present, at most one muted string
 * inside) and ranking them by playability and fullness. The common open
 * and barre shapes come out on top on their own.
 *
 * Strum patterns turn a chord and a length into strums and picked notes:
 * downs, ups, muted scratches, accents, palm mutes, and finger-picking
 * figures that pick strings by role (bass, thumb, the top strings). */
#ifndef FT_CHORDS_H
#define FT_CHORDS_H

#include "song.h"

typedef enum : u8 {
	Q_MAJ,
	Q_MIN,
	Q_POWER, /* 5 */
	Q_7,
	Q_MAJ7,
	Q_MIN7,
	Q_SUS2,
	Q_SUS4,
	Q_DIM,
	Q_AUG,
	Q_ADD9,
	Q_6,
	Q_MIN6,
	Q_9,
	Q_MIN9,
	Q_7SUS4,
	Q_DIM7,
	Q_MIN7B5,
	Q_MAJ9,
	Q_MINADD9,
	Q_COUNT
} Quality;

typedef struct {
	const char *name;   /* "maj", "m7", ... */
	const char *suffix; /* as written after the root: "", "m7", ... */
	u8 n;
	u8 iv[5];           /* semitones above the root */
	u8 optional;        /* bit i: iv[i] may be left out (the fifth, mostly) */
} QualityDef;
extern const QualityDef qualities[Q_COUNT];

typedef struct {
	i8 fret[6];  /* per string, low to high; -1 muted */
	u8 root, quality;
	u8 lo, hi;   /* lowest fretted fret (0 when all open) and highest */
	u8 sounded;
	u8 barre;    /* the barre's fret (0 none) */
	float score; /* lower is easier / better */
} Voicing;

#define CHORD_MAX_VOICINGS 16

typedef struct {
	int open[6];
	int strings;
	u16 count[12][Q_COUNT];
	Voicing v[12][Q_COUNT][CHORD_MAX_VOICINGS];
} ChordLib;

extern const char *const note_names[12];

/* Build the library for open strings (MIDI notes, low to high). */
void chordlib_build(ChordLib *lib, const int open[6], int strings);
static inline int chordlib_count(const ChordLib *lib, int root, int q) { return lib->count[root % 12][q]; }
static inline const Voicing *chordlib_get(const ChordLib *lib, int root, int q, int i)
{
	return &lib->v[root % 12][q][i];
}
/* MIDI notes per string (0 = muted) for a voicing. */
void voicing_notes(const ChordLib *lib, const Voicing *v, u8 notes[6]);
/* "x32010" (frets, 'x' muted; frets above 9 in parentheses) */
void voicing_shape(const ChordLib *lib, const Voicing *v, char *buf, size_t n);
void chord_name(int root, int q, char *buf, size_t n);
/* parse "C", "F#m7", "Bbmaj7", "E5" -> root, quality; false if not a chord */
bool chord_parse(const char *s, int *root, int *q);
/* the chord a set of pitch classes forms (bit i = pitch class i), best match */
bool chord_identify(u16 pcs, int bass_pc, int *root, int *q);

/* ---- strum patterns --------------------------------------------------------- */

/* A pattern is one bar or less of 16ths, one character a step:
 *   .  rest (what sounds rings on)    x  muted scratch (dead strings)
 *   D  down strum, accented           d  down strum
 *   U  up strum (top strings), accent u  up strum
 *   H  half strum down (the lower strings: chugs), accented, h unaccented
 *   B  the bass (root, lowest chord string)   A  the alternate bass (next string up)
 *   0-5  pick the n-th chord string from the bottom   P  pinch: bass + top string */
typedef struct {
	const char *name;
	const char *style; /* "folk", "pop", "rock", "punk", "funk", "ballad", "reggae", "metal", "fingerstyle" */
	const char *code;  /* steps, 16ths */
	u8 art;            /* articulation the pattern implies (palm for chugs, ring for picking) */
	i8 speed;          /* ms between strings of a full strum */
} StrumPattern;

extern const StrumPattern strum_patterns[];
extern const int strum_pattern_count;
int strum_find(const char *name);

/* Write a chord voiced as `notes` (per string, 0 muted) on track ch from
 * tick t0 for `ticks` ticks with pattern p (repeated as needed). vel is
 * the base velocity; art overrides the pattern's articulation when not
 * ART_NORMAL; ring lets the last strum ring to the end. Returns the
 * number of events added. */
int strum_write(Song *s, int ch, u32 t0, u32 ticks, const u8 notes[6], int strings, const StrumPattern *p, u8 vel,
                u8 art, bool ring);

#endif
