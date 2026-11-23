/* The workbench shell (ui.h): menu bar, transport bar, activity bar, side
 * view, main area, bottom panel, status bar, and the immediate-mode
 * widgets every view draws with. */
#include "ui.h"
#include "gpu.h"
#include "views.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <SDL3/SDL.h>

App app;

/* ---- commands --------------------------------------------------------- */

static const struct {
	const char *keys;
} cmd_info[CMD_COUNT] = {
    [CMD_NEW] = {"Ctrl+N"},       [CMD_OPEN] = {"Ctrl+O"},       [CMD_SAVE] = {"Ctrl+S"},
    [CMD_SAVE_AS] = {"Ctrl+Shift+S"}, [CMD_QUIT] = {"Ctrl+Q"},   [CMD_UNDO] = {"Ctrl+Z"},
    [CMD_REDO] = {"Ctrl+Y"},      [CMD_PLAY] = {"Space"},        [CMD_TO_START] = {"Home"},
    [CMD_LOOP] = {"L"},           [CMD_METRONOME] = {"K"},       [CMD_GENERATE] = {"Ctrl+G"},
    [CMD_EXPORT_MP3] = {"Ctrl+E"}, [CMD_ZOOM_IN] = {"Ctrl+="},   [CMD_ZOOM_OUT] = {"Ctrl+-"},
    [CMD_DELETE_TRACK] = {"Ctrl+Del"},
};

const char *command_keys(int cmd) { return cmd > 0 && cmd < CMD_COUNT ? cmd_info[cmd].keys : NULL; }

void run_command(int cmd)
{
	app.dirty = true;
	switch (cmd) {
	case CMD_QUIT:
		app.quit = true;
		return;
	case CMD_THEME_DARK:
		app.theme = THEME_DARK;
		return;
	case CMD_THEME_LIGHT:
		app.theme = THEME_LIGHT;
		return;
	case CMD_THEME_DARKER:
		app.theme = THEME_DARKER;
		return;
	case CMD_ZOOM_IN:
	case CMD_ZOOM_OUT:
		ui_status("UI scale: start with --scale N");
		return;
	default:
		views_command(cmd);
		return;
	}
}

static const struct {
	u32 key;
	int mods;
	int cmd;
} bindings[] = {
    {SDLK_N, MOD_CTRL, CMD_NEW},          {SDLK_O, MOD_CTRL, CMD_OPEN},   {SDLK_S, MOD_CTRL, CMD_SAVE},
    {SDLK_S, MOD_CTRL | MOD_SHIFT, CMD_SAVE_AS}, {SDLK_Q, MOD_CTRL, CMD_QUIT}, {SDLK_Z, MOD_CTRL, CMD_UNDO},
    {SDLK_Y, MOD_CTRL, CMD_REDO},         {SDLK_SPACE, 0, CMD_PLAY},      {SDLK_HOME, 0, CMD_TO_START},
    {SDLK_L, 0, CMD_LOOP},                {SDLK_K, 0, CMD_METRONOME},     {SDLK_G, MOD_CTRL, CMD_GENERATE},
    {SDLK_E, MOD_CTRL, CMD_EXPORT_MP3},   {SDLK_DELETE, MOD_CTRL, CMD_DELETE_TRACK},
};

/* ---- the menu bar ------------------------------------------------------- */

enum { MB_FILE, MB_EDIT, MB_TRACK, MB_GENERATE, MB_EXPORT, MB_VIEW, MB_HELP, MB_COUNT };
static const char *mb_names[MB_COUNT] = {"File", "Edit", "Track", "Generate", "Export", "View", "Help"};

static void open_bar_menu(int m, int x, int y)
{
	menu_open(x, y);
	switch (m) {
	case MB_FILE:
		menu_item("New Project", CMD_NEW, true);
		menu_item("Open...", CMD_OPEN, true);
		menu_sep();
		menu_item("Save", CMD_SAVE, true);
		menu_item("Save As...", CMD_SAVE_AS, true);
		menu_sep();
		menu_item("Quit", CMD_QUIT, true);
		break;
	case MB_EDIT:
		menu_item("Undo", CMD_UNDO, true);
		menu_item("Redo", CMD_REDO, true);
		break;
	case MB_TRACK:
		menu_item("Add Guitar", CMD_ADD_GUITAR, true);
		menu_item("Add Bass", CMD_ADD_BASS, true);
		menu_item("Add Synth", CMD_ADD_SYNTH, true);
		menu_item("Add Drums", CMD_ADD_DRUMS, true);
		menu_sep();
		menu_item("Delete Track", CMD_DELETE_TRACK, true);
		break;
	case MB_GENERATE:
		menu_item("Generate Song", CMD_GENERATE, true);
		break;
	case MB_EXPORT:
		menu_item("Export MP3...", CMD_EXPORT_MP3, true);
		menu_item("Export Opus...", CMD_EXPORT_OPUS, true);
		menu_item("Export WAV...", CMD_EXPORT_WAV, true);
		break;
	case MB_VIEW:
		menu_item("Dark Modern", CMD_THEME_DARK, true);
		menu_item("Dark Modern Darker", CMD_THEME_DARKER, true);
		menu_item("Light Modern", CMD_THEME_LIGHT, true);
		break;
	case MB_HELP:
		menu_item("About FT2002", CMD_ABOUT, true);
		break;
	}
}

void menu_fill_sub(int sub) { (void)sub; }

/* ---- status ------------------------------------------------------------- */

void ui_status(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(app.status, sizeof app.status, fmt, ap);
	va_end(ap);
	app.status_ns = ft_now_ns();
	app.dirty = true;
}

/* ---- widgets -------------------------------------------------------------- */

u32 ui_id(const char *s, int i)
{
	u32 h = 2166136261u;
	for (; *s; s++)
		h = (h ^ (u8)*s) * 16777619u;
	h = (h ^ (u32)i) * 16777619u;
	return h ? h : 1;
}

bool ui_hit(int x, int y, int w, int h)
{
	return !menu_is_open() && app.mx >= x && app.mx < x + w && app.my >= y && app.my < y + h;
}

static const char *tip_text;
static int tip_x, tip_y;
static u32 tip_id;
static u64 tip_since;

static void tip(u32 id, const char *text, int x, int y)
{
	if (!text)
		return;
	if (tip_id != id) {
		tip_id = id;
		tip_since = app.now_ns;
	}
	tip_text = text;
	tip_x = x;
	tip_y = y;
}

/* press-and-release on the same widget */
static bool clickable(u32 id, int x, int y, int w, int h)
{
	bool over = ui_hit(x, y, w, h);
	if (over)
		app.hot = id;
	if (over && app.pressed[0])
		app.active = id;
	bool clicked = false;
	if (app.active == id && app.released[0]) {
		clicked = over;
		app.active = 0;
	}
	return clicked;
}

bool ui_button(u32 id, int x, int y, int w, int h, const char *label, bool on)
{
	bool c = clickable(id, x, y, w, h);
	bool over = ui_hit(x, y, w, h);
	u32 bg = on ? COL(C_BUTTON_BG) : over ? COL(C_LIST_HOVER_BG) : COL(C_INPUT_BG);
	u32 fg = on ? COL(C_BUTTON_FG) : COL(C_FG);
	draw_rect(x, y, w, h, bg);
	draw_frame(x, y, w, h, on ? COL(C_BUTTON_BG) : COL(C_INPUT_BORDER));
	int tw = font_width(FONT_UI, label, strlen(label));
	int ty = y + (h - font_m[FONT_UI].line) / 2;
	if (tw > w - PX(8))
		draw_text_fit(FONT_UI, x + PX(4), ty, label, strlen(label), w - PX(8), fg);
	else
		draw_text(FONT_UI, x + (w - tw) / 2, ty, label, strlen(label), fg);
	return c;
}

bool ui_icon_button(u32 id, int x, int y, int w, int h, int icon, bool on, const char *tiptext)
{
	bool c = clickable(id, x, y, w, h);
	bool over = ui_hit(x, y, w, h);
	if (over)
		draw_rect(x, y, w, h, COL(C_LIST_HOVER_BG));
	if (on)
		draw_rect(x, y, w, h, COL(C_LIST_ACTIVE_BG));
	int s = icon_size(icon);
	draw_icon(icon, x + (w - s) / 2, y + (h - s) / 2, on ? COL(C_LIST_ACTIVE_FG) : COL(C_FG));
	if (over)
		tip(id, tiptext, x, y + h + PX(4));
	return c;
}

static bool drag_value(u32 id, int x, int y, int w, int h, float *v, float lo, float hi, float def, bool horizontal, int span)
{
	bool over = ui_hit(x, y, w, h);
	float old = *v;
	if (over)
		app.hot = id;
	if (over && app.pressed[0]) {
		if (app.clicks >= 2) {
			*v = def;
		} else {
			app.active = id;
			app.drag_v0 = *v;
			app.drag_x0 = app.mx;
			app.drag_y0 = app.my;
		}
	}
	if (app.active == id) {
		if (app.down[0]) {
			float d = horizontal ? (float)(app.mx - app.drag_x0) : (float)(app.drag_y0 - app.my);
			float fine = (app.mods & MOD_SHIFT) ? 0.1f : 1.0f;
			*v = app.drag_v0 + d * fine * (hi - lo) / (float)FT_MAX(span, 1);
		} else {
			app.active = 0;
		}
	}
	if (over && app.wheel != 0 && app.active != id)
		*v += app.wheel * (hi - lo) * ((app.mods & MOD_SHIFT) ? 0.005f : 0.025f);
	*v = FT_CLAMP(*v, lo, hi);
	return *v != old;
}

bool ui_knob(u32 id, int x, int y, int size, const char *label, float *v, float lo, float hi, float def)
{
	bool ch = drag_value(id, x, y, size, size + PX(16), v, lo, hi, def, false, PX(150));
	/* a square knob: a ring of 21 segments, lit up to the value */
	const float PI = 3.14159265f;
	float t = (*v - lo) / (hi - lo);
	int cx = x + size / 2, cy = y + size / 2;
	float r = size * 0.42f;
	bool act = app.active == id || ui_hit(x, y, size, size + PX(16));
	for (int i = 0; i <= 20; i++) {
		float u = i / 20.0f;
		float a = (0.75f + 1.5f * u) * PI;
		int px = cx + (int)lrintf(cosf(a) * r) - PX(2), py = cy + (int)lrintf(sinf(a) * r) - PX(2);
		u32 c = u <= t + 1e-4f ? COL(C_FOCUS_BORDER) : COL(C_INPUT_BORDER);
		draw_rect(px, py, PX(4), PX(4), c);
	}
	float a = (0.75f + 1.5f * t) * PI;
	for (int k = 0; k < 6; k++) {
		float rr = r * 0.25f + r * 0.5f * k / 5.0f;
		draw_rect(cx + (int)lrintf(cosf(a) * rr) - PX(1), cy + (int)lrintf(sinf(a) * rr) - PX(1), PX(2), PX(2),
		          act ? COL(C_FG) : COL(C_FG_DIM));
	}
	char buf[32];
	if (act)
		snprintf(buf, sizeof buf, fabsf(hi - lo) >= 100 ? "%.0f" : "%.2f", *v);
	const char *s = act ? buf : label;
	int tw = font_width(FONT_SMALL, s, strlen(s));
	draw_text(FONT_SMALL, cx - tw / 2, y + size, s, strlen(s), act ? COL(C_FG) : COL(C_FG_DIM));
	return ch;
}

bool ui_hslider(u32 id, int x, int y, int w, int h, float *v, float lo, float hi, float def)
{
	bool ch = drag_value(id, x, y, w, h, v, lo, hi, def, true, w);
	float t = (*v - lo) / (hi - lo);
	draw_rect(x, y + h / 2 - PX(1), w, PX(2), COL(C_INPUT_BORDER));
	draw_rect(x, y + h / 2 - PX(1), (int)(w * t), PX(2), COL(C_FOCUS_BORDER));
	int kx = x + (int)((w - PX(6)) * t);
	draw_rect(kx, y + PX(2), PX(6), h - PX(4), app.active == id ? COL(C_FG) : COL(C_FG_DIM));
	return ch;
}

bool ui_toggle(u32 id, int x, int y, const char *label, bool *v)
{
	int s = PX(14);
	int tw = font_width(FONT_UI, label, strlen(label));
	bool c = clickable(id, x, y, s + PX(6) + tw, PX(18));
	if (c)
		*v = !*v;
	draw_rect(x, y + PX(2), s, s, *v ? COL(C_BUTTON_BG) : COL(C_INPUT_BG));
	draw_frame(x, y + PX(2), s, s, *v ? COL(C_BUTTON_BG) : COL(C_INPUT_BORDER));
	if (*v)
		draw_icon(ICON_CHECK, x - PX(1), y + PX(1), COL(C_BUTTON_FG));
	draw_text(FONT_UI, x + s + PX(6), y + (PX(18) - font_m[FONT_UI].line) / 2, label, strlen(label), COL(C_FG));
	return c;
}

bool ui_spin(u32 id, int x, int y, int w, int h, int *v, int lo, int hi, const char *fmt)
{
	int old = *v;
	bool over = ui_hit(x, y, w, h);
	u32 idl = id ^ 0x5bd1e995u, idr = id ^ 0x1b873593u;
	if (clickable(idl, x, y, PX(18), h))
		(*v)--;
	if (clickable(idr, x + w - PX(18), y, PX(18), h))
		(*v)++;
	if (over && app.wheel != 0)
		*v += app.wheel > 0 ? 1 : -1;
	*v = FT_CLAMP(*v, lo, hi);
	draw_rect(x, y, w, h, COL(C_INPUT_BG));
	draw_frame(x, y, w, h, over ? COL(C_FOCUS_BORDER) : COL(C_INPUT_BORDER));
	draw_icon(ICON_CHEVRON_DOWN, x + PX(1), y + (h - PX(16)) / 2, COL(C_FG_DIM));
	draw_icon(ICON_CHEVRON_RIGHT, x + w - PX(17), y + (h - PX(16)) / 2, COL(C_FG_DIM));
	char buf[64];
	snprintf(buf, sizeof buf, fmt, *v);
	int tw = font_width(FONT_UI, buf, strlen(buf));
	draw_text(FONT_UI, x + (w - tw) / 2, y + (h - font_m[FONT_UI].line) / 2, buf, strlen(buf), COL(C_INPUT_FG));
	return *v != old;
}

void ui_label(int font, int x, int y, u32 col, const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	if (n > 0)
		draw_text(font, x, y, buf, FT_MIN((size_t)n, sizeof buf - 1), col);
}

void ui_section(int x, int y, int w, const char *title)
{
	draw_text(FONT_SMALL_BOLD, x, y, title, strlen(title), COL(C_SECTION_HEADER_FG));
	draw_rect(x, y + font_m[FONT_SMALL_BOLD].line + PX(2), w, PX(1), COL(C_SECTION_HEADER_BORDER));
}

/* ---- events ----------------------------------------------------------------- */

void ui_event(const Event *e)
{
	app.dirty = true;
	if (e->type == EV_RESIZE) {
		app.w = e->x;
		app.h = e->y;
		gpu_resize(app.w, app.h);
		return;
	}
	if (e->type == EV_QUIT) {
		app.quit = true;
		return;
	}
	if (e->type == EV_MMOVE || e->type == EV_MDOWN || e->type == EV_MUP) {
		app.mx = e->x;
		app.my = e->y;
	}
	if (menu_event(e)) {
		/* the menu took it; a button released over it still ends a drag */
		if (e->type == EV_MUP && e->button >= 1 && e->button <= 3) {
			app.down[e->button - 1] = false;
			app.active = 0;
		}
		return;
	}
	switch (e->type) {
	case EV_MDOWN:
		if (e->button >= 1 && e->button <= 3) {
			app.down[e->button - 1] = app.pressed[e->button - 1] = true;
			app.clicks = e->clicks;
		}
		app.mods = e->mods;
		break;
	case EV_MUP:
		if (e->button >= 1 && e->button <= 3) {
			app.down[e->button - 1] = false;
			app.released[e->button - 1] = true;
		}
		break;
	case EV_WHEEL:
		app.wheel += e->wy;
		app.mods = e->mods;
		break;
	case EV_KEY:
		app.mods = e->mods;
		if (views_key(e))
			break;
		for (size_t i = 0; i < FT_COUNT(bindings); i++)
			if (bindings[i].key == e->key && bindings[i].mods == (e->mods & ~MOD_SUPER)) {
				run_command(bindings[i].cmd);
				return;
			}
		if (app.nkeys < (int)FT_COUNT(app.keys))
			app.keys[app.nkeys++] = e->key;
		break;
	case EV_KEYUP:
		app.mods = e->mods;
		break;
	case EV_TEXT: {
		size_t n = strlen(app.text);
		snprintf(app.text + n, sizeof app.text - n, "%s", e->text);
		break;
	}
	default:
		break;
	}
}

/* ---- layout and frame ----------------------------------------------------------- */

static const struct {
	int icon;
	const char *name;
} sides[SIDE_COUNT] = {
    [SIDE_TRACKS] = {ICON_TRACKS, "Tracks"},       [SIDE_INSTRUMENT] = {ICON_GUITAR, "Instrument"},
    [SIDE_CHORDS] = {ICON_CHORDS, "Chords"},       [SIDE_MIXER] = {ICON_MIXER, "Mixer"},
    [SIDE_GENERATE] = {ICON_DICE, "Generate"},     [SIDE_EXPORT] = {ICON_EXPORT, "Export"},
    [SIDE_SETTINGS] = {ICON_GEAR, "Settings"},
};

static int open_menu_bar = -1;

static void draw_menu_bar(int y, int h)
{
	draw_rect(0, y, app.w, h, COL(C_ACTBAR_BG));
	draw_rect(0, y + h - PX(1), app.w, PX(1), COL(C_ACTBAR_BORDER));
	int x = PX(8);
	const char *title = "FT2002";
	draw_text(FONT_UI_BOLD, x, y + (h - font_m[FONT_UI_BOLD].line) / 2, title, strlen(title), COL(C_FOCUS_BORDER));
	x += font_width(FONT_UI_BOLD, title, strlen(title)) + PX(12);
	if (!menu_is_open())
		open_menu_bar = -1;
	for (int m = 0; m < MB_COUNT; m++) {
		int tw = font_width(FONT_UI, mb_names[m], strlen(mb_names[m])) + PX(16);
		bool over = app.mx >= x && app.mx < x + tw && app.my >= y && app.my < y + h;
		if (over && app.pressed[0] && !menu_is_open()) {
			open_bar_menu(m, x, y + h);
			open_menu_bar = m;
			app.pressed[0] = false;
		}
		if (over || open_menu_bar == m)
			draw_rect(x, y + PX(3), tw, h - PX(6), COL(C_STATUS_HOVER_BG));
		draw_text(FONT_UI, x + PX(8), y + (h - font_m[FONT_UI].line) / 2, mb_names[m], strlen(mb_names[m]), COL(C_FG));
		x += tw;
	}
	const char *pname = views_title();
	int tw = font_width(FONT_UI, pname, strlen(pname));
	draw_text(FONT_UI, (app.w - tw) / 2, y + (h - font_m[FONT_UI].line) / 2, pname, strlen(pname), COL(C_FG_DIM));
}

static void draw_activity_bar(int x, int y, int w, int h)
{
	draw_rect(x, y, w, h, COL(C_ACTBAR_BG));
	draw_rect(x + w - PX(1), y, PX(1), h, COL(C_ACTBAR_BORDER));
	for (int i = 0; i < SIDE_COUNT; i++) {
		int iy = i == SIDE_SETTINGS ? y + h - w : y + i * w;
		u32 id = ui_id("activity", i);
		bool sel = app.side_open && app.side == i;
		if (clickable(id, x, iy, w, w)) {
			if (app.side == i)
				app.side_open = !app.side_open;
			else
				app.side = i, app.side_open = true;
		}
		bool over = ui_hit(x, iy, w, w);
		if (sel)
			draw_rect(x, iy, PX(2), w, COL(C_ACTBAR_ACTIVE_BORDER));
		draw_icon(sides[i].icon, x + (w - PX(24)) / 2, iy + (w - PX(24)) / 2,
		          sel || over ? COL(C_ACTBAR_FG) : COL(C_ACTBAR_FG_INACTIVE));
		if (over)
			tip(id, sides[i].name, x + w + PX(4), iy + PX(12));
	}
}

static void draw_status_bar(int y, int h)
{
	draw_rect(0, y, app.w, h, COL(C_STATUS_BG));
	int ty = y + (h - font_m[FONT_SMALL].line) / 2;
	if (app.status[0])
		draw_text(FONT_SMALL, PX(10), ty, app.status, strlen(app.status), COL(C_STATUS_FG));
	char right[160];
	views_status_right(right, sizeof right);
	int tw = font_width(FONT_SMALL, right, strlen(right));
	draw_text(FONT_SMALL, app.w - tw - PX(10), ty, right, strlen(right), COL(C_STATUS_FG));
}

static void draw_tip(void)
{
	if (!tip_text || app.now_ns - tip_since < 600000000ull)
		return;
	int tw = font_width(FONT_SMALL, tip_text, strlen(tip_text)) + PX(12);
	int th = font_m[FONT_SMALL].line + PX(8);
	int x = FT_MIN(tip_x, app.w - tw - PX(2)), y = FT_MIN(tip_y, app.h - th - PX(2));
	draw_rect(x, y, tw, th, COL(C_WIDGET_BG));
	draw_frame(x, y, tw, th, COL(C_WIDGET_BORDER));
	draw_text(FONT_SMALL, x + PX(6), y + PX(4), tip_text, strlen(tip_text), COL(C_FG));
}

void ui_frame(void)
{
	app.now_ns = ft_now_ns();
	app.hot = 0;
	const char *prev_tip = tip_text;
	tip_text = NULL;
	draw_begin(app.w, app.h);

	int menu_h = PX(30), trans_h = PX(40), status_h = PX(22), act_w = PX(48);
	int body_y = menu_h + trans_h, body_h = app.h - body_y - status_h;
	draw_rect(0, 0, app.w, app.h, COL(C_EDITOR_BG));

	/* the transport bar */
	draw_rect(0, menu_h, app.w, trans_h, COL(C_TABS_BG));
	draw_rect(0, menu_h + trans_h - PX(1), app.w, PX(1), COL(C_TABS_BORDER));
	draw_clip(0, menu_h, app.w, trans_h);
	views_draw_transport(0, menu_h, app.w, trans_h);
	draw_noclip();

	draw_activity_bar(0, body_y, act_w, body_h);
	int x = act_w;
	if (app.side_open) {
		int sw = app.side_w;
		draw_rect(x, body_y, sw, body_h, COL(C_SIDEBAR_BG));
		draw_rect(x + sw - PX(1), body_y, PX(1), body_h, COL(C_SIDEBAR_BORDER));
		const char *t = sides[app.side].name;
		char up[32];
		size_t n = strlen(t);
		for (size_t i = 0; i < n && i < sizeof up - 1; i++)
			up[i] = (char)(t[i] >= 'a' && t[i] <= 'z' ? t[i] - 32 : t[i]);
		draw_text(FONT_SMALL, x + PX(20), body_y + PX(10), up, FT_MIN(n, sizeof up - 1), COL(C_SIDEBAR_TITLE_FG));
		draw_clip(x, body_y + PX(32), sw - PX(1), body_h - PX(32));
		views_draw_side(app.side, x, body_y + PX(32), sw - PX(1), body_h - PX(32));
		draw_noclip();
		x += sw;
	}
	int mw = app.w - x;
	int ph = FT_MIN(app.panel_h, body_h / 2);
	int mh = body_h - ph;
	draw_clip(x, body_y, mw, mh);
	views_draw_main(x, body_y, mw, mh);
	draw_noclip();
	draw_rect(x, body_y + mh, mw, ph, COL(C_PANEL_BG));
	draw_rect(x, body_y + mh, mw, PX(1), COL(C_PANEL_BORDER));
	draw_clip(x, body_y + mh + PX(1), mw, ph - PX(1));
	views_draw_panel(x, body_y + mh + PX(1), mw, ph - PX(1));
	draw_noclip();

	draw_status_bar(app.h - status_h, status_h);
	draw_menu_bar(0, menu_h);
	views_draw_overlay();
	menu_draw();
	draw_tip();
	if (tip_text != prev_tip)
		app.dirty = true;

	gpu_frame(draw.q, draw.n, COL(C_EDITOR_BG));

	/* this frame's input is consumed */
	for (int i = 0; i < 3; i++)
		app.pressed[i] = app.released[i] = false;
	app.wheel = 0;
	app.nkeys = 0;
	app.text[0] = 0;
	if (!app.down[0] && app.active)
		app.active = 0;
}

bool ui_init(int w, int h, int scale, bool headless)
{
	app.w = w;
	app.h = h;
	app.scale = scale;
	app.headless = headless;
	app.theme = THEME_DARK;
	app.side = SIDE_TRACKS;
	app.side_open = true;
	app.side_w = PX(300);
	app.panel_h = PX(280);
	theme_init();
	if (!font_init(PX(13), PX(13), PX(18), PX(11)))
		return false;
	icons_init();
	app.dirty = true;
	return views_init();
}

void ui_shutdown(void)
{
	views_shutdown();
	font_shutdown();
}
