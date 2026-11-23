/* The workbench: window state, input, immediate-mode widgets.
 *
 * Drawing is vxide's (gpu.c / draw.c / font.c, one instanced quad draw);
 * the layout follows VSCodium: a menu bar, an activity bar on the left
 * choosing the side view, the main area, a bottom panel and the status bar.
 * Widgets are immediate mode: each frame the UI code asks "was this clicked
 * / dragged?" while drawing, from the input gathered since the last frame. */
#ifndef FT_UI_H
#define FT_UI_H

#include "base.h"
#include "theme.h"
#include "draw.h"
#include "font.h"
#include "menu.h"

/* ---- input ------------------------------------------------------------ */

enum { MOD_CTRL = 1, MOD_SHIFT = 2, MOD_ALT = 4, MOD_SUPER = 8 };
enum { EV_KEY, EV_KEYUP, EV_TEXT, EV_MDOWN, EV_MUP, EV_MMOVE, EV_WHEEL, EV_RESIZE, EV_FOCUS, EV_QUIT };

typedef struct Event {
	int type;
	u32 key; /* SDL keycode */
	int mods;
	char text[32];
	int x, y, button, clicks;
	float wx, wy;
} Event;

/* ---- side views (the activity bar) -------------------------------------- */

enum { SIDE_TRACKS, SIDE_INSTRUMENT, SIDE_CHORDS, SIDE_MIXER, SIDE_GENERATE, SIDE_EXPORT, SIDE_SETTINGS, SIDE_COUNT };

/* ---- commands (menus, keys, buttons) ------------------------------------ */

enum {
	CMD_NONE,
	CMD_NEW,
	CMD_OPEN,
	CMD_SAVE,
	CMD_SAVE_AS,
	CMD_QUIT,
	CMD_UNDO,
	CMD_REDO,
	CMD_PLAY,
	CMD_STOP,
	CMD_TO_START,
	CMD_LOOP,
	CMD_METRONOME,
	CMD_ADD_GUITAR,
	CMD_ADD_BASS,
	CMD_ADD_SYNTH,
	CMD_ADD_DRUMS,
	CMD_DELETE_TRACK,
	CMD_GENERATE,
	CMD_EXPORT_WAV,
	CMD_EXPORT_MP3,
	CMD_EXPORT_OPUS,
	CMD_THEME_DARK,
	CMD_THEME_LIGHT,
	CMD_THEME_DARKER,
	CMD_ZOOM_IN,
	CMD_ZOOM_OUT,
	CMD_ABOUT,
	CMD_COUNT
};
void run_command(int cmd);
const char *command_keys(int cmd); /* "Ctrl+S" or NULL */

/* ---- the application ----------------------------------------------------- */

typedef struct {
	int w, h;
	int scale;    /* integer UI scale */
	int theme;
	bool dirty;   /* redraw wanted */
	bool quit;
	bool headless;
	u64 now_ns;

	/* input gathered for this frame */
	int mx, my;
	bool down[3];      /* mouse buttons held (left, middle, right) */
	bool pressed[3];   /* went down this frame */
	bool released[3];  /* went up this frame */
	int clicks;        /* of the last press */
	float wheel;
	int mods;
	u32 keys[16];      /* keys pressed this frame */
	int nkeys;
	char text[64];     /* text typed this frame */

	/* immediate-mode widget state */
	u32 hot, active;   /* widget ids: under the mouse / being dragged */
	float drag_v0;     /* value at the start of a drag */
	int drag_y0, drag_x0;
	u32 focus;         /* text field with the keyboard */

	/* layout */
	int side;          /* SIDE_* */
	bool side_open;
	int side_w, panel_h;

	char status[256];  /* the status bar's left message */
	u64 status_ns;
} App;

extern App app;

#define PX(n) ((n) * app.scale)
#define COL(id) (themes[app.theme].c[id])

bool ui_init(int w, int h, int scale, bool headless);
void ui_shutdown(void);
void ui_event(const Event *e);
void ui_frame(void);   /* lay out, handle widgets, draw, present */
void ui_status(const char *fmt, ...);

/* ---- widgets -------------------------------------------------------------
 * ids: any stable u32 per widget (ui_id hashes a string and an index). */
u32 ui_id(const char *s, int i);
bool ui_hit(int x, int y, int w, int h);
bool ui_button(u32 id, int x, int y, int w, int h, const char *label, bool on);
bool ui_icon_button(u32 id, int x, int y, int w, int h, int icon, bool on, const char *tip);
/* vertical drag (and wheel) changes *v in [lo, hi]; double click resets to def */
bool ui_knob(u32 id, int x, int y, int size, const char *label, float *v, float lo, float hi, float def);
bool ui_hslider(u32 id, int x, int y, int w, int h, float *v, float lo, float hi, float def);
bool ui_toggle(u32 id, int x, int y, const char *label, bool *v);
/* a value box with arrows: click left/right half or wheel */
bool ui_spin(u32 id, int x, int y, int w, int h, int *v, int lo, int hi, const char *fmt);
void ui_label(int font, int x, int y, u32 col, const char *fmt, ...);
void ui_section(int x, int y, int w, const char *title);

#endif
