/* Shared by the views (views.c: transport and side views; editors.c: the
 * arrangement and the track editors). */
#ifndef FT_VIEWS_INT_H
#define FT_VIEWS_INT_H

#include "app.h"
#include "guitar.h"
#include "views.h"

#include <stdio.h>

/* a colour per track (both themes), from the activity bar's lane palette */
u32 track_color(int t);
u32 with_alpha(u32 c, u32 a);
const char *track_kind(int t); /* "Steel acoustic", "Synth", "Drum kit" */

/* the arrangement and the editors (editors.c) */
void arrange_draw(int x, int y, int w, int h);
void editor_draw(int x, int y, int w, int h);
bool editor_key(const Event *e);
void follow_playhead(void);

/* a one-line text field; true when Return was pressed in it */
bool ui_textfield(u32 id, int x, int y, int w, int h, char *buf, size_t cap);
bool textfield_key(const Event *e); /* keys for the focused field */

static inline int line_h(int font) { return font_m[font].line; }
static inline int text_w(int font, const char *s) { return font_width(font, s, strlen(s)); }

#endif
