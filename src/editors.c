/* The arrangement (main area) and the selected track's editor (bottom
 * panel): tablature for guitars and basses, a piano roll for synths, a
 * step grid for drum kits. */
#include "views_int.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#define ROW_TICKS ((u32)ticks_per_row)
#define BEAT_TICKS ((u32)(ticks_per_row * rows_per_beat))
#define BAR_TICKS ((u32)ticks_per_bar)

static u32 section_color(SectionKind k)
{
	switch (k) {
	case SEC_INTRO:
	case SEC_OUTRO:
		return 0x6E7681FF;
	case SEC_VERSE:
		return 0x4C8DF6FF;
	case SEC_CHORUS:
	case SEC_DROP:
		return 0xE5534BFF;
	case SEC_BREAKDOWN:
	case SEC_BRIDGE:
		return 0x986EE2FF;
	case SEC_ARP_SOLO:
		return 0x39C5CFFF;
	case SEC_CHOIR:
		return 0xD65FA6FF;
	default:
		return 0x57AB5AFF;
	}
}

static u32 song_end(void)
{
	u32 t = song_total_ticks(&st.song);
	return t ? t : 8 * BAR_TICKS;
}

/* ---- the arrangement ------------------------------------------------------------ */

static int arr_x0, arr_w; /* lanes' x and width (for follow_playhead) */
static int pan_x0, pan_w;

void follow_playhead(void)
{
	if (!st.follow)
		return;
	u32 t = app_tick();
	double vis = arr_w / (double)st.px_per_tick;
	if (arr_w > 0 && (t < st.view_tick || t > st.view_tick + vis * 0.9))
		st.view_tick = t > vis * 0.1 ? t - vis * 0.1 : 0;
	double pvis = pan_w / (double)(st.panel_ppt > 0 ? st.panel_ppt : 4);
	if (pan_w > 0 && (t < st.panel_tick || t > st.panel_tick + pvis * 0.85))
		st.panel_tick = t > pvis * 0.1 ? floor((t - pvis * 0.1) / BEAT_TICKS) * BEAT_TICKS : 0;
}

static void draw_event_mark(const NoteEvent *e, int x, int y, int w, int h, int lo, int hi, u32 col, bool sel, float ppt)
{
	int ex = x + (int)(e->tick * ppt);
	int ew = (int)(e->len * ppt);
	if (ew < PX(2))
		ew = PX(2);
	const Patch *p = &st.song.patches[e->ch];
	if (p->inst == INST_KIT) {
		/* drums: a row per family */
		int n = e->notes[0];
		int row = n == DR_KICK ? 4 : n == DR_SNARE || n == DR_CLAP || n == DR_RIM ? 3
		                       : n == DR_HAT_CLOSED || n == DR_HAT_OPEN || n == DR_SHAKER ? 1
		                       : n == DR_CRASH ? 0 : 2;
		draw_rect(ex, y + PX(3) + row * (h - PX(6)) / 5, PX(2), (h - PX(6)) / 5 - PX(1), sel ? COL(C_FG) : col);
		return;
	}
	if (e->fx == FX_STRUM) {
		/* a strum: a vertical tick spanning its strings' pitches */
		int a = 127, b = 0;
		for (int k = 0; k < 6; k++)
			if (e->notes[k])
				a = e->notes[k] < a ? e->notes[k] : a, b = e->notes[k] > b ? e->notes[k] : b;
		if (b < a)
			return;
		int ya = y + h - PX(3) - (int)((float)(b - lo) / (float)(hi - lo) * (h - PX(6)));
		int yb = y + h - PX(3) - (int)((float)(a - lo) / (float)(hi - lo) * (h - PX(6)));
		draw_rect(ex, ya, PX(2), yb - ya + PX(2), sel ? COL(C_FG) : col);
		draw_rect(ex, ya + (e->strum < 0 ? 0 : yb - ya), ew, PX(1), with_alpha(col, 0x60));
		return;
	}
	int cnt = e->fx == FX_SLIDE_UP || e->art == ART_SLIDE || e->art == ART_BEND ? 1 : e->count;
	for (int k = 0; k < cnt && k < 6; k++) {
		int ny = y + h - PX(4) - (int)((float)(e->notes[k] - lo) / (float)(hi - lo) * (h - PX(8)));
		draw_rect(ex, ny, ew, PX(3), sel ? COL(C_FG) : col);
	}
}

void arrange_draw(int x, int y, int w, int h)
{
	const int head_w = PX(130), ruler_h = PX(34), lane_h = PX(52);
	Song *s = &st.song;
	Player *pl = audio_player(st.audio);
	int lx = x + head_w, lw = w - head_w;
	arr_x0 = lx;
	arr_w = lw;
	float ppt = st.px_per_tick;
	u32 end = song_end();

	/* wheel: scroll in time; Ctrl zooms around the mouse; Shift scrolls the lanes */
	if (ui_hit(x, y, w, h) && app.wheel != 0) {
		if (app.mods & MOD_CTRL) {
			double at = st.view_tick + (app.mx - lx) / (double)ppt;
			ppt *= app.wheel > 0 ? 1.25f : 0.8f;
			ppt = ppt < 0.05f ? 0.05f : ppt > 8.0f ? 8.0f : ppt;
			st.px_per_tick = ppt;
			st.view_tick = at - (app.mx - lx) / (double)ppt;
		} else if (app.mods & MOD_SHIFT) {
			st.lane_scroll -= app.wheel > 0 ? 1 : -1;
		} else {
			st.view_tick -= app.wheel * 120.0 / ppt;
			st.follow = st.follow && !st.playing;
		}
		app.wheel = 0;
	}
	if (st.view_tick < 0)
		st.view_tick = 0;
	int max_scroll = s->ntracks - (h - ruler_h) / lane_h;
	if (st.lane_scroll > max_scroll)
		st.lane_scroll = max_scroll;
	if (st.lane_scroll < 0)
		st.lane_scroll = 0;
	double t0 = st.view_tick;
#define TX(t) (lx + (int)(((double)(t) - t0) * ppt))

	/* the ruler: bars, sections, the loop */
	draw_rect(x, y, w, ruler_h, COL(C_TABS_BG));
	draw_rect(x, y + ruler_h - PX(1), w, PX(1), COL(C_TABS_BORDER));
	draw_clip(lx, y, lw, h);
	for (u32 i = 0; i < s->nsections; i++) {
		const Section *sec = &s->sections[i];
		int sx = TX(sec->start_bar * BAR_TICKS), ex = TX((sec->start_bar + sec->bars) * BAR_TICKS);
		u32 c = section_color(sec->kind);
		draw_rect(sx, y + PX(2), ex - sx - PX(1), PX(14), with_alpha(c, 0x50));
		draw_rect(sx, y + PX(2), PX(2), PX(14), c);
		const char *n = section_name(sec->kind);
		if (ex - sx > text_w(FONT_SMALL, n) + PX(8))
			draw_text(FONT_SMALL, sx + PX(5), y + PX(2), n, strlen(n), COL(C_FG));
	}
	int bar_step = ppt * BAR_TICKS < PX(28) ? (ppt * BAR_TICKS < PX(10) ? 8 : 4) : 1;
	for (u32 b = (u32)(t0 / BAR_TICKS); ; b++) {
		int bx = TX(b * BAR_TICKS);
		if (bx > lx + lw)
			break;
		if (b % (u32)bar_step == 0) {
			draw_rect(bx, y + PX(18), PX(1), ruler_h - PX(18), COL(C_FG_DIM));
			ui_label(FONT_SMALL, bx + PX(3), y + PX(17), COL(C_FG_DIM), "%u", b + 1);
		}
	}
	if (pl->loop && pl->loop_end > pl->loop_start) {
		int a = TX(pl->loop_start), b = TX(pl->loop_end);
		draw_rect(a, y + ruler_h - PX(5), b - a, PX(4), COL(C_FOCUS_BORDER));
	}
	draw_noclip();
	ui_label(FONT_SMALL, x + PX(10), y + PX(10), COL(C_FG_DIM), "%u bars", s->total_bars);

	/* ruler clicks: seek; Shift+drag: the loop range */
	static bool loop_drag;
	static u32 loop_a;
	u32 id_ruler = ui_id("ruler", 0);
	if (ui_hit(lx, y, lw, ruler_h) && app.pressed[0]) {
		u32 t = (u32)fmax(0.0, t0 + (app.mx - lx) / (double)ppt);
		t -= t % BEAT_TICKS;
		app.active = id_ruler;
		if (app.mods & MOD_SHIFT) {
			loop_drag = true;
			loop_a = t - t % BAR_TICKS;
		} else {
			loop_drag = false;
			app_seek(t);
		}
	}
	if (app.active == id_ruler) {
		u32 t = (u32)fmax(0.0, t0 + (app.mx - lx) / (double)ppt);
		if (loop_drag) {
			u32 b = (t + BAR_TICKS - 1) / BAR_TICKS * BAR_TICKS;
			audio_lock(st.audio);
			pl->loop_start = loop_a < b ? loop_a : b;
			pl->loop_end = loop_a < b ? b : loop_a + BAR_TICKS;
			pl->loop = true;
			audio_unlock(st.audio);
		} else if (app.down[0]) {
			app_seek(t - t % BEAT_TICKS);
		}
	}

	/* lanes */
	int ly = y + ruler_h;
	for (int t = st.lane_scroll; t < s->ntracks && ly < y + h; t++, ly += lane_h) {
		bool sel = t == st.track;
		u32 col = track_color(t);
		draw_rect(x, ly, head_w, lane_h, sel ? COL(C_LIST_ACTIVE_BG) : COL(C_SIDEBAR_BG));
		draw_rect(x, ly, PX(4), lane_h, col);
		draw_text_fit(FONT_UI_BOLD, x + PX(10), ly + PX(6), s->track_name[t], strlen(s->track_name[t]), head_w - PX(16),
		              sel ? COL(C_LIST_ACTIVE_FG) : COL(C_FG));
		const char *kind = track_kind(t);
		draw_text_fit(FONT_SMALL, x + PX(10), ly + PX(8) + line_h(FONT_UI_BOLD), kind, strlen(kind), head_w - PX(16),
		              COL(C_DESCRIPTION_FG));
		bool muted = pl->solo ? !((pl->solo >> t) & 1u) : (pl->mute >> t) & 1u;
		if (muted)
			ui_label(FONT_SMALL_BOLD, x + head_w - PX(20), ly + PX(6), COL(C_WARNING), "M");
		draw_rect(lx, ly, lw, lane_h, sel ? with_alpha(col, 0x14) : COL(C_EDITOR_BG));
		draw_rect(x, ly + lane_h - PX(1), w, PX(1), COL(C_PANEL_BORDER));
		draw_clip(lx, ly, lw, lane_h);
		/* bar lines */
		for (u32 b = (u32)(t0 / BAR_TICKS);; b++) {
			int bx = TX(b * BAR_TICKS);
			if (bx > lx + lw)
				break;
			draw_rect(bx, ly, PX(1), lane_h, b % 4 ? with_alpha(COL(C_PANEL_BORDER), 0x80) : COL(C_PANEL_BORDER));
		}
		/* the song's end */
		int ex = TX(end);
		if (ex < lx + lw)
			draw_rect(ex, ly, lx + lw - ex, lane_h, with_alpha(0x00000000, 0x30));
		/* pitch range of the track's events */
		int lo = 127, hi = 0;
		for (u32 i = 0; i < s->nevents; i++) {
			const NoteEvent *e = &s->events[i];
			if (e->ch != t)
				continue;
			for (int k = 0; k < (e->fx == FX_STRUM ? 6 : e->count) && k < 6; k++)
				if (e->notes[k])
					lo = e->notes[k] < lo ? e->notes[k] : lo, hi = e->notes[k] > hi ? e->notes[k] : hi;
		}
		if (hi <= lo)
			lo -= 6, hi += 6;
		double vis_end = t0 + lw / (double)ppt;
		for (u32 i = 0; i < s->nevents; i++) {
			const NoteEvent *e = &s->events[i];
			if (e->ch != t || e->tick + e->len < t0)
				continue;
			if (e->tick > vis_end)
				break;
			NoteEvent shifted = *e;
			draw_event_mark(&shifted, lx - (int)(t0 * ppt), ly, lw, lane_h, lo, hi, col, (int)i == st.sel, ppt);
		}
		draw_noclip();
		/* click: select the track, place the cursor */
		if (ui_hit(x, ly, w, lane_h) && app.pressed[0]) {
			st.track = t;
			if (app.mx >= lx) {
				u32 tk = (u32)fmax(0.0, t0 + (app.mx - lx) / (double)ppt);
				st.cursor = tk - tk % BEAT_TICKS;
				if (!st.playing)
					app_seek(st.cursor);
				st.panel_tick = st.cursor > 2 * BEAT_TICKS ? st.cursor - 2 * BEAT_TICKS : 0;
			}
			st.sel = -1;
		}
	}
	if (s->ntracks == 0)
		ui_label(FONT_UI, lx + PX(20), y + ruler_h + PX(20), COL(C_FG_DIM), "No tracks: Track > Add Guitar");

	/* cursor and playhead */
	draw_clip(lx, y, lw, h);
	int cx = TX(st.cursor);
	draw_rect(cx, y + ruler_h, PX(1), h - ruler_h, with_alpha(COL(C_FOCUS_BORDER), 0x90));
	int px = TX(app_tick());
	draw_rect(px, y, PX(2), h, COL(C_FG));
	draw_rect(px - PX(5), y + ruler_h - PX(8), PX(12), PX(8), COL(C_FG));
	draw_noclip();
#undef TX
}

/* ---- the editors --------------------------------------------------------------------- */

static int hit_event(int mx, int my, int x, int y, int w, int h, double t0, float ppt, int (*row_of)(const NoteEvent *, int, int),
                     int rows)
{
	int best = -1, bd = PX(8);
	for (u32 i = 0; i < st.song.nevents; i++) {
		const NoteEvent *e = &st.song.events[i];
		if (e->ch != st.track)
			continue;
		int ex = x + (int)((e->tick - t0) * ppt);
		if (ex > x + w + PX(20))
			break;
		int ew = (int)(e->len * ppt);
		int dx = mx < ex ? ex - mx : mx > ex + (ew > PX(8) ? ew : PX(8)) ? mx - ex - (ew > PX(8) ? ew : PX(8)) : 0;
		if (dx > bd)
			continue;
		if (row_of) {
			int r = row_of(e, rows, my);
			if (r < 0)
				continue;
		}
		best = (int)i;
		bd = dx;
		if (dx == 0)
			break;
	}
	(void)y;
	(void)h;
	return best;
}

static const char *art_abbrev(int a)
{
	static const char *ab[ART_COUNT] = {"", "PM", "let", "st", "<>", "x", "/", "b", "h", "~", "fb"};
	return a >= 0 && a < ART_COUNT ? ab[a] : "";
}

/* the tab: a string per row, fret numbers at the notes */
static void tab_editor(int x, int y, int w, int h)
{
	Song *s = &st.song;
	Patch *p = &s->patches[st.track];
	int open[6];
	int strings = gtr_open_strings(p, open);
	const int key_w = PX(44);
	int tx = x + key_w, tw = w - key_w;
	pan_x0 = tx;
	pan_w = tw;
	float ppt = st.panel_ppt;
	double t0 = st.panel_tick;
	int top = y + PX(30), sh = (h - PX(44)) / (strings + 1);
	if (sh > PX(24))
		sh = PX(24);
	/* string names */
	for (int k = 0; k < strings; k++) {
		int sy = top + (strings - 1 - k) * sh + sh / 2;
		ui_label(FONT_MONO, x + PX(10), sy - line_h(FONT_MONO) / 2, COL(C_FG_DIM), "%s", note_names[open[k] % 12]);
		draw_rect(tx, sy, tw, PX(1), COL(C_FG_DIM));
	}
	draw_clip(tx, y, tw, h);
	/* beats and bars */
	for (u32 b = (u32)(t0 / BEAT_TICKS);; b++) {
		int bx = tx + (int)((b * BEAT_TICKS - t0) * ppt);
		if (bx > tx + tw)
			break;
		bool bar = b % 4 == 0;
		draw_rect(bx, top, PX(1), strings * sh, bar ? COL(C_FG) : with_alpha(COL(C_PANEL_BORDER), 0xA0));
		if (bar)
			ui_label(FONT_SMALL, bx + PX(3), y + PX(4), COL(C_FG_DIM), "%u", b / 4 + 1);
	}
	/* the hand's position for single notes, from the start */
	float hand = 3.0f;
	int last_root = -1, last_q = -1;
	double vis_end = t0 + tw / (double)ppt;
	for (u32 i = 0; i < s->nevents; i++) {
		const NoteEvent *e = &s->events[i];
		if (e->ch != st.track)
			continue;
		if (e->tick > vis_end)
			break;
		int ex = tx + (int)((e->tick - t0) * ppt);
		bool vis = e->tick + e->len >= t0;
		bool sel = (int)i == st.sel;
		u32 col = sel ? COL(C_FOCUS_BORDER) : COL(C_FG);
		if (e->fx == FX_STRUM) {
			u16 pcs = 0;
			int bass = -1;
			for (int k = 0; k < strings; k++) {
				if (!e->notes[k])
					continue;
				pcs |= (u16)(1u << (e->notes[k] % 12));
				if (bass < 0)
					bass = e->notes[k] % 12;
				int fret = e->notes[k] - open[k];
				if (fret > 0)
					hand = (float)fret;
				if (!vis)
					continue;
				int sy = top + (strings - 1 - k) * sh + sh / 2;
				char fs[8];
				snprintf(fs, sizeof fs, e->art == ART_DEAD ? "x" : "%d", fret);
				int fw = text_w(FONT_MONO_BOLD, fs);
				draw_rect(ex - PX(1), sy - line_h(FONT_MONO_BOLD) / 2, fw + PX(2), line_h(FONT_MONO_BOLD), COL(C_PANEL_BG));
				draw_text(FONT_MONO_BOLD, ex, sy - line_h(FONT_MONO_BOLD) / 2, fs, strlen(fs), col);
			}
			if (!vis)
				continue;
			/* the stroke and the articulation over the staff; the chord when it changes */
			int ay = top + strings * sh + PX(2);
			ui_label(FONT_SMALL, ex, ay, e->strum < 0 ? COL(C_GIT_MODIFIED) : COL(C_FG_DIM), "%s%s",
			         e->strum < 0 ? "\xe2\x86\x91" : "\xe2\x86\x93", art_abbrev(e->art));
			int r, q;
			if (pcs && chord_identify(pcs, bass, &r, &q) && (r != last_root || q != last_q)) {
				char cn[16];
				chord_name(r, q, cn, sizeof cn);
				ui_label(FONT_SMALL_BOLD, ex, top - PX(16), track_color(st.track), "%s", cn);
				last_root = r, last_q = q;
			}
			if (sel)
				draw_frame(ex - PX(3), top - PX(2), PX(4) + text_w(FONT_MONO_BOLD, "00"), strings * sh + PX(4), col);
		} else {
			int cnt = e->art == ART_SLIDE || e->art == ART_BEND || e->fx == FX_SLIDE_UP ? 1 : e->count;
			for (int k = 0; k < cnt && k < 6; k++) {
				int fret = 0, str = e->string && k == 0 ? e->string - 1 : gtr_pick_string(p, e->notes[k], hand, &fret);
				if (e->string && k == 0)
					fret = e->notes[k] - open[str];
				if (str < 0)
					continue;
				hand = hand * 0.6f + (float)(fret > 0 ? fret : hand) * 0.4f;
				if (!vis)
					continue;
				int sy = top + (strings - 1 - str) * sh + sh / 2;
				char fs[16];
				if (e->art == ART_BEND || e->art == ART_SLIDE)
					snprintf(fs, sizeof fs, "%d%s%d", fret, e->art == ART_BEND ? "b" : "/", fret + e->notes[1] - e->notes[0]);
				else
					snprintf(fs, sizeof fs, "%d%s", fret, e->art == ART_RING ? "" : art_abbrev(e->art));
				int fw = text_w(FONT_MONO_BOLD, fs);
				int ew = (int)(e->len * ppt);
				draw_rect(ex, sy + PX(6), ew > PX(4) ? ew - PX(2) : PX(2), PX(2), with_alpha(track_color(st.track), 0x80));
				draw_rect(ex - PX(1), sy - line_h(FONT_MONO_BOLD) / 2, fw + PX(2), line_h(FONT_MONO_BOLD), COL(C_PANEL_BG));
				draw_text(FONT_MONO_BOLD, ex, sy - line_h(FONT_MONO_BOLD) / 2, fs, strlen(fs), col);
				if (sel)
					draw_frame(ex - PX(3), sy - line_h(FONT_MONO_BOLD) / 2 - PX(2), fw + PX(6), line_h(FONT_MONO_BOLD) + PX(4), col);
			}
		}
	}
	/* cursor and playhead */
	int cx = tx + (int)((st.cursor - t0) * ppt);
	draw_rect(cx, top - PX(4), PX(2), strings * sh + PX(8), with_alpha(COL(C_FOCUS_BORDER), 0xC0));
	int px = tx + (int)((app_tick() - t0) * ppt);
	draw_rect(px, y, PX(2), h, COL(C_FG));
	draw_noclip();

	/* clicks: select an event, or place the cursor; double click inserts the
	 * palette's chord; right click flips a strum's direction */
	if (ui_hit(tx, y, tw, h) && (app.pressed[0] || app.pressed[2])) {
		int hit = hit_event(app.mx, app.my, tx, y, tw, h, t0, ppt, NULL, 0);
		u32 tk = (u32)fmax(0.0, t0 + (app.mx - tx) / (double)ppt);
		tk -= tk % ROW_TICKS;
		if (app.pressed[2] && hit >= 0 && st.song.events[hit].fx == FX_STRUM) {
			app_edit_begin();
			st.song.events[hit].strum = (i8)-st.song.events[hit].strum;
			app_edit_end("flip strum");
			st.sel = hit;
		} else if (app.pressed[0] && app.clicks >= 2) {
			st.cursor = tk;
			app_insert_chord(tk);
		} else if (app.pressed[0]) {
			st.sel = hit;
			st.cursor = hit >= 0 ? st.song.events[hit].tick : tk;
			if (hit >= 0) {
				const NoteEvent *e = &st.song.events[hit];
				NoteEvent pv = *e;
				audio_lock(st.audio);
				player_preview(audio_player(st.audio), &pv);
				audio_unlock(st.audio);
			}
		}
	}
}

/* the piano roll */
static int roll_lo(void) { return st.piano_lo; }

static void roll_editor(int x, int y, int w, int h)
{
	Song *s = &st.song;
	const int key_w = PX(44), nh = PX(9);
	int tx = x + key_w, tw = w - key_w;
	pan_x0 = tx;
	pan_w = tw;
	float ppt = st.panel_ppt;
	double t0 = st.panel_tick;
	int top = y + PX(22), rows = (h - PX(26)) / nh;
	int lo = roll_lo(), hi = lo + rows;
	/* keys */
	for (int n = lo; n < hi; n++) {
		int ny = top + (hi - 1 - n) * nh;
		bool black = (0x54A >> (n % 12)) & 1;
		draw_rect(x, ny, key_w - PX(2), nh - PX(1), black ? 0x202020FF : 0xD8D8D8FF);
		if (n % 12 == 0)
			ui_label(FONT_SMALL, x + PX(4), ny - PX(3), 0x404040FF, "C%d", n / 12 - 1);
		draw_rect(tx, ny, tw, nh, black ? with_alpha(0x00000000, 0x28) : 0x00000000);
		if (ui_hit(x, ny, key_w, nh) && app.pressed[0])
			app_preview_note(st.track, n);
	}
	draw_clip(tx, y, tw, h);
	for (u32 b = (u32)(t0 / BEAT_TICKS);; b++) {
		int bx = tx + (int)((b * BEAT_TICKS - t0) * ppt);
		if (bx > tx + tw)
			break;
		draw_rect(bx, top, PX(1), rows * nh, b % 4 ? with_alpha(COL(C_PANEL_BORDER), 0xA0) : COL(C_FG_DIM));
		if (b % 4 == 0)
			ui_label(FONT_SMALL, bx + PX(3), y + PX(3), COL(C_FG_DIM), "%u", b / 4 + 1);
	}
	double vis_end = t0 + tw / (double)ppt;
	u32 col = track_color(st.track);
	for (u32 i = 0; i < s->nevents; i++) {
		const NoteEvent *e = &s->events[i];
		if (e->ch != st.track || e->tick + e->len < t0)
			continue;
		if (e->tick > vis_end)
			break;
		int ex = tx + (int)((e->tick - t0) * ppt), ew = (int)(e->len * ppt);
		int cnt = e->fx == FX_SLIDE_UP ? 1 : e->count;
		for (int k = 0; k < cnt && k < 6; k++) {
			int n = e->notes[k];
			if (n < lo || n >= hi)
				continue;
			int ny = top + (hi - 1 - n) * nh;
			draw_rect(ex, ny + PX(1), ew > PX(3) ? ew - PX(1) : PX(2), nh - PX(2), (int)i == st.sel ? COL(C_FG) : col);
		}
	}
	int cx = tx + (int)((st.cursor - t0) * ppt);
	draw_rect(cx, top, PX(1), rows * nh, with_alpha(COL(C_FOCUS_BORDER), 0xC0));
	int px = tx + (int)((app_tick() - t0) * ppt);
	draw_rect(px, y, PX(2), h, COL(C_FG));
	draw_noclip();
	/* wheel: pitch; Ctrl+wheel is the zoom (editor_draw) */
	if (ui_hit(x, y, w, h) && app.wheel != 0 && !(app.mods & MOD_CTRL)) {
		st.piano_lo += app.wheel > 0 ? 2 : -2;
		st.piano_lo = st.piano_lo < 12 ? 12 : st.piano_lo > 100 ? 100 : st.piano_lo;
		app.wheel = 0;
	}
	/* click selects, double click adds a note a beat long, drag moves the selection */
	static u32 drag_id;
	static int drag_dn;
	static u32 drag_t0;
	u32 id = ui_id("roll", 0);
	if (ui_hit(tx, top, tw, rows * nh) && app.pressed[0]) {
		u32 tk = (u32)fmax(0.0, t0 + (app.mx - tx) / (double)ppt);
		tk -= tk % ROW_TICKS;
		int n = hi - 1 - (app.my - top) / nh;
		int hit = -1;
		for (u32 i = 0; i < s->nevents; i++) {
			const NoteEvent *e = &s->events[i];
			if (e->ch != st.track || tk + ROW_TICKS <= e->tick || tk >= e->tick + e->len)
				continue;
			for (int k = 0; k < e->count && k < 6; k++)
				if (e->notes[k] == n)
					hit = (int)i;
		}
		if (app.clicks >= 2 && hit < 0) {
			app_insert_note(st.track, tk, BEAT_TICKS, n);
			app_preview_note(st.track, n);
		} else {
			st.sel = hit;
			st.cursor = tk;
			if (hit >= 0) {
				app.active = id;
				drag_id = id;
				drag_dn = n;
				drag_t0 = tk;
				app_preview_note(st.track, n);
			}
		}
	}
	if (app.active == id && drag_id == id && st.sel >= 0 && !app.down[0]) {
		u32 tk = (u32)fmax(0.0, t0 + (app.mx - tx) / (double)ppt);
		tk -= tk % ROW_TICKS;
		int n = hi - 1 - (app.my - top) / nh;
		int dn = n - drag_dn;
		i64 dt = (i64)tk - (i64)drag_t0;
		if (dn || dt) {
			app_edit_begin();
			NoteEvent *e = &st.song.events[st.sel];
			i64 nt = (i64)e->tick + dt;
			e->tick = (u32)(nt < 0 ? 0 : nt);
			for (int k = 0; k < 6; k++)
				if (e->notes[k] && (k < e->count || e->fx == FX_STRUM))
					e->notes[k] = (u8)FT_CLAMP(e->notes[k] + dn, 1, 127);
			NoteEvent moved = *e;
			app_edit_end("move note");
			for (u32 i = 0; i < st.song.nevents; i++)
				if (!memcmp(&st.song.events[i], &moved, sizeof moved))
					st.sel = (int)i;
		}
		drag_id = 0;
	}
}

/* the drum grid: a row per drum, a cell per 16th */
static const struct {
	u8 note;
	const char *name;
} kit_rows[] = {{DR_CRASH, "Crash"},   {DR_HAT_OPEN, "Open hat"}, {DR_HAT_CLOSED, "Hat"}, {DR_SHAKER, "Shaker"},
                {DR_TOM_HIGH, "Tom hi"}, {DR_TOM_MID, "Tom mid"}, {DR_TOM_LOW, "Tom lo"},  {DR_RIM, "Rim"},
                {DR_CLAP, "Clap"},     {DR_SNARE, "Snare"},      {DR_KICK, "Kick"}};

static void kit_editor(int x, int y, int w, int h)
{
	Song *s = &st.song;
	const int key_w = PX(70);
	int nrows = (int)(sizeof kit_rows / sizeof kit_rows[0]);
	int tx = x + key_w, tw = w - key_w;
	pan_x0 = tx;
	pan_w = tw;
	float ppt = st.panel_ppt;
	double t0 = st.panel_tick;
	int top = y + PX(22), rh = (h - PX(26)) / nrows;
	if (rh > PX(20))
		rh = PX(20);
	for (int r = 0; r < nrows; r++) {
		int ry = top + r * rh;
		ui_label(FONT_SMALL, x + PX(8), ry + (rh - line_h(FONT_SMALL)) / 2, COL(C_FG_DIM), "%s", kit_rows[r].name);
		if (ui_hit(x, ry, key_w, rh) && app.pressed[0])
			app_preview_note(st.track, kit_rows[r].note);
	}
	draw_clip(tx, y, tw, h);
	int cell = (int)(ROW_TICKS * ppt);
	for (u32 k = (u32)(t0 / ROW_TICKS);; k++) {
		int cx = tx + (int)((k * ROW_TICKS - t0) * ppt);
		if (cx > tx + tw)
			break;
		u32 c = k % 16 == 0 ? COL(C_FG_DIM) : k % 4 == 0 ? COL(C_PANEL_BORDER) : with_alpha(COL(C_PANEL_BORDER), 0x60);
		draw_rect(cx, top, PX(1), nrows * rh, c);
		if (k % 16 == 0)
			ui_label(FONT_SMALL, cx + PX(3), y + PX(3), COL(C_FG_DIM), "%u", k / 16 + 1);
	}
	double vis_end = t0 + tw / (double)ppt;
	for (u32 i = 0; i < s->nevents; i++) {
		const NoteEvent *e = &s->events[i];
		if (e->ch != st.track || e->tick + ROW_TICKS < t0)
			continue;
		if (e->tick > vis_end)
			break;
		for (int r = 0; r < nrows; r++)
			for (int k = 0; k < e->count && k < 6; k++)
				if (e->notes[k] == kit_rows[r].note) {
					int cx = tx + (int)((e->tick - t0) * ppt);
					u32 c = with_alpha(track_color(st.track), (u32)(80 + e->vel * 175 / 127));
					draw_rect(cx + PX(1), top + r * rh + PX(2), cell - PX(2), rh - PX(3), c);
				}
	}
	int px = tx + (int)((app_tick() - t0) * ppt);
	draw_rect(px, y, PX(2), h, COL(C_FG));
	draw_noclip();
	/* a click toggles the hit */
	if (ui_hit(tx, top, tw, nrows * rh) && app.pressed[0]) {
		u32 tk = (u32)fmax(0.0, t0 + (app.mx - tx) / (double)ppt);
		tk -= tk % ROW_TICKS;
		int r = (app.my - top) / rh;
		if (r >= 0 && r < nrows) {
			int found = -1;
			for (u32 i = 0; i < s->nevents; i++) {
				const NoteEvent *e = &s->events[i];
				if (e->ch == st.track && e->tick == tk && e->count == 1 && e->notes[0] == kit_rows[r].note)
					found = (int)i;
			}
			if (found >= 0) {
				app_delete_event(found);
			} else {
				NoteEvent e = note_event();
				e.ch = (u8)st.track;
				e.tick = tk;
				e.len = (u16)ROW_TICKS;
				e.vel = (app.mods & MOD_SHIFT) ? 70 : 105;
				e.notes[0] = kit_rows[r].note;
				app_edit_begin();
				song_add(s, e);
				u32 eb = (tk + BAR_TICKS) / BAR_TICKS;
				if (eb > s->total_bars)
					s->total_bars = eb;
				app_edit_end("drum hit");
				app_preview_note(st.track, kit_rows[r].note);
			}
		}
	}
}

void editor_draw(int x, int y, int w, int h)
{
	if (st.panel_ppt <= 0)
		st.panel_ppt = 4.0f;
	if (st.track < 0 || st.track >= st.song.ntracks)
		return;
	const Patch *p = &st.song.patches[st.track];
	/* a newly selected synth: the piano roll centres on its notes */
	static int roll_for = -1;
	if (roll_for != st.track && p->inst == INST_SYNTH) {
		roll_for = st.track;
		double sum = 0;
		u32 n = 0;
		for (u32 i = 0; i < st.song.nevents; i++)
			if (st.song.events[i].ch == st.track)
				sum += st.song.events[i].notes[0], n++;
		int rows = (h - PX(52)) / PX(9);
		st.piano_lo = n ? (int)(sum / n) - rows / 2 : 48;
		st.piano_lo = st.piano_lo < 12 ? 12 : st.piano_lo > 100 ? 100 : st.piano_lo;
	}
	/* the header: what this is, and how to use it */
	int hh = PX(26);
	draw_rect(x, y, w, hh, COL(C_TABS_BG));
	draw_rect(x, y + hh - PX(1), w, PX(1), COL(C_PANEL_BORDER));
	const char *kind = p->inst == INST_GUITAR ? "TAB" : p->inst == INST_KIT ? "DRUM GRID" : "PIANO ROLL";
	ui_label(FONT_SMALL_BOLD, x + PX(12), y + (hh - line_h(FONT_SMALL_BOLD)) / 2, COL(C_PANEL_TITLE_ACTIVE_FG), "%s", kind);
	draw_rect(x + PX(12), y + hh - PX(2), text_w(FONT_SMALL_BOLD, kind), PX(1), COL(C_PANEL_TITLE_ACTIVE_BORDER));
	ui_label(FONT_SMALL, x + PX(24) + text_w(FONT_SMALL_BOLD, kind), y + (hh - line_h(FONT_SMALL)) / 2, track_color(st.track),
	         "%s", st.song.track_name[st.track]);
	const char *help = p->inst == INST_GUITAR
	                       ? "double-click: insert the palette chord  right-click: flip strum  Del: delete  Enter: insert at cursor"
	                   : p->inst == INST_KIT ? "click: toggle a hit (Shift: soft)"
	                                         : "double-click: add a note  drag: move  Del: delete  keys: preview";
	int hw = text_w(FONT_SMALL, help);
	draw_text(FONT_SMALL, x + w - hw - PX(12), y + (hh - line_h(FONT_SMALL)) / 2, help, strlen(help), COL(C_FG_DIM));
	int by = y + hh, bh = h - hh;
	/* Ctrl+wheel zooms, wheel scrolls (except the piano roll's pitch) */
	if (ui_hit(x, by, w, bh) && app.wheel != 0) {
		if (app.mods & MOD_CTRL) {
			st.panel_ppt *= app.wheel > 0 ? 1.25f : 0.8f;
			st.panel_ppt = st.panel_ppt < 0.5f ? 0.5f : st.panel_ppt > 16 ? 16 : st.panel_ppt;
			app.wheel = 0;
		} else if (p->inst != INST_SYNTH || (app.mods & MOD_SHIFT)) {
			st.panel_tick -= app.wheel * 60.0 / st.panel_ppt;
			if (st.panel_tick < 0)
				st.panel_tick = 0;
			app.wheel = 0;
		}
	}
	draw_clip(x, by, w, bh);
	if (p->inst == INST_GUITAR)
		tab_editor(x, by, w, bh);
	else if (p->inst == INST_KIT)
		kit_editor(x, by, w, bh);
	else
		roll_editor(x, by, w, bh);
	draw_noclip();
}

bool editor_key(const Event *e)
{
	if (e->mods & (MOD_CTRL | MOD_ALT))
		return false;
	switch (e->key) {
	case SDLK_DELETE:
	case SDLK_BACKSPACE:
		if (st.sel >= 0) {
			app_delete_event(st.sel);
			return true;
		}
		return false;
	case SDLK_LEFT:
	case SDLK_RIGHT: {
		u32 step = (e->mods & MOD_SHIFT) ? BAR_TICKS : ROW_TICKS;
		if (e->key == SDLK_LEFT)
			st.cursor = st.cursor >= step ? st.cursor - step : 0;
		else
			st.cursor += step;
		st.sel = -1;
		if (!st.playing)
			app_seek(st.cursor);
		double pvis = pan_w / (double)st.panel_ppt;
		if (st.cursor < st.panel_tick || st.cursor > st.panel_tick + pvis * 0.9)
			st.panel_tick = st.cursor > pvis * 0.3 ? st.cursor - pvis * 0.3 : 0;
		return true;
	}
	case SDLK_UP:
	case SDLK_DOWN:
		st.track += e->key == SDLK_UP ? -1 : 1;
		st.track = st.track < 0 ? 0 : st.track >= st.song.ntracks ? st.song.ntracks - 1 : st.track;
		st.sel = -1;
		return true;
	case SDLK_RETURN:
		app_insert_chord(st.cursor);
		return true;
	default:
		return false;
	}
}
