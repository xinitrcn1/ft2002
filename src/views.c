/* The application's views (views.h): the transport bar, the side views
 * (tracks, instrument, chords, mixer, generate, export, settings), commands
 * and keys. The arrangement and the editors are in editors.c. */
#include "views_int.h"
#include "composer.h"
#include "presets.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

/* ---- shared helpers ---------------------------------------------------------- */

u32 track_color(int t)
{
	static const u32 pal[8] = {0xE5534BFF, 0xF0A030FF, 0xD9C043FF, 0x57AB5AFF, 0x39C5CFFF, 0x4C8DF6FF, 0x986EE2FF, 0xD65FA6FF};
	return pal[(t % 8 + 8) % 8];
}

u32 with_alpha(u32 c, u32 a) { return (c & 0xFFFFFF00u) | (a & 255); }

const char *track_kind(int t)
{
	const Patch *p = &st.song.patches[t];
	if (p->inst == INST_GUITAR)
		return gtr_model_name(p->gtr);
	if (p->inst == INST_KIT)
		return "Drum kit";
	if (p->formant)
		return "Synth: choir";
	return p->unison ? "Synth: supersaw" : "Synth";
}

static u32 tf_focus;
static size_t tf_cursor;

bool ui_textfield(u32 id, int x, int y, int w, int h, char *buf, size_t cap)
{
	bool over = ui_hit(x, y, w, h);
	if (over && app.pressed[0]) {
		tf_focus = id;
		app.focus = id;
		tf_cursor = strlen(buf);
	}
	bool focused = app.focus == id && tf_focus == id;
	bool enter = false;
	if (focused && app.text[0]) {
		size_t n = strlen(buf), add = strlen(app.text);
		if (n + add < cap) {
			memmove(buf + tf_cursor + add, buf + tf_cursor, n - tf_cursor + 1);
			memcpy(buf + tf_cursor, app.text, add);
			tf_cursor += add;
		}
		app.text[0] = 0;
	}
	if (focused)
		for (int i = 0; i < app.nkeys; i++)
			if (app.keys[i] == SDLK_RETURN)
				enter = true;
	if (tf_cursor > strlen(buf))
		tf_cursor = strlen(buf);
	draw_rect(x, y, w, h, COL(C_INPUT_BG));
	draw_frame(x, y, w, h, focused ? COL(C_FOCUS_BORDER) : COL(C_INPUT_BORDER));
	int ty = y + (h - line_h(FONT_UI)) / 2;
	/* show the end of a long path */
	const char *s = buf;
	while (*s && font_width(FONT_UI, s, strlen(s)) > w - PX(12))
		s++;
	draw_text(FONT_UI, x + PX(6), ty, s, strlen(s), COL(C_INPUT_FG));
	if (focused) {
		size_t off = (size_t)(s - buf);
		int cx = x + PX(6) + (tf_cursor >= off ? font_width(FONT_UI, s, tf_cursor - off) : 0);
		draw_rect(cx, ty, PX(1), line_h(FONT_UI), COL(C_CURSOR));
	}
	return enter;
}

bool textfield_key(const Event *e)
{
	if (!tf_focus || app.focus != tf_focus)
		return false;
	return e->key == SDLK_BACKSPACE || e->key == SDLK_LEFT || e->key == SDLK_RIGHT || e->key == SDLK_HOME ||
	       e->key == SDLK_END || e->key == SDLK_ESCAPE || e->key == SDLK_DELETE || (e->key < 128 && !(e->mods & MOD_CTRL));
}

/* ---- views.h -------------------------------------------------------------------- */

bool views_init(void) { return true; }
void views_shutdown(void) {}

const char *views_title(void)
{
	static char t[200];
	snprintf(t, sizeof t, "%s%s", st.name, st.modified ? " \xe2\x97\x8f" : "");
	return t;
}

void views_status_right(char *buf, size_t n)
{
	const Song *s = &st.song;
	snprintf(buf, n, "%s  %.0f BPM  4/4  %u bars  %d tracks  48 kHz", note_names[((s->key % 12) + 12) % 12], s->bpm,
	         s->total_bars, s->ntracks);
}

static int focus_buf_track = -1;
static char *focus_buf; /* the text field being typed in */

void views_tick(void)
{
	audio_pump(st.audio);
	if (st.playing) {
		Player *p = audio_player(st.audio);
		if (p->finished && !p->loop) {
			app_play(false);
			ui_status("End of the song");
		}
		follow_playhead();
		app.dirty = true;
	}
	static int last_state;
	if (st.exp_state != last_state) {
		last_state = st.exp_state;
		if (st.exp_state == 2)
			ui_status("Exported %s: %.1f s, peak %.1f dBFS, %.1f MB", st.exp_path, st.exp_report.seconds,
			          st.exp_report.peak_dbfs, (double)st.exp_report.bytes / 1e6);
		else if (st.exp_state == 3)
			ui_status("Export failed: %s", st.exp_err);
		app.dirty = true;
	}
	if (st.exp_state == 1)
		app.dirty = true;
	(void)focus_buf_track;
	(void)focus_buf;
}

void views_command(int cmd)
{
	switch (cmd) {
	case CMD_NEW:
		app_new();
		ui_status("New project");
		break;
	case CMD_OPEN:
		app.side = SIDE_SETTINGS, app.side_open = true;
		ui_status("Open: type a path in Settings > Project and press Return");
		break;
	case CMD_SAVE:
		app_save(NULL);
		break;
	case CMD_SAVE_AS:
		app.side = SIDE_SETTINGS, app.side_open = true;
		ui_status("Save As: type a path in Settings > Project and press Return");
		break;
	case CMD_UNDO:
		app_undo();
		break;
	case CMD_REDO:
		app_redo();
		break;
	case CMD_PLAY:
		app_play(!st.playing);
		break;
	case CMD_STOP:
		app_play(false);
		app_seek(0);
		break;
	case CMD_TO_START:
		app_seek(0);
		st.view_tick = 0;
		st.panel_tick = 0;
		break;
	case CMD_LOOP: {
		audio_lock(st.audio);
		Player *p = audio_player(st.audio);
		p->loop = !p->loop;
		if (p->loop && p->loop_end <= p->loop_start) {
			/* the bar under the cursor, 4 bars */
			u32 b = st.cursor / ticks_per_bar;
			p->loop_start = b * ticks_per_bar;
			p->loop_end = p->loop_start + 4 * ticks_per_bar;
		}
		bool on = p->loop;
		u32 a = p->loop_start, b = p->loop_end;
		audio_unlock(st.audio);
		ui_status(on ? "Loop on: bars %u-%u (Shift+drag on the ruler sets it)" : "Loop off", a / ticks_per_bar + 1,
		          b / ticks_per_bar);
		break;
	}
	case CMD_METRONOME: {
		audio_lock(st.audio);
		Player *p = audio_player(st.audio);
		p->metronome = !p->metronome;
		bool on = p->metronome;
		audio_unlock(st.audio);
		ui_status(on ? "Metronome on" : "Metronome off");
		break;
	}
	case CMD_ADD_GUITAR:
		app_add_track("Guitar", preset_patch(preset_find("Steel acoustic")));
		break;
	case CMD_ADD_BASS:
		app_add_track("Bass", preset_patch(preset_find("Electric bass")));
		break;
	case CMD_ADD_SYNTH:
		app_add_track("Synth", preset_patch(preset_find("Supersaw")));
		break;
	case CMD_ADD_DRUMS:
		app_add_track("Drums", preset_patch(preset_find("Drum kit")));
		break;
	case CMD_DELETE_TRACK:
		if (st.song.ntracks > 1) {
			char n[32];
			snprintf(n, sizeof n, "%s", st.song.track_name[st.track]);
			app_delete_track(st.track);
			ui_status("Deleted track %s (Ctrl+Z restores it)", n);
		}
		break;
	case CMD_GENERATE:
		app_generate();
		break;
	case CMD_EXPORT_MP3:
	case CMD_EXPORT_OPUS:
	case CMD_EXPORT_WAV:
		st.exp.fmt = cmd == CMD_EXPORT_MP3 ? EXP_MP3 : cmd == CMD_EXPORT_OPUS ? EXP_OPUS : EXP_WAV;
		app.side = SIDE_EXPORT, app.side_open = true;
		app_export();
		break;
	case CMD_ABOUT:
		ui_status("FT2002: modelled guitars, geosim's synth and composer, vxide's UI. C23 + Vulkan + SDL3.");
		break;
	default:
		break;
	}
}

static char path_buf[1024];

bool views_key(const Event *e)
{
	if (textfield_key(e)) {
		/* edit the focused field: path_buf (Settings) or st.exp_path (Export) */
		char *b = app.focus == ui_id("path", 0) ? path_buf : app.focus == ui_id("exp-path", 0) ? st.exp_path : NULL;
		if (!b)
			return false;
		size_t n = strlen(b);
		if (tf_cursor > n)
			tf_cursor = n;
		switch (e->key) {
		case SDLK_BACKSPACE:
			if (tf_cursor > 0) {
				memmove(b + tf_cursor - 1, b + tf_cursor, n - tf_cursor + 1);
				tf_cursor--;
			}
			return true;
		case SDLK_DELETE:
			if (tf_cursor < n)
				memmove(b + tf_cursor, b + tf_cursor + 1, n - tf_cursor);
			return true;
		case SDLK_LEFT:
			tf_cursor = tf_cursor ? tf_cursor - 1 : 0;
			return true;
		case SDLK_RIGHT:
			tf_cursor = tf_cursor < n ? tf_cursor + 1 : n;
			return true;
		case SDLK_HOME:
			tf_cursor = 0;
			return true;
		case SDLK_END:
			tf_cursor = n;
			return true;
		case SDLK_ESCAPE:
			app.focus = 0;
			tf_focus = 0;
			return true;
		case SDLK_RETURN:
			return false; /* the field sees it in app.keys */
		default:
			return true; /* printable keys arrive as text */
		}
	}
	return editor_key(e);
}

/* ---- the transport bar ---------------------------------------------------------- */

static void meter(int x, int y, int w, int h, float v)
{
	draw_rect(x, y, w, h, COL(C_INPUT_BG));
	float db = v > 1e-6f ? 20.0f * log10f(v) : -120.0f;
	float t = (db + 48.0f) / 48.0f;
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	int fill = (int)(w * t);
	u32 c = db > -1.5f ? 0xE5534BFF : db > -9.0f ? 0xD9C043FF : 0x57AB5AFF;
	draw_rect(x, y, fill, h, c);
}

void views_draw_transport(int x, int y, int w, int h)
{
	Player *p = audio_player(st.audio);
	int bx = x + PX(8), by = y + PX(5), bh = h - PX(10), bw = PX(30);
	if (ui_icon_button(ui_id("tr", 0), bx, by, bw, bh, ICON_TO_START, false, "To start (Home)"))
		run_command(CMD_TO_START);
	bx += bw + PX(2);
	if (ui_icon_button(ui_id("tr", 1), bx, by, bw, bh, st.playing ? ICON_PAUSE : ICON_PLAY, st.playing, "Play / pause (Space)"))
		run_command(CMD_PLAY);
	bx += bw + PX(2);
	if (ui_icon_button(ui_id("tr", 2), bx, by, bw, bh, ICON_STOP, false, "Stop"))
		run_command(CMD_STOP);
	bx += bw + PX(2);
	if (ui_icon_button(ui_id("tr", 3), bx, by, bw, bh, ICON_LOOP, p->loop, "Loop (L)"))
		run_command(CMD_LOOP);
	bx += bw + PX(2);
	if (ui_icon_button(ui_id("tr", 4), bx, by, bw, bh, ICON_METRONOME, p->metronome, "Metronome (K)"))
		run_command(CMD_METRONOME);
	bx += bw + PX(14);

	/* position: bar . beat . 16th, and time */
	u32 tick = app_tick();
	u32 bar = tick / ticks_per_bar, beat = tick % ticks_per_bar / (ticks_per_row * rows_per_beat),
	    six = tick % (ticks_per_row * rows_per_beat) / ticks_per_row;
	char pos[64];
	snprintf(pos, sizeof pos, "%03u.%u.%u", bar + 1, beat + 1, six + 1);
	draw_rect(bx, by, PX(150), bh, COL(C_INPUT_BG));
	draw_text(FONT_MONO_BOLD, bx + PX(8), y + (h - line_h(FONT_MONO_BOLD)) / 2, pos, strlen(pos), COL(C_FOCUS_BORDER));
	double sec = tick * song_seconds_per_tick(&st.song), total = song_duration(&st.song);
	char tm[64];
	snprintf(tm, sizeof tm, "%d:%04.1f / %d:%04.1f", (int)sec / 60, fmod(sec, 60.0), (int)total / 60, fmod(total, 60.0));
	draw_text(FONT_SMALL, bx + PX(80), y + (h - line_h(FONT_SMALL)) / 2, tm, strlen(tm), COL(C_FG_DIM));
	bx += PX(160);

	/* tempo */
	int bpm = (int)lrintf(st.song.bpm);
	ui_label(FONT_SMALL, bx, y + (h - line_h(FONT_SMALL)) / 2, COL(C_FG_DIM), "BPM");
	bx += PX(30);
	if (ui_spin(ui_id("bpm", 0), bx, by, PX(80), bh, &bpm, 40, 260, "%d")) {
		app_edit_begin();
		st.song.bpm = (float)bpm;
		Player *pl = audio_player(st.audio);
		(void)pl;
		app_edit_end("~tempo");
	}
	bx += PX(90);
	/* section under the playhead */
	const char *sec_name = "";
	for (u32 i = 0; i < st.song.nsections; i++)
		if (bar >= st.song.sections[i].start_bar && bar < st.song.sections[i].start_bar + st.song.sections[i].bars)
			sec_name = section_name(st.song.sections[i].kind);
	if (sec_name[0]) {
		draw_text(FONT_UI_BOLD, bx, y + (h - line_h(FONT_UI_BOLD)) / 2, sec_name, strlen(sec_name), COL(C_FG));
	}

	/* master volume and the output meter, right */
	int mx = x + w - PX(260);
	draw_icon(ICON_SPEAKER, mx, y + (h - PX(16)) / 2, COL(C_FG_DIM));
	if (ui_hslider(ui_id("master", 0), mx + PX(22), by, PX(110), bh, &st.master, 0.0f, 1.0f, 0.8f)) {
		audio_lock(st.audio);
		audio_player(st.audio)->volume_target = st.master;
		audio_unlock(st.audio);
	}
	meter(mx + PX(142), by + PX(4), PX(110), PX(8), p->peak_l);
	meter(mx + PX(142), by + bh - PX(12), PX(110), PX(8), p->peak_r);
}

/* ---- side views --------------------------------------------------------------------- */

static void side_tracks(int x, int y, int w, int h)
{
	Player *p = audio_player(st.audio);
	int row = PX(54), ry = y;
	for (int t = 0; t < st.song.ntracks && ry < y + h - PX(70); t++, ry += row) {
		bool sel = t == st.track;
		u32 id = ui_id("trow", t);
		bool over = ui_hit(x, ry, w, row);
		if (sel)
			draw_rect(x, ry, w, row, COL(C_LIST_ACTIVE_BG));
		else if (over)
			draw_rect(x, ry, w, row, COL(C_LIST_HOVER_BG));
		draw_rect(x + PX(8), ry + PX(6), PX(4), row - PX(12), track_color(t));
		ui_label(FONT_UI_BOLD, x + PX(20), ry + PX(6), sel ? COL(C_LIST_ACTIVE_FG) : COL(C_FG), "%s", st.song.track_name[t]);
		ui_label(FONT_SMALL, x + PX(20), ry + PX(8) + line_h(FONT_UI_BOLD), COL(C_DESCRIPTION_FG), "%s", track_kind(t));
		/* mute / solo */
		int bx = x + w - PX(150);
		bool m = (p->mute >> t) & 1u, s = (p->solo >> t) & 1u;
		if (ui_button(ui_id("mute", t), bx, ry + PX(8), PX(22), PX(20), "M", m)) {
			audio_lock(st.audio);
			p->mute ^= 1u << t;
			audio_unlock(st.audio);
		}
		if (ui_button(ui_id("solo", t), bx + PX(24), ry + PX(8), PX(22), PX(20), "S", s)) {
			audio_lock(st.audio);
			p->solo ^= 1u << t;
			audio_unlock(st.audio);
		}
		float g = st.song.mix.gain[t];
		if (ui_hslider(ui_id("tgain", t), bx + PX(52), ry + PX(8), PX(88), PX(20), &g, 0.0f, 1.2f, 0.7f)) {
			app_edit_begin();
			st.song.mix.gain[t] = g;
			app_edit_end("~volume");
		}
		/* a level meter under the slider */
		float lv = p->peak[t];
		float db = lv > 1e-6f ? 20.0f * log10f(lv) : -120.0f;
		float f = (db + 48.0f) / 48.0f;
		f = f < 0 ? 0 : f > 1 ? 1 : f;
		draw_rect(bx + PX(52), ry + PX(32), (int)(PX(88) * f), PX(3), track_color(t));
		/* select on click (not on the buttons) */
		if (over && app.pressed[0] && app.mx < bx) {
			st.track = t;
			st.sel = -1;
			if (app.clicks >= 2)
				app.side = SIDE_INSTRUMENT;
		}
		(void)id;
	}
	int by = y + h - PX(64), bw = (w - PX(40)) / 4;
	static const char *add[4] = {"+ Guitar", "+ Bass", "+ Synth", "+ Drums"};
	static const int cmd[4] = {CMD_ADD_GUITAR, CMD_ADD_BASS, CMD_ADD_SYNTH, CMD_ADD_DRUMS};
	for (int i = 0; i < 4; i++)
		if (ui_button(ui_id("addtrack", i), x + PX(12) + i * (bw + PX(5)), by, bw, PX(26), add[i], false))
			run_command(cmd[i]);
	if (ui_button(ui_id("deltrack", 0), x + PX(12), by + PX(32), w - PX(24), PX(24), "Delete track", false))
		run_command(CMD_DELETE_TRACK);
}

/* a row of knobs for patch fields by name; returns the next y */
static int knob_row(int x, int y, int w, const char *const *names, const char *const *labels, int n)
{
	Patch *pt = &st.song.patches[st.track];
	int ks = PX(40), gap = (w - n * ks) / (n > 1 ? n : 1);
	for (int i = 0; i < n; i++) {
		const PatchField *f = patch_field(names[i]);
		if (!f)
			continue;
		float v = patch_get(pt, f);
		Patch def = st.song.patches[st.track].inst == INST_GUITAR ? gtr_preset(pt->gtr) : patch_default();
		int kx = x + i * (ks + gap) + gap / 2;
		if (ui_knob(ui_id(names[i], st.track), kx, y, ks, labels[i], &v, f->lo, f->hi, patch_get(&def, f))) {
			app_edit_begin();
			patch_set(pt, f, v);
			char what[48];
			snprintf(what, sizeof what, "~%s", labels[i]);
			app_edit_end(what);
		}
	}
	return y + ks + PX(26);
}

static void retune(const i8 tune[6], const char *name)
{
	app_edit_begin();
	memcpy(st.song.patches[st.track].gtr_tuning, tune, 6);
	app_edit_end(name);
	ui_status("Tuning: %s", name);
}

static int instrument_preset_sel = -1;

static void side_instrument(int x, int y, int w, int h)
{
	if (st.track < 0 || st.track >= st.song.ntracks)
		return;
	Patch *pt = &st.song.patches[st.track];
	int pad = PX(14), cw = w - 2 * pad, cy = y;
	ui_label(FONT_UI_BOLD, x + pad, cy, track_color(st.track), "%s", st.song.track_name[st.track]);
	ui_label(FONT_SMALL, x + pad + text_w(FONT_UI_BOLD, st.song.track_name[st.track]) + PX(8), cy + PX(2), COL(C_FG_DIM),
	         "%s", track_kind(st.track));
	cy += PX(26);
	/* presets */
	ui_section(x + pad, cy, cw, "PRESET");
	cy += PX(22);
	/* the preset list starts at the track's own instrument */
	static int preset_for = -1;
	if (preset_for != st.track || instrument_preset_sel < 0) {
		preset_for = st.track;
		instrument_preset_sel = 0;
		for (int i = 0; i < preset_count; i++) {
			Patch pp = preset_patch(i);
			if (pp.inst == pt->inst && (pt->inst != INST_GUITAR || pp.gtr == pt->gtr)) {
				instrument_preset_sel = i;
				break;
			}
		}
	}
	int ps = instrument_preset_sel;
	char fmt[64];
	snprintf(fmt, sizeof fmt, "%s", presets[ps].name);
	for (char *c = fmt; *c; c++)
		if (*c == '%')
			*c = ' ';
	ui_spin(ui_id("preset", 0), x + pad, cy, cw - PX(70), PX(24), &instrument_preset_sel, 0, preset_count - 1, fmt);
	if (ui_button(ui_id("preset-apply", 0), x + pad + cw - PX(64), cy, PX(64), PX(24), "Apply", false)) {
		app_edit_begin();
		*pt = preset_patch(instrument_preset_sel);
		app_edit_end("preset");
		ui_status("%s: %s", st.song.track_name[st.track], presets[instrument_preset_sel].name);
		st.lib_track = -1;
	}
	cy += PX(34);
	if (pt->inst == INST_GUITAR) {
		ui_section(x + pad, cy, cw, "GUITAR");
		cy += PX(24);
		int m = pt->gtr;
		snprintf(fmt, sizeof fmt, "%s", gtr_model_name((GuitarModel)m));
		if (ui_spin(ui_id("gmodel", 0), x + pad, cy, cw, PX(24), &m, 0, GTR_COUNT - 1, fmt)) {
			app_edit_begin();
			Patch np = gtr_preset((GuitarModel)m);
			memcpy(np.gtr_tuning, pt->gtr_tuning, 6);
			np.gtr_capo = pt->gtr_capo;
			*pt = np;
			app_edit_end("guitar model");
			st.lib_track = -1;
		}
		cy += PX(36);
		static const char *n1[] = {"gtr_tone", "gtr_pick", "gtr_sustain", "gtr_gain", "gtr_body"};
		static const char *l1[] = {"Tone", "Pick", "Sustain", "Drive", "Body"};
		cy = knob_row(x + pad, cy, cw, n1, l1, 5);
		static const char *n2[] = {"gain"};
		static const char *l2[] = {"Level"};
		knob_row(x + pad, cy, PX(60), n2, l2, 1);
		int capo = pt->gtr_capo;
		ui_label(FONT_SMALL, x + pad + PX(80), cy + PX(2), COL(C_FG_DIM), "Capo");
		if (ui_spin(ui_id("capo", 0), x + pad + PX(80), cy + PX(18), PX(90), PX(24), &capo, 0, 12, "fret %d")) {
			app_edit_begin();
			pt->gtr_capo = (u8)capo;
			app_edit_end("~capo");
		}
		cy += PX(70);
		ui_section(x + pad, cy, cw, "TUNING");
		cy += PX(24);
		if (pt->gtr != GTR_BASS) {
			static const struct {
				const char *name;
				i8 t[6];
			} tunings[] = {{"Standard", {40, 45, 50, 55, 59, 64}},
			               {"Drop D", {38, 45, 50, 55, 59, 64}},
			               {"Half down", {39, 44, 49, 54, 58, 63}},
			               {"DADGAD", {38, 45, 50, 55, 57, 62}},
			               {"Open G", {38, 43, 50, 55, 59, 62}},
			               {"Open D", {38, 45, 50, 54, 57, 62}}};
			int bw = (cw - PX(10)) / 3;
			for (int i = 0; i < 6; i++) {
				bool on = !memcmp(pt->gtr_tuning, tunings[i].t, 6) || (i == 0 && !pt->gtr_tuning[0]);
				if (ui_button(ui_id("tuning", i), x + pad + (i % 3) * (bw + PX(5)), cy + (i / 3) * PX(30), bw, PX(26),
				              tunings[i].name, on))
					retune(tunings[i].t, tunings[i].name);
			}
			cy += PX(64);
		}
		int open[6];
		int strings = gtr_open_strings(pt, open);
		char tn[64] = "";
		for (int s = 0; s < strings; s++) {
			size_t l = strlen(tn);
			snprintf(tn + l, sizeof tn - l, "%s%d ", note_names[open[s] % 12], open[s] / 12 - 1);
		}
		ui_label(FONT_MONO, x + pad, cy, COL(C_FG_DIM), "%s", tn);
		cy += PX(24);
		ui_section(x + pad, cy, cw, "ARTICULATION (inserted notes, previews)");
		cy += PX(24);
		int bw = (cw - PX(10)) / 3;
		for (int a = 0; a < ART_COUNT && cy + (a / 3) * PX(28) < y + h - PX(28); a++)
			if (ui_button(ui_id("art", a), x + pad + (a % 3) * (bw + PX(5)), cy + (a / 3) * PX(28), bw, PX(24), art_name(a),
			              st.art == a))
				st.art = a;
		return;
	}
	if (pt->inst == INST_KIT) {
		ui_section(x + pad, cy, cw, "KICK");
		cy += PX(24);
		static const char *k1[] = {"kick_pitch", "kick_end", "kick_decay", "kick_click", "kick_drive"};
		static const char *kl1[] = {"Pitch", "End", "Decay", "Click", "Drive"};
		cy = knob_row(x + pad, cy, cw, k1, kl1, 5);
		ui_section(x + pad, cy, cw, "SNARE / HATS");
		cy += PX(24);
		static const char *k2[] = {"snare_tone", "snare_decay", "hat_decay", "hat_tone", "gain"};
		static const char *kl2[] = {"Tone", "Decay", "Hat dec", "Hat tone", "Level"};
		knob_row(x + pad, cy, cw, k2, kl2, 5);
		return;
	}
	/* a synth */
	ui_section(x + pad, cy, cw, "OSCILLATORS");
	cy += PX(24);
	static const char *const wnames[] = {"pulse", "saw", "triangle", "sine", "noise", "table"};
	int w1 = pt->w1, w2 = pt->w2;
	snprintf(fmt, sizeof fmt, "osc 1: %s", wnames[w1 % 6]);
	if (ui_spin(ui_id("w1", 0), x + pad, cy, cw / 2 - PX(4), PX(24), &w1, 0, 4, fmt)) {
		app_edit_begin();
		pt->w1 = (Wave)w1;
		app_edit_end("~wave");
	}
	snprintf(fmt, sizeof fmt, "osc 2: %s", wnames[w2 % 6]);
	if (ui_spin(ui_id("w2", 0), x + pad + cw / 2 + PX(4), cy, cw / 2 - PX(4), PX(24), &w2, 0, 4, fmt)) {
		app_edit_begin();
		pt->w2 = (Wave)w2;
		app_edit_end("~wave");
	}
	cy += PX(32);
	static const char *o1[] = {"mix2", "detune", "semi2", "sub", "unison", "spread"};
	static const char *ol1[] = {"Osc 2", "Detune", "Semi", "Sub", "Unison", "Spread"};
	cy = knob_row(x + pad, cy, cw, o1, ol1, 6);
	ui_section(x + pad, cy, cw, "FILTER");
	cy += PX(24);
	static const char *f1[] = {"cutoff", "resonance", "fenv", "fdecay", "key_track", "drive"};
	static const char *fl1[] = {"Cutoff", "Reso", "Env", "Decay", "Keytrk", "Drive"};
	cy = knob_row(x + pad, cy, cw, f1, fl1, 6);
	ui_section(x + pad, cy, cw, "AMP");
	cy += PX(24);
	static const char *a1[] = {"attack", "decay", "sustain", "release", "gain"};
	static const char *al1[] = {"Attack", "Decay", "Sustain", "Release", "Level"};
	cy = knob_row(x + pad, cy, cw, a1, al1, 5);
	ui_section(x + pad, cy, cw, "MOVEMENT");
	cy += PX(24);
	static const char *m1[] = {"vib_depth", "vib_rate", "glide", "duty", "pwm_depth"};
	static const char *ml1[] = {"Vibrato", "Rate", "Glide", "Width", "PWM"};
	cy = knob_row(x + pad, cy, cw, m1, ml1, 5);
	bool p4 = pt->poles4, mono = pt->mono;
	if (ui_toggle(ui_id("poles4", 0), x + pad, cy, "24 dB", &p4)) {
		app_edit_begin();
		pt->poles4 = p4;
		app_edit_end("24 dB");
	}
	if (ui_toggle(ui_id("mono", 0), x + pad + PX(80), cy, "Mono", &mono)) {
		app_edit_begin();
		pt->mono = mono;
		app_edit_end("mono");
	}
	int fm = pt->formant;
	static const char *const fnames[] = {"vowel: off", "vowel: aah", "vowel: ooh"};
	if (ui_spin(ui_id("formant", 0), x + pad + PX(150), cy - PX(2), cw - PX(150), PX(22), &fm, 0, 2, fnames[fm % 3])) {
		app_edit_begin();
		pt->formant = (u8)fm;
		app_edit_end("~vowel");
	}
}

/* a chord box: strings vertical, 5 frets, dots, x / o above, the base fret aside */
static void chord_box(const ChordLib *lib, const Voicing *v, int x, int y, int w, int h, bool sel, bool over)
{
	if (sel)
		draw_rect(x, y, w, h, COL(C_LIST_ACTIVE_BG));
	else if (over)
		draw_rect(x, y, w, h, COL(C_LIST_HOVER_BG));
	int strings = lib->strings;
	int gx = x + PX(16), gy = y + PX(18), gw = w - PX(26), gh = h - PX(30);
	int base = v->hi <= 4 ? 1 : v->lo;
	float sx = (float)gw / (float)(strings - 1), fy = (float)gh / 5.0f;
	u32 line = sel ? COL(C_LIST_ACTIVE_FG) : COL(C_FG_DIM);
	for (int s = 0; s < strings; s++)
		draw_rect(gx + (int)(s * sx), gy, PX(1), gh, line);
	for (int f = 0; f <= 5; f++)
		draw_rect(gx, gy + (int)(f * fy), gw + PX(1), base == 1 && f == 0 ? PX(3) : PX(1), line);
	if (base > 1)
		ui_label(FONT_SMALL, x + PX(2), gy + PX(1), COL(C_FG_DIM), "%d", base);
	for (int s = 0; s < strings; s++) {
		int fx = gx + (int)(s * sx);
		if (v->fret[s] < 0)
			draw_text(FONT_SMALL, fx - PX(3), y + PX(2), "x", 1, COL(C_FG_DIM));
		else if (v->fret[s] == 0)
			draw_text(FONT_SMALL, fx - PX(3), y + PX(2), "o", 1, COL(C_FG_DIM));
		else {
			int row = v->fret[s] - base;
			int cy = gy + (int)((row + 0.5f) * fy);
			int r = PX(5);
			draw_rect(fx - r + PX(1), cy - r + PX(1), 2 * r - PX(1), 2 * r - PX(1), sel ? COL(C_LIST_ACTIVE_FG) : COL(C_FG));
		}
	}
	if (v->barre && v->barre >= base) {
		int first = -1, last = -1;
		for (int s = 0; s < strings; s++)
			if (v->fret[s] == v->barre) {
				if (first < 0)
					first = s;
				last = s;
			}
		if (first >= 0 && last > first) {
			int cy = gy + (int)((v->barre - base + 0.5f) * fy);
			draw_rect(gx + (int)(first * sx), cy - PX(2), (int)((last - first) * sx) + PX(1), PX(4), sel ? COL(C_LIST_ACTIVE_FG) : COL(C_FG));
		}
	}
}

static void side_chords(int x, int y, int w, int h)
{
	const ChordLib *lib = app_chords();
	int pad = PX(12), cw = w - 2 * pad, cy = y;
	bool guitar = st.song.patches[st.track].inst == INST_GUITAR;
	char name[32];
	chord_name(st.ch_root, st.ch_q, name, sizeof name);
	ui_label(FONT_H2, x + pad, cy - PX(4), COL(C_FG), "%s", name);
	ui_label(FONT_SMALL, x + pad + text_w(FONT_H2, name) + PX(10), cy + PX(6), COL(C_FG_DIM), "on %s%s",
	         st.song.track_name[st.track], guitar ? "" : " (as notes)");
	cy += line_h(FONT_H2) + PX(4);
	/* roots */
	int bw = (cw - 5 * PX(3)) / 6;
	for (int r = 0; r < 12; r++)
		if (ui_button(ui_id("root", r), x + pad + (r % 6) * (bw + PX(3)), cy + (r / 6) * PX(27), bw, PX(24), note_names[r],
		              st.ch_root == r)) {
			st.ch_root = r;
			st.ch_voicing = 0;
			u8 n[6];
			if (app_current_chord(n))
				app_preview_chord(st.track, n, false);
		}
	cy += PX(60);
	/* qualities */
	int qw = (cw - 4 * PX(3)) / 5;
	for (int q = 0; q < Q_COUNT; q++) {
		const char *l = q == Q_MAJ ? "maj" : q == Q_MIN ? "m" : qualities[q].suffix;
		if (ui_button(ui_id("qual", q), x + pad + (q % 5) * (qw + PX(3)), cy + (q / 5) * PX(25), qw, PX(22), l, st.ch_q == q)) {
			st.ch_q = q;
			st.ch_voicing = 0;
			u8 n[6];
			if (app_current_chord(n))
				app_preview_chord(st.track, n, false);
		}
	}
	cy += PX(106);
	/* voicings */
	int n = chordlib_count(lib, st.ch_root, st.ch_q);
	int cols = 4, boxw = (cw - (cols - 1) * PX(4)) / cols, boxh = PX(96);
	int rows_fit = (y + h - PX(150) - cy) / (boxh + PX(4));
	if (rows_fit < 1)
		rows_fit = 1;
	for (int i = 0; i < n && i < rows_fit * cols; i++) {
		int bx = x + pad + (i % cols) * (boxw + PX(4)), by = cy + (i / cols) * (boxh + PX(4));
		const Voicing *v = chordlib_get(lib, st.ch_root, st.ch_q, i);
		bool over = ui_hit(bx, by, boxw, boxh);
		chord_box(lib, v, bx, by, boxw, boxh, i == st.ch_voicing, over);
		if (over && (app.pressed[0] || app.pressed[2])) {
			st.ch_voicing = i;
			u8 notes[6];
			voicing_notes(lib, v, notes);
			app_preview_chord(st.track, notes, app.pressed[2]);
			if (app.clicks >= 2 && app.pressed[0])
				app_insert_chord(st.cursor);
		}
		if (over) {
			char shape[32];
			voicing_shape(lib, v, shape, sizeof shape);
			static char tipbuf[64];
			snprintf(tipbuf, sizeof tipbuf, "%s %s  (click: strum, right: up-strum, double: insert)", name, shape);
			(void)tipbuf;
		}
	}
	cy += ((n < rows_fit * cols ? n : rows_fit * cols) + cols - 1) / cols * (boxh + PX(4)) + PX(6);
	/* how it is written */
	ui_section(x + pad, cy, cw, "INSERT AT THE CURSOR");
	cy += PX(22);
	char fmt[64];
	snprintf(fmt, sizeof fmt, "%s (%s)", strum_patterns[st.pattern].name, strum_patterns[st.pattern].style);
	ui_spin(ui_id("pattern", 0), x + pad, cy, cw, PX(24), &st.pattern, 0, strum_pattern_count - 1, fmt);
	cy += PX(28);
	ui_label(FONT_MONO, x + pad, cy, COL(C_FG_DIM), "%s", strum_patterns[st.pattern].code);
	cy += PX(22);
	snprintf(fmt, sizeof fmt, "%%d beats");
	ui_spin(ui_id("beats", 0), x + pad, cy, cw / 2 - PX(4), PX(24), &st.ins_beats, 1, 32, fmt);
	if (ui_button(ui_id("insert", 0), x + pad + cw / 2 + PX(4), cy, cw / 2 - PX(4), PX(24), "Insert (Enter)", true))
		app_insert_chord(st.cursor);
}

static void side_mixer(int x, int y, int w, int h)
{
	Player *p = audio_player(st.audio);
	int pad = PX(10), cy = y, ks = PX(30);
	MixSettings *m = &st.song.mix;
	for (int t = 0; t < st.song.ntracks && cy < y + h - PX(140); t++) {
		draw_rect(x + pad, cy, PX(3), ks + PX(16), track_color(t));
		draw_text_fit(FONT_SMALL_BOLD, x + pad + PX(8), cy, st.song.track_name[t], strlen(st.song.track_name[t]), PX(70),
		              t == st.track ? COL(C_FOCUS_BORDER) : COL(C_FG));
		float lv = p->peak[t];
		float db = lv > 1e-6f ? 20.0f * log10f(lv) : -120.0f;
		float f = (db + 48.0f) / 48.0f;
		f = f < 0 ? 0 : f > 1 ? 1 : f;
		draw_rect(x + pad + PX(8), cy + PX(20), PX(64), PX(4), COL(C_INPUT_BG));
		draw_rect(x + pad + PX(8), cy + PX(20), (int)(PX(64) * f), PX(4), track_color(t));
		struct {
			float *v;
			const char *l;
			float lo, hi, def;
		} k[] = {{&m->gain[t], "Vol", 0, 1.2f, 0.7f},    {&m->pan[t], "Pan", -1, 1, 0},
		         {&m->reverb[t], "Rev", 0, 1, 0.15f},   {&m->delay[t], "Dly", 0, 1, 0},
		         {&m->chorus[t], "Cho", 0, 1, 0},       {&m->highpass[t], "HP", 0, 400, 0}};
		int kx = x + pad + PX(80);
		int gap = (w - pad - PX(80) - 6 * ks) / 6;
		for (int i = 0; i < 6; i++) {
			float v = *k[i].v;
			if (ui_knob(ui_id(k[i].l, t), kx + i * (ks + gap), cy, ks, k[i].l, &v, k[i].lo, k[i].hi, k[i].def)) {
				app_edit_begin();
				*k[i].v = v;
				char what[32];
				snprintf(what, sizeof what, "~%s", k[i].l);
				app_edit_end(what);
			}
		}
		if (ui_hit(x, cy, PX(80), ks + PX(16)) && app.pressed[0])
			st.track = t;
		cy += ks + PX(22);
	}
	cy = y + h - PX(130);
	ui_section(x + pad, cy, w - 2 * pad, "EFFECTS BUS");
	cy += PX(24);
	struct {
		float *v;
		const char *l;
		float lo, hi, def;
	} b[] = {{&m->reverb_size, "Room", 0, 1, 0.8f}, {&m->delay_rows, "Delay", 1, 8, 3}, {&m->delay_feedback, "Feedback", 0, 0.9f, 0.4f}};
	for (int i = 0; i < 3; i++) {
		float v = *b[i].v;
		if (ui_knob(ui_id("bus", i), x + pad + PX(10) + i * PX(80), cy, PX(40), b[i].l, &v, b[i].lo, b[i].hi, b[i].def)) {
			app_edit_begin();
			*b[i].v = i == 1 ? roundf(v) : v;
			app_edit_end("~bus");
		}
	}
	ui_label(FONT_SMALL, x + pad, cy + PX(64), COL(C_FG_DIM), "delay in 16ths (3 = a dotted 8th)");
}

static void side_generate(int x, int y, int w, int h)
{
	int pad = PX(14), cw = w - 2 * pad, cy = y;
	ui_section(x + pad, cy, cw, "SEED");
	cy += PX(24);
	int seed = (int)(st.gen.seed % 1000000);
	if (ui_spin(ui_id("seed", 0), x + pad, cy, cw - PX(40), PX(26), &seed, 1, 999999, "%d"))
		st.gen.seed = (u64)seed;
	if (ui_icon_button(ui_id("dice", 0), x + pad + cw - PX(34), cy, PX(34), PX(26), ICON_DICE_S, false, "A random seed"))
		st.gen.seed = (next_seed(ft_now_ns()) % 999999) + 1;
	cy += PX(38);
	ui_section(x + pad, cy, cw, "MOOD");
	cy += PX(24);
	int bw = (cw - PX(8)) / 3;
	for (int m = 0; m < MOOD_COUNT; m++)
		if (ui_button(ui_id("mood", m), x + pad + (m % 3) * (bw + PX(4)), cy + (m / 3) * PX(30), bw, PX(26), mood_name((Mood)m),
		              st.gen.mood == m))
			st.gen.mood = (Mood)m;
	cy += PX(68);
	ui_section(x + pad, cy, cw, "BAND");
	cy += PX(24);
	for (int b = 0; b < BAND_COUNT; b++)
		if (ui_button(ui_id("band", b), x + pad + (b % 3) * (bw + PX(4)), cy + (b / 3) * PX(30), bw, PX(26), band_name((Band)b),
		              st.gen.band == b))
			st.gen.band = (Band)b;
	cy += PX(68);
	ui_section(x + pad, cy, cw, "OVERRIDES");
	cy += PX(24);
	ui_toggle(ui_id("gbpm-on", 0), x + pad, cy + PX(3), "Tempo", &st.gen_bpm_on);
	ui_spin(ui_id("gbpm", 0), x + pad + PX(90), cy, cw - PX(90), PX(24), &st.gen_bpm, 60, 200, "%d BPM");
	cy += PX(30);
	ui_toggle(ui_id("gkey-on", 0), x + pad, cy + PX(3), "Key", &st.gen_key_on);
	char kf[16];
	snprintf(kf, sizeof kf, "%s", note_names[st.gen_key % 12]);
	ui_spin(ui_id("gkey", 0), x + pad + PX(90), cy, cw - PX(90), PX(24), &st.gen_key, 0, 11, kf);
	cy += PX(40);
	if (ui_button(ui_id("generate", 0), x + pad, cy, cw, PX(34), "Generate song (Ctrl+G)", true))
		app_generate();
	cy += PX(46);
	/* what is loaded now */
	ui_section(x + pad, cy, cw, "THIS SONG");
	cy += PX(22);
	const char *d = st.song.description;
	/* wrap at spaces into the side bar */
	const char *c = d;
	while (*c && cy + line_h(FONT_SMALL) <= y + h) {
		size_t best = 0, n = 0;
		while (c[n]) {
			size_t m = n;
			while (c[m] && c[m] != ' ')
				m++;
			if (font_width(FONT_SMALL, c, m) > cw - PX(4) && best)
				break;
			best = m;
			n = c[m] ? m + 1 : m;
			if (!c[m])
				break;
		}
		draw_text(FONT_SMALL, x + pad, cy, c, best, COL(C_DESCRIPTION_FG));
		cy += line_h(FONT_SMALL) + PX(2);
		c += best;
		while (*c == ' ')
			c++;
	}
}

static void side_export(int x, int y, int w, int h)
{
	int pad = PX(14), cw = w - 2 * pad, cy = y;
	ui_section(x + pad, cy, cw, "FORMAT");
	cy += PX(24);
	static const char *fn[EXP_COUNT] = {"WAV", "MP3", "Opus"};
	static const int order[3] = {EXP_MP3, EXP_OPUS, EXP_WAV};
	int bw = (cw - PX(8)) / 3;
	for (int i = 0; i < 3; i++)
		if (ui_button(ui_id("fmt", i), x + pad + i * (bw + PX(4)), cy, bw, PX(28), fn[order[i]], st.exp.fmt == order[i])) {
			st.exp.fmt = (ExportFormat)order[i];
			st.exp.vbr = st.exp.fmt == EXP_MP3 ? st.exp.vbr : false;
			if (st.exp_path[0]) {
				char *dot = strrchr(st.exp_path, '.');
				if (dot)
					snprintf(dot, sizeof st.exp_path - (size_t)(dot - st.exp_path), ".%s", export_ext(st.exp.fmt));
			}
		}
	cy += PX(40);
	if (st.exp.fmt != EXP_WAV) {
		ui_section(x + pad, cy, cw, "QUALITY");
		cy += PX(24);
		int kb = st.exp.bitrate_kbps ? st.exp.bitrate_kbps : st.exp.fmt == EXP_OPUS ? 160 : 192;
		int step = kb / 32;
		if (ui_spin(ui_id("kbps", 0), x + pad, cy, cw, PX(26), &step, 2, 10, "%d"))
			kb = step * 32;
		char lbl[64];
		snprintf(lbl, sizeof lbl, "%d kbps%s", kb, st.exp.fmt == EXP_MP3 && st.exp.vbr ? " (VBR, about)" : "");
		draw_rect(x + pad + PX(20), cy + PX(1), cw - PX(40), PX(24), COL(C_INPUT_BG));
		draw_text(FONT_UI, x + pad + (cw - text_w(FONT_UI, lbl)) / 2, cy + (PX(26) - line_h(FONT_UI)) / 2, lbl, strlen(lbl),
		          COL(C_INPUT_FG));
		st.exp.bitrate_kbps = kb;
		cy += PX(34);
		if (st.exp.fmt == EXP_MP3) {
			ui_toggle(ui_id("vbr", 0), x + pad, cy, "Variable bit rate", &st.exp.vbr);
			cy += PX(28);
		}
	}
	if (st.exp.fmt != EXP_OPUS) {
		bool r44 = st.exp.sample_rate == 44100;
		if (ui_toggle(ui_id("r44", 0), x + pad, cy, "44.1 kHz (else 48 kHz)", &r44))
			st.exp.sample_rate = r44 ? 44100 : 48000;
		cy += PX(28);
	}
	ui_section(x + pad, cy, cw, "FILE");
	cy += PX(24);
	if (!st.exp_path[0]) {
		snprintf(st.exp_path, sizeof st.exp_path, "%s.%s", st.name, export_ext(st.exp.fmt));
		char *d = strstr(st.exp_path, ".ft2.");
		if (d)
			memmove(d, d + 4, strlen(d + 4) + 1);
	}
	if (ui_textfield(ui_id("exp-path", 0), x + pad, cy, cw, PX(26), st.exp_path, sizeof st.exp_path))
		app_export();
	cy += PX(36);
	bool running = st.exp_state == 1;
	if (ui_button(ui_id("export", 0), x + pad, cy, cw, PX(34), running ? "Cancel" : "Export", !running)) {
		if (running)
			st.exp_cancel = true;
		else
			app_export();
	}
	cy += PX(44);
	if (running || st.exp_state >= 2) {
		float f = running ? st.exp_progress : 1.0f;
		draw_rect(x + pad, cy, cw, PX(6), COL(C_INPUT_BG));
		draw_rect(x + pad, cy, (int)(cw * f), PX(6), st.exp_state == 3 ? 0xE5534BFF : COL(C_FOCUS_BORDER));
		cy += PX(14);
		if (running)
			ui_label(FONT_SMALL, x + pad, cy, COL(C_FG_DIM), "%.0f %%", f * 100.0f);
		else if (st.exp_state == 2) {
			const ExportReport *r = &st.exp_report;
			ui_label(FONT_SMALL, x + pad, cy, COL(C_FG), "%d:%04.1f, %.2f MB", (int)r->seconds / 60, fmod(r->seconds, 60.0),
			         (double)r->bytes / 1e6);
			ui_label(FONT_SMALL, x + pad, cy + PX(18), COL(C_FG_DIM), "peak %.1f dBFS, RMS %.1f dBFS, %llu clipped",
			         r->peak_dbfs, r->rms_dbfs, (unsigned long long)r->clipped);
		} else {
			ui_label(FONT_SMALL, x + pad, cy, 0xE5534BFF, "%s", st.exp_err);
		}
	}
	(void)h;
}

static void side_settings(int x, int y, int w, int h)
{
	int pad = PX(14), cw = w - 2 * pad, cy = y;
	ui_section(x + pad, cy, cw, "PROJECT");
	cy += PX(24);
	if (!path_buf[0])
		snprintf(path_buf, sizeof path_buf, "%s", st.path[0] ? st.path : "song.ft2");
	ui_textfield(ui_id("path", 0), x + pad, cy, cw, PX(26), path_buf, sizeof path_buf);
	cy += PX(32);
	int bw = (cw - PX(8)) / 3;
	if (ui_button(ui_id("p-open", 0), x + pad, cy, bw, PX(26), "Open", false))
		app_open(path_buf);
	if (ui_button(ui_id("p-save", 0), x + pad + bw + PX(4), cy, bw, PX(26), "Save as", false))
		app_save(path_buf);
	if (ui_button(ui_id("p-new", 0), x + pad + 2 * (bw + PX(4)), cy, bw, PX(26), "New", false))
		run_command(CMD_NEW);
	cy += PX(40);
	ui_section(x + pad, cy, cw, "THEME");
	cy += PX(24);
	static const char *tn[3] = {"Dark", "Darker", "Light"};
	static const int tv[3] = {THEME_DARK, THEME_DARKER, THEME_LIGHT};
	for (int i = 0; i < 3; i++)
		if (ui_button(ui_id("theme", i), x + pad + i * (bw + PX(4)), cy, bw, PX(26), tn[i], app.theme == tv[i]))
			app.theme = tv[i];
	cy += PX(40);
	ui_section(x + pad, cy, cw, "PLAYBACK");
	cy += PX(24);
	ui_toggle(ui_id("follow", 0), x + pad, cy, "The view follows the playhead", &st.follow);
	cy += PX(26);
	Player *p = audio_player(st.audio);
	bool met = p->metronome;
	if (ui_toggle(ui_id("met", 0), x + pad, cy, "Metronome", &met))
		run_command(CMD_METRONOME);
	cy += PX(26);
	float inten = p->intensity_target;
	ui_label(FONT_SMALL, x + pad, cy + PX(2), COL(C_FG_DIM), "Intensity layers");
	if (ui_hslider(ui_id("intensity", 0), x + pad + PX(110), cy, cw - PX(110), PX(20), &inten, 0, 1, 0.6f)) {
		audio_lock(st.audio);
		player_set_intensity_now(p, inten);
		audio_unlock(st.audio);
	}
	cy += PX(34);
	ui_section(x + pad, cy, cw, "AUDIO");
	cy += PX(24);
	ui_label(FONT_SMALL, x + pad, cy, COL(C_FG), "%s", audio_device_name(st.audio));
	cy += PX(18);
	ui_label(FONT_SMALL, x + pad, cy, COL(C_FG_DIM), "%d Hz, float stereo", audio_rate(st.audio));
	(void)h;
}

void views_draw_side(int side, int x, int y, int w, int h)
{
	switch (side) {
	case SIDE_TRACKS:
		side_tracks(x, y, w, h);
		break;
	case SIDE_INSTRUMENT:
		side_instrument(x, y, w, h);
		break;
	case SIDE_CHORDS:
		side_chords(x, y, w, h);
		break;
	case SIDE_MIXER:
		side_mixer(x, y, w, h);
		break;
	case SIDE_GENERATE:
		side_generate(x, y, w, h);
		break;
	case SIDE_EXPORT:
		side_export(x, y, w, h);
		break;
	case SIDE_SETTINGS:
		side_settings(x, y, w, h);
		break;
	}
}

void views_draw_main(int x, int y, int w, int h) { arrange_draw(x, y, w, h); }
void views_draw_panel(int x, int y, int w, int h) { editor_draw(x, y, w, h); }
void views_draw_overlay(void) {}
