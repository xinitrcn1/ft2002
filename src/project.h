/* Projects on disk and the editing history (docs/projects.md).
 *
 * A project is a Song (song.h) saved as text, one record a line, so it
 * diffs and merges well:
 *
 *     ft2002 1
 *     song bpm=126 key=57 final_key=57 mode=5 seed=7 mood=calm bars=64 style="techno"
 *     mixbus delay_rows=3 delay_feedback=0.4 reverb_size=0.8
 *     section verse 16 8
 *     track 0 "Rhythm" role=0
 *     patch 0 inst=guitar gtr=steel gtr_tone=0.6 ...       (every field, key=value)
 *     mix 0 gain=0.7 pan=-0.3 ...
 *     ev 0 TICK LEN VEL LAYER FX COUNT ART STRUM STRING GATE N0 N1 N2 N3 N4 N5
 *
 * Floats are written with 9 significant digits, so a save / load round
 * trip is exact. Unknown keys are skipped (older builds read newer files). */
#ifndef FT_PROJECT_H
#define FT_PROJECT_H

#include "song.h"

bool project_save(const Song *s, const char *path, char *err, size_t errn);
bool project_load(Song *s, const char *path, char *err, size_t errn); /* s is (re)initialised */

/* Patch fields by name (the instrument panel and the file format share them). */
typedef enum : u8 { PF_FLOAT, PF_INT, PF_BOOL, PF_U8, PF_ENUM } PatchFieldType;
typedef struct {
	const char *name;
	PatchFieldType type;
	size_t offset;
	float lo, hi;            /* the panel's range */
	const char *const *enums; /* PF_ENUM: names, NULL-terminated */
} PatchField;
extern const PatchField patch_fields[];
extern const int patch_field_count;
float patch_get(const Patch *p, const PatchField *f);
void patch_set(Patch *p, const PatchField *f, float v);
const PatchField *patch_field(const char *name);

/* ---- undo ------------------------------------------------------------------- */

#define UNDO_DEPTH 64

typedef struct {
	Song snap[UNDO_DEPTH];
	char what[UNDO_DEPTH][48];
	int n;       /* snapshots held */
	int at;      /* the current state's index; redo goes to at + 1 */
	int head;    /* ring start */
} History;

void history_init(History *h, const Song *s);
void history_free(History *h);
/* Record s after an edit called `what`. */
void history_push(History *h, const Song *s, const char *what);
/* Replace the current state with s, keeping its name (a knob drag or a
 * spin held: one step however many values went by). */
void history_amend(History *h, const Song *s);
const char *history_current(const History *h); /* the current state's edit name */
/* Step back / forward: copies into s; returns the edit's name or NULL. */
const char *history_undo(History *h, Song *s);
const char *history_redo(History *h, Song *s);

#endif
