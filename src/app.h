/* The application's state outside the widgets: the open project, its
 * history, the audio device and transport, the editing cursor, the chord
 * palette, background jobs (generate, export). Views read it and change it
 * through the app_* functions, which lock the audio stream around edits. */
#ifndef FT_APP_H
#define FT_APP_H

#include "base.h"
#include "audio.h"
#include "autogen.h"
#include "chords.h"
#include "export.h"
#include "project.h"

typedef struct {
	/* the project */
	Song song;
	History hist;
	char path[1024];      /* "" = untitled */
	char name[128];
	bool modified;

	Audio *audio;
	bool playing;

	/* the editor */
	int track;            /* selected track */
	int sel;              /* selected event index, -1 none */
	u32 cursor;           /* edit cursor, ticks */
	double view_tick;     /* the arrangement's left edge, ticks */
	float px_per_tick;    /* zoom */
	int lane_scroll;      /* first visible track */
	bool follow;          /* the view follows the playhead */
	int piano_lo;         /* the piano roll's lowest note */

	/* the chord palette */
	ChordLib *lib;        /* for the selected track's tuning */
	int lib_track;        /* the track (and tuning) it was built for */
	int lib_key;          /* tuning + capo signature it was built for */
	int ch_root, ch_q, ch_voicing;
	int pattern;          /* strum pattern index */
	int art;              /* articulation for inserted notes */
	int ins_beats;        /* length of an inserted chord, beats */
	bool strum_up;

	/* generate */
	AutogenOptions gen;
	bool gen_bpm_on, gen_key_on;
	int gen_bpm, gen_key;

	/* export (a worker thread) */
	ExportOptions exp;
	char exp_path[1024];
	volatile float exp_progress;
	volatile bool exp_cancel;
	volatile int exp_state;   /* 0 idle, 1 running, 2 done, 3 failed */
	ExportReport exp_report;
	char exp_err[256];

	float master;         /* master volume */
	u64 edit_ns;          /* the last edit (coalescing knob drags) */
	double panel_tick;    /* the editor panel's left edge, ticks */
	float panel_ppt;      /* the editor panel's zoom, px per tick */
	bool panel_follow;
} AppState;

extern AppState st;

bool app_start(const char *project, bool headless);
void app_stop(void);
bool app_busy(void);
bool app_command_by_name(const char *name);

/* ---- editing (each records one undo step) --------------------------------------- */
void app_edit_begin(void);              /* lock the audio stream */
void app_edit_end(const char *what);    /* re-sort, unlock, record history ("~what": coalesced), mark modified */
void app_set_song(Song *s, const char *what); /* take s (moved), e.g. a generated song */
int app_add_track(const char *name, Patch p);
void app_delete_track(int t);
void app_undo(void);
void app_redo(void);

/* ---- transport --------------------------------------------------------------- */
void app_play(bool on);
void app_seek(u32 tick);
u32 app_tick(void);                     /* the playhead */
void app_preview_chord(int track, const u8 notes[6], bool up);
void app_preview_note(int track, int note);
void app_preview_release(void);

/* ---- the chord palette ------------------------------------------------------------ */
const ChordLib *app_chords(void);       /* for the selected track */
bool app_current_chord(u8 notes[6]);    /* the palette's voicing on the selected track */
void app_insert_chord(u32 tick);        /* the palette chord with the pattern at tick */
void app_insert_note(int track, u32 tick, u32 len, int note);
void app_delete_event(int index);

/* ---- jobs ---------------------------------------------------------------------- */
void app_generate(void);
void app_export(void);
bool app_save(const char *path);        /* NULL: the current path (or a default name) */
bool app_open(const char *path);
void app_new(void);

#endif
