/* The score: what a project (and a composed song) is made of.
 *
 * Ported from geosim's procedural music (src/client/audio/song.hpp, C++20)
 * to C23. A song is a tracker-style score: up to FT_MAX_TRACKS tracks, a
 * fixed BPM, 16th-note rows (4 rows per beat, 16 rows per 4/4 bar)
 * subdivided into 6 tracker ticks per row. Each track has one instrument
 * (a Patch: a synth, a drum kit or a modelled guitar) and its mix settings.
 * The composer writes the composed tracks 0..CH_COUNT-1 in geosim's channel
 * order; a project may add more tracks (guitars, a second synth, ...).
 *
 * The player (synth.h) only reads a song while it plays. */
#ifndef FT_SONG_H
#define FT_SONG_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

/* frustrated: things keep going wrong short of a catastrophe (hardstyle). */
typedef enum : u8 { MOOD_CALM, MOOD_TENSE, MOOD_CRISIS, MOOD_TRIUMPH, MOOD_WARTIME, MOOD_FRUSTRATED, MOOD_COUNT } Mood;
const char *mood_name(Mood m);
bool parse_mood(const char *text, Mood *out);

/* The composer's channels (track indices of a composed song).
 * CH_COUNTER: a second melodic voice; CH_CHOIR: the formant choir. */
enum { CH_KICK, CH_SNARE, CH_HATS, CH_BASS, CH_LEAD, CH_ARP, CH_PAD, CH_FX, CH_COUNTER, CH_CHOIR, CH_COUNT };
const char *channel_name(int ch);

#define FT_MAX_TRACKS 16

constexpr int rows_per_beat = 4; /* 16th notes */
constexpr int rows_per_bar = 16; /* 4/4 */
constexpr int ticks_per_row = 6; /* tracker "speed 6" */
constexpr int ticks_per_bar = rows_per_bar * ticks_per_row;

/* Drum-kit note numbers (General MIDI-like). */
enum {
	DR_KICK = 36,
	DR_RIM = 37,
	DR_SNARE = 38,
	DR_CLAP = 39,
	DR_HAT_CLOSED = 42,
	DR_TOM_LOW = 45,
	DR_HAT_OPEN = 46,
	DR_TOM_MID = 47,
	DR_CRASH = 49,
	DR_TOM_HIGH = 50,
	DR_SHAKER = 70,
	DR_RISER = 90,      /* noise sweep up over the event length */
	DR_IMPACT = 91,     /* low boom + noise burst (start of a drop) */
	DR_SWELL = 92,      /* reversed-cymbal-like swell */
	DR_LP_CLOSE = 93,   /* not a sound: the bus low-pass closes over the event, then reopens */
	DR_LP_OPEN = 94,    /* not a sound: the bus low-pass opens over the event */
	DR_DOWNLIFTER = 95, /* noise sweep down */
};

typedef enum : u8 { WAVE_PULSE, WAVE_SAW, WAVE_TRIANGLE, WAVE_SINE, WAVE_NOISE, WAVE_TABLE } Wave;
constexpr int wavetable_count = 4;
constexpr int wavetable_len = 32;

/* What a track's patch plays: geosim's subtractive synth, its drum
 * models, or a modelled plucked string instrument (guitar.h). */
typedef enum : u8 { INST_SYNTH, INST_KIT, INST_GUITAR } InstKind;

/* Guitar bodies and amps (guitar.h). */
typedef enum : u8 {
	GTR_STEEL,    /* steel-string acoustic */
	GTR_NYLON,    /* classical */
	GTR_CLEAN,    /* electric, clean amp */
	GTR_CRUNCH,   /* electric, crunch */
	GTR_LEAD,     /* electric, high gain with sustain */
	GTR_BASS,     /* 4-string electric bass */
	GTR_12STRING, /* 12-string acoustic (octave courses) */
	GTR_COUNT
} GuitarModel;

/* One instrument. Tonal synth patches are two oscillators -> SVF low-pass
 * with its own envelope -> amp ADSR. Kits synthesise drums from the note.
 * Guitars: see the gtr_* fields and guitar.h. */
typedef struct {
	InstKind inst;
	bool mono;     /* a new note cuts the previous one (tracker channel) */
	Wave w1, w2;
	u8 table1, table2;
	float mix2;    /* osc 2 level (osc 1 is 1) */
	float detune;  /* osc 2 detune, cents */
	float semi2;   /* osc 2 transpose, semitones */
	float duty, pwm_depth, pwm_rate;
	float sub;     /* sine one octave below */
	int unison;    /* extra detuned saws (0-4): supersaw */
	float spread;  /* unison detune, cents */
	bool poles4;   /* 24 dB/oct low-pass */
	u8 formant;    /* 0 off, 1 "aah", 2 "ooh" */
	float attack, decay, sustain, release;
	float cutoff, resonance, fenv, fdecay, key_track;
	float glide;
	float vib_depth, vib_rate, vib_delay;
	float crush_rate;
	float drive;
	float gain;
	/* kit */
	float kick_pitch, kick_end, kick_decay, kick_click, kick_sweep, kick_drive;
	float snare_tone, snare_decay, hat_decay, hat_tone;
	/* guitar */
	GuitarModel gtr;
	float gtr_tone;     /* 0 dark .. 1 bright: loop filter and pick hardness */
	float gtr_pick;     /* pick position along the string, 0.05 (bridge) .. 0.5 */
	float gtr_sustain;  /* string decay time multiplier, 0.25 .. 4 (feedback above 3.5) */
	float gtr_gain;     /* amp pre-gain (drive), 0 .. 1 */
	float gtr_body;     /* body / cabinet resonance mix, 0 .. 1 */
	i8 gtr_tuning[6];   /* MIDI notes of the open strings, low to high (0 = standard) */
	u8 gtr_capo;
} Patch;

/* Defaults the C++ member initialisers gave. */
Patch patch_default(void);

/* Layer flags: an event plays only while the live intensity is above / below
 * a threshold, so the arrangement thickens without recomposing. */
enum { LAYER_ALWAYS, LAYER_MIN_MID, LAYER_MIN_HIGH, LAYER_MAX_MID, LAYER_MAX_HIGH };
constexpr float layer_mid = 0.4f, layer_high = 0.7f;

/* arp: cycle notes[] every tick; slide_up: notes[0] bends to notes[1];
 * stab: pluck envelope; swell: the attack lasts the event.
 * strum (guitars): notes[s] is the note on string s (0 = not played),
 * struck one string after another, `strum` ms apart. */
typedef enum : u8 { FX_NONE, FX_ARP, FX_SLIDE_UP, FX_STAB, FX_SWELL, FX_STRUM } NoteFx;

/* Guitar articulations (FX_STRUM chords and single notes). */
typedef enum : u8 {
	ART_NORMAL,
	ART_PALM,     /* palm mute: damped, short, darker */
	ART_RING,     /* let ring: the strings sustain past the event until re-struck */
	ART_STACCATO, /* damped right at note-off */
	ART_HARMONIC, /* natural harmonic (12th-fret node) */
	ART_DEAD,     /* muted scratch, no pitch */
	ART_SLIDE,    /* slide from notes[0] to notes[1] over the event */
	ART_BEND,     /* bend up to notes[1] */
	ART_HAMMER,   /* legato: no pick attack */
	ART_VIBRATO,  /* finger vibrato */
	ART_FEEDBACK, /* endless sustain blooming into feedback */
	ART_COUNT
} Articulation;
const char *art_name(int a);

typedef struct {
	u32 tick;      /* absolute tracker tick (row * 6 + offset) */
	u16 len;       /* ticks until note-off */
	u8 ch;         /* track */
	u8 vel;        /* 0..127 */
	u8 layer;
	NoteFx fx;
	u8 count;      /* notes used (FX_STRUM: 6) */
	u8 art;        /* Articulation (guitars) */
	u8 notes[6];   /* MIDI notes (or drum ids on kits) */
	u16 gate;      /* trance gate bits per 16th (0 = none) */
	i8 strum;      /* FX_STRUM: ms between strings, > 0 down (low to high), < 0 up */
	u8 string;     /* single guitar notes: 1..6 = this string, 0 = automatic */
} NoteEvent;

typedef enum : u8 { SEC_INTRO, SEC_VERSE, SEC_CHORUS, SEC_BRIDGE, SEC_BREAKDOWN, SEC_DROP, SEC_OUTRO, SEC_GROOVE, SEC_ARP_SOLO, SEC_CHOIR, SEC_COUNT } SectionKind;
const char *section_name(SectionKind k);

typedef struct {
	SectionKind kind;
	u32 start_bar, bars;
} Section;

typedef struct {
	float gain[FT_MAX_TRACKS];
	float pan[FT_MAX_TRACKS];    /* -1..1 */
	float delay[FT_MAX_TRACKS];  /* send levels */
	float reverb[FT_MAX_TRACKS];
	float duck[FT_MAX_TRACKS];   /* sidechain depth from the kick */
	bool bright[FT_MAX_TRACKS];  /* cutoff follows the live intensity */
	float chorus[FT_MAX_TRACKS];
	float highpass[FT_MAX_TRACKS]; /* Hz, 0 = off */
	float delay_rows;
	float delay_feedback;
	float reverb_size;
} MixSettings;

#define FT_MAX_SECTIONS 64

typedef struct {
	u64 seed;
	Mood mood;
	float bpm;
	int key;       /* tonic MIDI note */
	int final_key; /* tonic after a key change for the last chorus */
	int mode;      /* the composer's mode table index */
	char style[24];
	char description[4096]; /* the composer's: up to ~3 KB for a 10-minute song */
	int ntracks;   /* tracks in use (a composed song: CH_COUNT) */
	u8 role[FT_MAX_TRACKS]; /* a composed track's channel CH_* + 1 (0: none); the player treats roles as geosim did */
	char track_name[FT_MAX_TRACKS][24];
	Patch patches[FT_MAX_TRACKS];
	MixSettings mix;
	u32 nsections;
	Section sections[FT_MAX_SECTIONS];
	NoteEvent *events; /* sorted by tick (stable) */
	u32 nevents, cap_events;
	u32 total_bars;
} Song;

void song_init(Song *s); /* zeroed, mix defaults, no events */
void song_free(Song *s);
void song_copy(Song *dst, const Song *src); /* deep */
NoteEvent *song_add(Song *s, NoteEvent e);
/* stable sort by tick (insertion order kept for equal ticks) */
void song_sort(Song *s);
static inline u32 song_total_ticks(const Song *s) { return s->total_bars * (u32)ticks_per_bar; }
static inline double song_seconds_per_tick(const Song *s) { return 60.0 / ((double)s->bpm * rows_per_beat * ticks_per_row); }
static inline double song_duration(const Song *s) { return song_total_ticks(s) * song_seconds_per_tick(s); }
static inline NoteEvent note_event(void) { return (NoteEvent){.len = 6, .vel = 100, .count = 1, .notes = {60}}; }

#endif
