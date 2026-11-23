/* The application's state outside the widgets (app.h). */
#define _POSIX_C_SOURCE 200809L
#include "app.h"
#include "guitar.h"
#include "presets.h"
#include "ui.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

AppState st;

static void name_from_path(void)
{
	const char *b = strrchr(st.path, '/');
	b = b ? b + 1 : st.path;
	snprintf(st.name, sizeof st.name, "%s", st.path[0] ? b : "Untitled");
}

/* ---- the new project: a band set up, a progression on the guitar ----------------- */

static void demo_song(Song *s)
{
	song_init(s);
	s->bpm = 104;
	s->key = 57;
	s->final_key = 57;
	s->mode = 5;
	s->total_bars = 8;
	snprintf(s->style, sizeof s->style, "folk");
	snprintf(s->description, sizeof s->description, "new project: Am F C G");
	s->sections[s->nsections++] = (Section){SEC_VERSE, 0, 8};
	static const struct {
		const char *name, *preset;
		float gain, pan, reverb;
	} tr[] = {{"Drums", "Drum kit", 0.7f, 0.0f, 0.08f},
	          {"Bass", "Electric bass", 0.7f, 0.0f, 0.0f},
	          {"Guitar", "Steel acoustic", 0.65f, -0.3f, 0.2f},
	          {"Pad", "Warm pad", 0.3f, 0.25f, 0.4f}};
	for (size_t t = 0; t < sizeof tr / sizeof tr[0]; t++) {
		s->patches[t] = preset_patch(preset_find(tr[t].preset));
		snprintf(s->track_name[t], sizeof s->track_name[t], "%s", tr[t].name);
		s->mix.gain[t] = tr[t].gain;
		s->mix.pan[t] = tr[t].pan;
		s->mix.reverb[t] = tr[t].reverb;
	}
	s->ntracks = 4;
	ChordLib *lib = malloc(sizeof *lib);
	int open[6];
	int strings = gtr_open_strings(&s->patches[2], open);
	chordlib_build(lib, open, strings);
	static const char *prog[4] = {"Am", "F", "C", "G"};
	const StrumPattern *pat = &strum_patterns[strum_find("Folk")];
	for (u32 bar = 0; bar < 8; bar++) {
		int r, q;
		chord_parse(prog[bar % 4], &r, &q);
		u8 notes[6];
		voicing_notes(lib, chordlib_get(lib, r, q, 0), notes);
		u32 t0 = bar * (u32)ticks_per_bar;
		strum_write(s, 2, t0, (u32)ticks_per_bar, notes, strings, pat, 92, ART_NORMAL, false);
		/* bass: root on 1, fifth on 3 */
		NoteEvent b = note_event();
		b.ch = 1;
		b.tick = t0;
		b.len = 36;
		b.vel = 100;
		b.notes[0] = (u8)(28 + (r + 12 - 4) % 12);
		song_add(s, b);
		b.tick = t0 + 48;
		b.notes[0] = (u8)(b.notes[0] + 7 > 52 ? b.notes[0] - 5 : b.notes[0] + 7);
		song_add(s, b);
		/* pad: the triad, the whole bar */
		NoteEvent pd = note_event();
		pd.ch = 3;
		pd.tick = t0;
		pd.len = (u16)ticks_per_bar;
		pd.vel = 70;
		pd.count = 3;
		for (int k = 0; k < 3; k++)
			pd.notes[k] = (u8)(60 + (r + qualities[q].iv[k]) % 12);
		song_add(s, pd);
		/* drums: kick 1 and 3, snare 2 and 4, 8th hats */
		for (int st8 = 0; st8 < 8; st8++) {
			NoteEvent d = note_event();
			d.ch = 0;
			d.tick = t0 + (u32)st8 * 12;
			d.len = 6;
			d.vel = st8 % 2 ? 70 : 90;
			d.notes[0] = DR_HAT_CLOSED;
			song_add(s, d);
			if (st8 == 0 || st8 == 4 || (st8 == 5 && bar % 2)) {
				d.notes[0] = DR_KICK, d.vel = 110;
				song_add(s, d);
			}
			if (st8 == 2 || st8 == 6) {
				d.notes[0] = DR_SNARE, d.vel = 100;
				song_add(s, d);
			}
		}
	}
	free(lib);
	song_sort(s);
}

/* ---- start / stop ------------------------------------------------------------------- */

static void reset_editor(void)
{
	st.track = st.song.ntracks > 2 ? 2 : 0;
	for (int t = 0; t < st.song.ntracks; t++)
		if (st.song.patches[t].inst == INST_GUITAR && st.song.patches[t].gtr != GTR_BASS) {
			st.track = t;
			break;
		}
	st.sel = -1;
	st.cursor = 0;
	st.view_tick = 0;
	st.lane_scroll = 0;
	st.lib_track = -1;
}

static void attach(void)
{
	audio_lock(st.audio);
	Player *p = audio_player(st.audio);
	player_attach(p, &st.song);
	p->volume_target = st.master;
	st.playing = false;
	audio_unlock(st.audio);
}

bool app_start(const char *project, bool headless)
{
	st.audio = audio_open(!headless);
	if (!st.audio) {
		fprintf(stderr, "ft2002: audio initialisation failed\n");
		return false;
	}
	st.master = 0.8f;
	st.px_per_tick = 0.5f;
	st.follow = true;
	st.piano_lo = 36;
	st.lib = malloc(sizeof *st.lib);
	st.lib_track = -1;
	st.ch_root = 9;
	st.ch_q = Q_MIN;
	st.pattern = strum_find("Folk");
	st.art = ART_NORMAL;
	st.ins_beats = 4;
	st.gen = (AutogenOptions){.seed = 2002, .mood = MOOD_TRIUMPH, .band = BAND_ROCK, .bpm = 0, .key = -1};
	st.gen_bpm = 120;
	st.gen_key = 9;
	st.exp = export_defaults(EXP_MP3);
	song_init(&st.song);
	bool ok = false;
	if (project) {
		char err[256];
		ok = project_load(&st.song, project, err, sizeof err);
		if (ok)
			snprintf(st.path, sizeof st.path, "%s", project);
		else
			ui_status("%s", err);
	}
	if (!ok)
		demo_song(&st.song);
	history_init(&st.hist, &st.song);
	name_from_path();
	reset_editor();
	attach();
	if (ok)
		ui_status("Opened %s", st.path);
	else if (!project)
		ui_status("New project. Space plays; the Chords view strums on the selected guitar. Audio: %s",
		          audio_device_name(st.audio));
	return true;
}

void app_stop(void)
{
	if (st.exp_state == 1) {
		st.exp_cancel = true;
		while (st.exp_state == 1) {
			struct timespec ts = {0, 10000000};
			nanosleep(&ts, NULL);
		}
	}
	audio_close(st.audio);
	st.audio = NULL;
	history_free(&st.hist);
	song_free(&st.song);
	free(st.lib);
}

bool app_busy(void) { return st.playing || st.exp_state == 1; }

bool app_command_by_name(const char *name)
{
	static const struct {
		const char *n;
		int cmd;
	} names[] = {
	    {"quit", CMD_QUIT},           {"theme-light", CMD_THEME_LIGHT}, {"theme-dark", CMD_THEME_DARK},
	    {"new", CMD_NEW},             {"save", CMD_SAVE},               {"undo", CMD_UNDO},
	    {"redo", CMD_REDO},           {"play", CMD_PLAY},               {"stop", CMD_STOP},
	    {"generate", CMD_GENERATE},   {"export", CMD_EXPORT_MP3},       {"export-opus", CMD_EXPORT_OPUS},
	    {"export-wav", CMD_EXPORT_WAV}, {"add-guitar", CMD_ADD_GUITAR}, {"add-bass", CMD_ADD_BASS},
	    {"add-synth", CMD_ADD_SYNTH}, {"add-drums", CMD_ADD_DRUMS},     {"delete-track", CMD_DELETE_TRACK},
	    {"loop", CMD_LOOP},           {"metronome", CMD_METRONOME},     {"to-start", CMD_TO_START},
	};
	for (size_t i = 0; i < FT_COUNT(names); i++)
		if (!strcasecmp(name, names[i].n)) {
			run_command(names[i].cmd);
			return true;
		}
	/* "side NAME": open a side view */
	static const char *sides[SIDE_COUNT] = {"tracks", "instrument", "chords", "mixer", "generate", "export", "settings"};
	if (!strncasecmp(name, "side ", 5))
		for (int i = 0; i < SIDE_COUNT; i++)
			if (!strcasecmp(name + 5, sides[i])) {
				app.side = i;
				app.side_open = true;
				return true;
			}
	/* "track N": select a track; "seed N" */
	if (!strncasecmp(name, "track ", 6)) {
		int t = atoi(name + 6);
		if (t >= 0 && t < st.song.ntracks)
			st.track = t, st.sel = -1;
		return true;
	}
	if (!strncasecmp(name, "seed ", 5)) {
		st.gen.seed = strtoull(name + 5, NULL, 10);
		return true;
	}
	if (!strncasecmp(name, "band ", 5))
		return parse_band(name + 5, &st.gen.band);
	if (!strncasecmp(name, "mood ", 5))
		return parse_mood(name + 5, &st.gen.mood);
	if (!strncasecmp(name, "chord ", 6))
		return chord_parse(name + 6, &st.ch_root, &st.ch_q) ? (st.ch_voicing = 0, true) : false;
	if (!strncasecmp(name, "seek ", 5)) {
		app_seek((u32)atoi(name + 5) * (u32)ticks_per_bar);
		return true;
	}
	if (!strncasecmp(name, "export-to ", 10)) {
		snprintf(st.exp_path, sizeof st.exp_path, "%s", name + 10);
		const char *e = strrchr(st.exp_path, '.');
		st.exp.fmt = e && !strcasecmp(e, ".opus") ? EXP_OPUS : e && !strcasecmp(e, ".wav") ? EXP_WAV : EXP_MP3;
		app_export();
		return true;
	}
	if (!strcasecmp(name, "wait-export")) {
		while (st.exp_state == 1) {
			struct timespec ts = {0, 20000000};
			nanosleep(&ts, NULL);
		}
		return true;
	}
	return false;
}

/* ---- editing --------------------------------------------------------------------- */

void app_edit_begin(void) { audio_lock(st.audio); }

void app_edit_end(const char *what)
{
	song_sort(&st.song);
	Player *p = audio_player(st.audio);
	/* the sequencer's next event index moved with the sort */
	if (p->song == &st.song) {
		u32 lo = 0, hi = st.song.nevents;
		while (lo < hi) {
			u32 mid = lo + (hi - lo) / 2;
			if (st.song.events[mid].tick <= p->tick)
				lo = mid + 1;
			else
				hi = mid;
		}
		p->next_event = lo;
	}
	audio_unlock(st.audio);
	if (what) {
		/* "~name": a continuous edit (a knob drag); one undo step while it repeats */
		if (what[0] == '~' && st.hist.n && !strcmp(history_current(&st.hist), what) && ft_now_ns() - st.edit_ns < 1500000000ull)
			history_amend(&st.hist, &st.song);
		else
			history_push(&st.hist, &st.song, what);
		st.edit_ns = ft_now_ns();
		st.modified = true;
	}
	app.dirty = true;
}

void app_set_song(Song *s, const char *what)
{
	audio_lock(st.audio);
	Player *p = audio_player(st.audio);
	player_hard_stop(p);
	song_free(&st.song);
	st.song = *s;
	memset(s, 0, sizeof *s);
	player_attach(p, &st.song);
	st.playing = false;
	audio_unlock(st.audio);
	history_push(&st.hist, &st.song, what);
	st.modified = true;
	reset_editor();
	app.dirty = true;
}

int app_add_track(const char *name, Patch p)
{
	if (st.song.ntracks >= FT_MAX_TRACKS) {
		ui_status("A project holds %d tracks", FT_MAX_TRACKS);
		return -1;
	}
	app_edit_begin();
	int t = st.song.ntracks++;
	snprintf(st.song.track_name[t], sizeof st.song.track_name[t], "%s", name);
	st.song.patches[t] = p;
	st.song.role[t] = 0;
	st.song.mix.gain[t] = 0.7f;
	st.song.mix.pan[t] = 0;
	st.song.mix.reverb[t] = 0.15f;
	st.song.mix.delay[t] = 0;
	st.song.mix.duck[t] = 0;
	st.song.mix.chorus[t] = 0;
	st.song.mix.highpass[t] = 0;
	st.song.mix.bright[t] = false;
	if (st.song.total_bars == 0)
		st.song.total_bars = 8;
	app_edit_end("add track");
	st.track = t;
	st.sel = -1;
	return t;
}

void app_delete_track(int t)
{
	if (t < 0 || t >= st.song.ntracks)
		return;
	app_edit_begin();
	Song *s = &st.song;
	Player *p = audio_player(st.audio);
	/* silence what the track was playing; the voices of later tracks move down */
	for (int k = t; k < FT_MAX_TRACKS; k++) {
		for (int i = 0; i < VOICES_PER_TRACK; i++)
			p->voices[k][i].active = false;
		gtr_reset(p->gtr[k]);
	}
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
	/* mute and solo masks follow */
	u32 lowmask = (1u << t) - 1;
	p->mute = (p->mute & lowmask) | ((p->mute >> 1) & ~lowmask);
	p->solo = (p->solo & lowmask) | ((p->solo >> 1) & ~lowmask);
	app_edit_end("delete track");
	if (st.track >= s->ntracks)
		st.track = s->ntracks - 1;
	if (st.track < 0)
		st.track = 0;
	st.sel = -1;
	st.lib_track = -1;
}

static void after_history(const char *what, const char *verb)
{
	audio_lock(st.audio);
	Player *p = audio_player(st.audio);
	u32 tick = p->tick;
	bool run = p->running;
	player_attach(p, &st.song);
	p->running = false;
	audio_unlock(st.audio);
	st.playing = false;
	if (run) {
		app_seek(tick);
		app_play(true);
	}
	if (st.track >= st.song.ntracks)
		st.track = st.song.ntracks ? st.song.ntracks - 1 : 0;
	st.sel = -1;
	st.lib_track = -1;
	st.modified = true;
	ui_status("%s: %s", verb, what);
}

void app_undo(void)
{
	audio_lock(st.audio);
	const char *w = history_undo(&st.hist, &st.song);
	audio_unlock(st.audio);
	if (!w) {
		ui_status("Nothing to undo");
		return;
	}
	after_history(w, "Undo");
}

void app_redo(void)
{
	audio_lock(st.audio);
	const char *w = history_redo(&st.hist, &st.song);
	audio_unlock(st.audio);
	if (!w) {
		ui_status("Nothing to redo");
		return;
	}
	after_history(w, "Redo");
}

/* ---- transport -------------------------------------------------------------------- */

void app_play(bool on)
{
	audio_lock(st.audio);
	Player *p = audio_player(st.audio);
	if (p->song != &st.song)
		player_attach(p, &st.song);
	if (on && p->tick >= song_total_ticks(&st.song))
		player_seek(p, 0);
	player_set_running(p, on);
	st.playing = p->running;
	audio_unlock(st.audio);
	app.dirty = true;
}

void app_seek(u32 tick)
{
	audio_lock(st.audio);
	Player *p = audio_player(st.audio);
	if (p->song != &st.song)
		player_attach(p, &st.song);
	player_seek(p, tick);
	audio_unlock(st.audio);
	st.cursor = tick;
	app.dirty = true;
}

u32 app_tick(void)
{
	Player *p = audio_player(st.audio);
	return p->tick;
}

void app_preview_chord(int track, const u8 notes[6], bool up)
{
	if (track < 0 || track >= st.song.ntracks)
		return;
	NoteEvent e = note_event();
	e.ch = (u8)track;
	e.len = (u16)ticks_per_bar;
	e.vel = 100;
	if (st.song.patches[track].inst == INST_GUITAR) {
		e.fx = FX_STRUM;
		e.count = 6;
		e.strum = up ? -9 : 9;
		e.art = (u8)st.art;
		memcpy(e.notes, notes, 6);
	} else {
		/* a synth plays the chord's notes (up to 4), a kit its first */
		e.count = 0;
		for (int k = 0; k < 6 && e.count < 4; k++)
			if (notes[k])
				e.notes[e.count++] = notes[k];
		if (!e.count)
			return;
	}
	audio_lock(st.audio);
	player_preview(audio_player(st.audio), &e);
	audio_unlock(st.audio);
}

void app_preview_note(int track, int note)
{
	if (track < 0 || track >= st.song.ntracks)
		return;
	NoteEvent e = note_event();
	e.ch = (u8)track;
	e.len = (u16)(ticks_per_bar / 2);
	e.notes[0] = (u8)note;
	e.art = (u8)st.art;
	audio_lock(st.audio);
	player_preview(audio_player(st.audio), &e);
	audio_unlock(st.audio);
}

void app_preview_release(void)
{
	audio_lock(st.audio);
	player_preview_release(audio_player(st.audio));
	audio_unlock(st.audio);
}

/* ---- the chord palette ------------------------------------------------------------- */

const ChordLib *app_chords(void)
{
	int t = st.track;
	Patch p = t >= 0 && t < st.song.ntracks && st.song.patches[t].inst == INST_GUITAR ? st.song.patches[t]
	                                                                                    : gtr_preset(GTR_STEEL);
	int open[6];
	int strings = gtr_open_strings(&p, open);
	int key = strings;
	for (int i = 0; i < 6; i++)
		key = key * 131 + open[i];
	if (st.lib_track != t || st.lib_key != key) {
		chordlib_build(st.lib, open, strings);
		st.lib_track = t;
		st.lib_key = key;
		int n = chordlib_count(st.lib, st.ch_root, st.ch_q);
		if (st.ch_voicing >= n)
			st.ch_voicing = 0;
	}
	return st.lib;
}

bool app_current_chord(u8 notes[6])
{
	const ChordLib *lib = app_chords();
	int n = chordlib_count(lib, st.ch_root, st.ch_q);
	if (!n)
		return false;
	int v = st.ch_voicing < n ? st.ch_voicing : 0;
	voicing_notes(lib, chordlib_get(lib, st.ch_root, st.ch_q, v), notes);
	return true;
}

void app_insert_chord(u32 tick)
{
	int t = st.track;
	if (t < 0 || t >= st.song.ntracks)
		return;
	u8 notes[6];
	if (!app_current_chord(notes))
		return;
	tick -= tick % (u32)ticks_per_row;
	u32 len = (u32)st.ins_beats * (u32)(ticks_per_row * rows_per_beat);
	char name[16];
	chord_name(st.ch_root, st.ch_q, name, sizeof name);
	app_edit_begin();
	/* the chord replaces what the track had in its span */
	Song *s = &st.song;
	u32 w = 0;
	for (u32 i = 0; i < s->nevents; i++) {
		const NoteEvent *e = &s->events[i];
		if (e->ch == t && e->tick >= tick && e->tick < tick + len)
			continue;
		s->events[w++] = *e;
	}
	s->nevents = w;
	const Patch *p = &s->patches[t];
	if (p->inst == INST_GUITAR) {
		int open[6];
		int strings = gtr_open_strings(p, open);
		const StrumPattern *pat = &strum_patterns[st.pattern >= 0 && st.pattern < strum_pattern_count ? st.pattern : 0];
		strum_write(s, t, tick, len, notes, strings, pat, 96, (u8)st.art, false);
	} else {
		NoteEvent e = note_event();
		e.ch = (u8)t;
		e.tick = tick;
		e.len = (u16)len;
		e.vel = 90;
		e.count = 0;
		for (int k = 0; k < 6 && e.count < 4; k++)
			if (notes[k])
				e.notes[e.count++] = (u8)(notes[k] + (p->inst == INST_SYNTH ? 12 : 0));
		song_add(s, e);
	}
	u32 end_bar = (tick + len + (u32)ticks_per_bar - 1) / (u32)ticks_per_bar;
	if (end_bar > s->total_bars)
		s->total_bars = end_bar;
	char what[48];
	snprintf(what, sizeof what, "insert %s", name);
	app_edit_end(what);
	st.cursor = tick + len;
	ui_status("Inserted %s (%s) at bar %u", name, strum_patterns[st.pattern].name, tick / ticks_per_bar + 1);
}

void app_insert_note(int track, u32 tick, u32 len, int note)
{
	if (track < 0 || track >= st.song.ntracks)
		return;
	NoteEvent e = note_event();
	e.ch = (u8)track;
	e.tick = tick;
	e.len = (u16)(len ? len : (u32)ticks_per_row);
	e.vel = 100;
	e.notes[0] = (u8)note;
	e.art = st.song.patches[track].inst == INST_GUITAR ? (u8)st.art : 0;
	app_edit_begin();
	song_add(&st.song, e);
	u32 end_bar = (tick + e.len + (u32)ticks_per_bar - 1) / (u32)ticks_per_bar;
	if (end_bar > st.song.total_bars)
		st.song.total_bars = end_bar;
	app_edit_end("insert note");
	/* select the new event */
	for (u32 i = 0; i < st.song.nevents; i++) {
		const NoteEvent *x = &st.song.events[i];
		if (x->ch == track && x->tick == tick && x->notes[0] == note) {
			st.sel = (int)i;
			break;
		}
	}
}

void app_delete_event(int index)
{
	if (index < 0 || (u32)index >= st.song.nevents)
		return;
	app_edit_begin();
	Song *s = &st.song;
	memmove(&s->events[index], &s->events[index + 1], (s->nevents - (u32)index - 1) * sizeof *s->events);
	s->nevents--;
	app_edit_end("delete");
	st.sel = -1;
}

/* ---- jobs ---------------------------------------------------------------------- */

void app_generate(void)
{
	AutogenOptions o = st.gen;
	o.bpm = st.gen_bpm_on ? (float)st.gen_bpm : 0;
	o.key = st.gen_key_on ? st.gen_key : -1;
	Song s;
	u64 t0 = ft_now_ns();
	if (!autogen(&s, &o)) {
		ui_status("Generating failed: out of memory");
		return;
	}
	char what[48];
	snprintf(what, sizeof what, "generate %llu", (unsigned long long)o.seed);
	app_set_song(&s, what);
	st.path[0] = 0;
	snprintf(st.name, sizeof st.name, "%s-%llu", band_name(o.band), (unsigned long long)o.seed);
	for (char *c = st.name; *c; c++)
		if (*c == '+')
			*c = '-';
	ui_status("Generated in %.0f ms: %s", (double)(ft_now_ns() - t0) / 1e6, st.song.description);
}

typedef struct {
	Song song;
	ExportOptions opt;
	char path[1024];
} ExportJob;

static void *export_main(void *arg)
{
	ExportJob *j = arg;
	bool ok = export_song(&j->song, j->path, &j->opt, &st.exp_report, st.exp_err, sizeof st.exp_err, &st.exp_progress,
	                      &st.exp_cancel);
	song_free(&j->song);
	free(j);
	st.exp_state = ok ? 2 : 3;
	return NULL;
}

void app_export(void)
{
	if (st.exp_state == 1) {
		ui_status("An export is running");
		return;
	}
	if (!st.exp_path[0]) {
		char cwd[512];
		if (!getcwd(cwd, sizeof cwd))
			snprintf(cwd, sizeof cwd, ".");
		char base[128];
		snprintf(base, sizeof base, "%s", st.name);
		char *dot = strrchr(base, '.');
		if (dot && !strcmp(dot, ".ft2"))
			*dot = 0;
		snprintf(st.exp_path, sizeof st.exp_path, "%s/%s.%s", cwd, base, export_ext(st.exp.fmt));
	} else {
		/* keep the name, follow the format */
		char *dot = strrchr(st.exp_path, '.');
		char *slash = strrchr(st.exp_path, '/');
		if (dot && (!slash || dot > slash))
			*dot = 0;
		size_t n = strlen(st.exp_path);
		snprintf(st.exp_path + n, sizeof st.exp_path - n, ".%s", export_ext(st.exp.fmt));
	}
	ExportJob *j = calloc(1, sizeof *j);
	if (!j)
		return;
	audio_lock(st.audio);
	song_copy(&j->song, &st.song);
	audio_unlock(st.audio);
	j->opt = st.exp;
	char title[128];
	snprintf(title, sizeof title, "%s", st.name);
	j->opt.title = NULL; /* the song's own when the project has no name */
	snprintf(j->path, sizeof j->path, "%s", st.exp_path);
	st.exp_progress = 0;
	st.exp_cancel = false;
	st.exp_err[0] = 0;
	st.exp_state = 1;
	pthread_t th;
	if (pthread_create(&th, NULL, export_main, j) != 0) {
		st.exp_state = 3;
		snprintf(st.exp_err, sizeof st.exp_err, "cannot start a thread");
		song_free(&j->song);
		free(j);
		return;
	}
	pthread_detach(th);
	ui_status("Exporting %s...", st.exp_path);
}

bool app_save(const char *path)
{
	char p[1024];
	if (path)
		snprintf(p, sizeof p, "%s", path);
	else if (st.path[0])
		snprintf(p, sizeof p, "%s", st.path);
	else {
		char cwd[512];
		if (!getcwd(cwd, sizeof cwd))
			snprintf(cwd, sizeof cwd, ".");
		snprintf(p, sizeof p, "%s/%s.ft2", cwd, st.name);
	}
	char err[256];
	audio_lock(st.audio);
	bool ok = project_save(&st.song, p, err, sizeof err);
	audio_unlock(st.audio);
	if (!ok) {
		ui_status("%s", err);
		return false;
	}
	snprintf(st.path, sizeof st.path, "%s", p);
	name_from_path();
	st.modified = false;
	ui_status("Saved %s", p);
	return true;
}

bool app_open(const char *path)
{
	Song s;
	song_init(&s);
	char err[256];
	if (!project_load(&s, path, err, sizeof err)) {
		song_free(&s);
		ui_status("%s", err);
		return false;
	}
	app_set_song(&s, "open");
	history_free(&st.hist);
	history_init(&st.hist, &st.song);
	snprintf(st.path, sizeof st.path, "%s", path);
	name_from_path();
	st.modified = false;
	ui_status("Opened %s", path);
	return true;
}

void app_new(void)
{
	Song s;
	demo_song(&s);
	app_set_song(&s, "new");
	st.path[0] = 0;
	name_from_path();
	st.modified = false;
}
