/* Projects: save / load round trips exactly (every field, every event),
 * undo / redo restore states; the autogenerator makes playable songs for
 * every band and is deterministic. */
#include "autogen.h"
#include "export.h"
#include "project.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, ...)                                                                                                  \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			fails++;                                                                                       \
			printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                    \
			printf(__VA_ARGS__);                                                                           \
			printf("\n");                                                                                  \
		}                                                                                                      \
	} while (0)

/* field by field: the structs have padding bytes */
static bool same_event(const NoteEvent *a, const NoteEvent *b)
{
	return a->tick == b->tick && a->len == b->len && a->ch == b->ch && a->vel == b->vel && a->layer == b->layer &&
	       a->fx == b->fx && a->count == b->count && a->art == b->art && !memcmp(a->notes, b->notes, 6) &&
	       a->gate == b->gate && a->strum == b->strum && a->string == b->string;
}

static bool same(const Song *a, const Song *b)
{
	if (a->nevents != b->nevents || a->ntracks != b->ntracks || a->nsections != b->nsections)
		return false;
	for (u32 i = 0; i < a->nevents; i++)
		if (!same_event(&a->events[i], &b->events[i]))
			return false;
	for (u32 i = 0; i < a->nsections; i++)
		if (a->sections[i].kind != b->sections[i].kind || a->sections[i].start_bar != b->sections[i].start_bar ||
		    a->sections[i].bars != b->sections[i].bars)
			return false;
	for (int t = 0; t < a->ntracks; t++) {
		for (int k = 0; k < patch_field_count; k++)
			if (patch_get(&a->patches[t], &patch_fields[k]) != patch_get(&b->patches[t], &patch_fields[k]))
				return false;
		if (memcmp(a->patches[t].gtr_tuning, b->patches[t].gtr_tuning, 6) || strcmp(a->track_name[t], b->track_name[t]) ||
		    a->role[t] != b->role[t] || a->mix.gain[t] != b->mix.gain[t] || a->mix.pan[t] != b->mix.pan[t] ||
		    a->mix.reverb[t] != b->mix.reverb[t] || a->mix.highpass[t] != b->mix.highpass[t] ||
		    a->mix.chorus[t] != b->mix.chorus[t] || a->mix.duck[t] != b->mix.duck[t] || a->mix.bright[t] != b->mix.bright[t])
			return false;
	}
	return a->bpm == b->bpm && a->key == b->key && a->total_bars == b->total_bars && a->seed == b->seed &&
	       !strcmp(a->style, b->style) && !strcmp(a->description, b->description);
}

int main(void)
{
	const char *path = "test_project.ft2";
	for (int band = 0; band < BAND_COUNT; band++) {
		AutogenOptions o = {.seed = 11 + (u64)band, .mood = (Mood)(band % MOOD_COUNT), .band = (Band)band, .key = -1,
		                   .variations = band % 2 == 0};
		Song s, t, u;
		CHECK(autogen(&s, &o), "autogen %s", band_name((Band)band));
		CHECK(autogen(&u, &o) && same(&s, &u), "%s: not deterministic", band_name((Band)band));
		song_free(&u);
		CHECK(s.ntracks > 3 && s.nevents > 100, "%s: %d tracks, %u events", band_name((Band)band), s.ntracks, s.nevents);
		bool guitars = false;
		for (int k = 0; k < s.ntracks; k++)
			guitars |= s.patches[k].inst == INST_GUITAR;
		CHECK(guitars == (band != BAND_ELECTRONIC), "%s: guitars", band_name((Band)band));
		for (u32 i = 0; i < s.nevents; i++)
			if (s.events[i].ch >= s.ntracks) {
				CHECK(false, "%s: event on track %u", band_name((Band)band), s.events[i].ch);
				break;
			}
		/* a round trip */
		char err[256];
		CHECK(project_save(&s, path, err, sizeof err), "save: %s", err);
		song_init(&t);
		CHECK(project_load(&t, path, err, sizeof err), "load: %s", err);
		CHECK(same(&s, &t), "%s: the round trip differs", band_name((Band)band));
		/* the first 20 seconds render cleanly */
		Song cut;
		song_copy(&cut, &s);
		cut.total_bars = cut.total_bars < 10 ? cut.total_bars : 10;
		ExportOptions eo = export_defaults(EXP_WAV);
		u64 n;
		float *x = render_song(&cut, &eo, &n);
		float pk = 0;
		bool fin = true;
		for (u64 i = 0; x && i < n * 2; i++)
			fin &= isfinite(x[i]), pk = fabsf(x[i]) > pk ? fabsf(x[i]) : pk;
		CHECK(x && fin && pk < 1.0f && pk > 0.05f, "%s: render peak %g", band_name((Band)band), pk);
		free(x);
		song_free(&cut);
		/* undo / redo */
		History h;
		history_init(&h, &s);
		Song edit;
		song_copy(&edit, &s);
		edit.bpm = 77;
		history_push(&h, &edit, "bpm");
		edit.nevents = 0;
		history_push(&h, &edit, "clear");
		CHECK(!strcmp(history_undo(&h, &edit), "clear") && edit.nevents == s.nevents && edit.bpm == 77, "undo 1");
		CHECK(!strcmp(history_undo(&h, &edit), "bpm") && same(&edit, &s), "undo 2");
		CHECK(history_undo(&h, &edit) == NULL, "undo past the start");
		CHECK(!strcmp(history_redo(&h, &edit), "bpm") && edit.bpm == 77, "redo");
		history_free(&h);
		song_free(&edit);
		song_free(&s);
		song_free(&t);
	}
	/* the undo ring keeps the last UNDO_DEPTH states */
	{
		Song s;
		song_init(&s);
		History h;
		history_init(&h, &s);
		for (int i = 0; i < UNDO_DEPTH * 2; i++) {
			s.bpm = (float)(100 + i);
			history_push(&h, &s, "bpm");
		}
		int steps = 0;
		while (history_undo(&h, &s))
			steps++;
		CHECK(steps == UNDO_DEPTH - 1, "undo depth %d", steps);
		CHECK(s.bpm == (float)(100 + UNDO_DEPTH), "oldest kept %g", s.bpm);
		history_free(&h);
		song_free(&s);
	}
	remove(path);
	if (fails)
		printf("%d failures\n", fails);
	return fails != 0;
}
