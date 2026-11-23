/* The autogenerator (docs/autogen.md): a whole song from a seed.
 *
 * geosim's composer writes form, harmony, motifs, grooves and synth sounds
 * for a mood. The autogenerator then reads the harmony back out of the
 * score (the chord under every half bar, from the pads, counter voice and
 * bass), and arranges it for a band:
 *  - rhythm guitars: a voicing per chord from the chord library, chosen
 *    for a small hand movement, strummed or picked with a pattern per
 *    section (picking in intros, chugs in verses, open strums in choruses,
 *    ringing whole notes in breakdowns), double-tracked left / right in a
 *    rock band with different voicings;
 *  - a lead guitar playing the composer's lead line, with vibrato on long
 *    notes, hammer-ons on quick steps, slides into leaps and bends at
 *    phrase ends;
 *  - a bass guitar playing the composer's bass line in its range.
 * The band decides which of the composer's synth tracks stay. */
#ifndef FT_AUTOGEN_H
#define FT_AUTOGEN_H

#include "song.h"

typedef enum : u8 {
	BAND_SYNTH_GUITAR, /* the composer's synths + a rhythm guitar */
	BAND_ROCK,         /* drums, bass guitar, two crunch guitars, lead guitar, a quiet pad */
	BAND_ACOUSTIC,     /* steel strums, nylon picking, bass guitar, light drums */
	BAND_METAL,        /* drums, bass, two high-gain chugging guitars, lead */
	BAND_ELECTRONIC,   /* the composer alone */
	BAND_COUNT
} Band;
const char *band_name(Band b);
bool parse_band(const char *s, Band *b);

typedef struct {
	u64 seed;
	Mood mood;
	Band band;
	float bpm;      /* 0 = the composer's */
	int key;        /* -1 = the composer's; else a tonic pitch class 0..11 (transposes) */
	bool variations; /* more progressions, arps and patterns (compose_ex); off: geosim's song as is */
} AutogenOptions;

/* A chord segment read from the score. */
typedef struct {
	u32 tick, len;
	u8 root, quality;
	u8 bass; /* bass pitch class */
} ChordSeg;

/* Read the harmony of a score: one chord per half bar, merged when equal.
 * Returns the count written (at most max). */
int harmony_extract(const Song *s, ChordSeg *out, int max);

bool autogen(Song *out, const AutogenOptions *opt);

#endif
