/* Immediate-mode quad list with a rectangular clip. Everything is square:
 * fills, 1px borders, glyphs, icons, squiggles. */
#ifndef FT_DRAW_H
#define FT_DRAW_H

#include "gpu.h"

enum {
	/* 24 px activity bar icons */
	ICON_TRACKS,
	ICON_GUITAR,
	ICON_CHORDS,
	ICON_MIXER,
	ICON_DICE,
	ICON_EXPORT,
	ICON_GEAR,
	/* 16 px: transport */
	ICON_PLAY,
	ICON_PAUSE,
	ICON_STOP,
	ICON_RECORD,
	ICON_TO_START,
	ICON_LOOP,
	ICON_METRONOME,
	ICON_SPEAKER,
	ICON_NOTE,
	ICON_DICE_S,
	/* 16 px */
	ICON_CHEVRON_RIGHT,
	ICON_CHEVRON_DOWN,
	ICON_CLOSE,
	ICON_DOT,
	ICON_ERROR,
	ICON_WARNING,
	ICON_INFO,
	ICON_CHECK,
	ICON_PLUS,
	ICON_MINUS,
	ICON_ARROW_UP,
	ICON_ARROW_DOWN,
	ICON_COUNT
};

typedef struct {
	Quad *q;
	u32 n, cap;
	int x0, y0, x1, y1; /* clip */
	int w, h;
} DrawList;

extern DrawList draw;

void icons_init(void);
int icon_size(int icon);

void draw_begin(int w, int h);
void draw_clip(int x, int y, int w, int h); /* intersect with screen */
void draw_noclip(void);
void draw_rect(int x, int y, int w, int h, u32 rgba);
/* the background image (gpu_set_background) at this alpha, over x,y,w,h */
void push_image_quad(int x, int y, int w, int h, u32 alpha);
/* Picture atlas rect (u, v, iw, ih) scaled into x, y, w, h (keep the aspect:
 * one scale, iw / w); see gpu_pic_pixels. */
void draw_picture(int x, int y, int w, int h, int u, int v, int iw, int ih, u32 alpha);
void draw_frame(int x, int y, int w, int h, u32 rgba); /* 1px outline */
void draw_squiggle(int x, int y, int w, u32 rgba);
void draw_icon(int icon, int x, int y, u32 rgba);
/* Draw UTF-8 text with its line top at y. Returns the pen x after it.
 * Tabs advance to the next multiple of tab_px from x0 when tab_px > 0. */
int draw_text(int font, int x, int y, const char *s, size_t n, u32 rgba);
/* Text cut with an ellipsis to fit max_w. */
int draw_text_fit(int font, int x, int y, const char *s, size_t n, int max_w, u32 rgba);
void draw_glyph(u32 gid, int x, int y, u32 rgba);
void draw_atlas_rect(int x, int y, int w, int h, u16 u, u16 v, u32 rgba);

#endif
