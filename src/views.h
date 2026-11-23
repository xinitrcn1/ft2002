/* The application's views, drawn inside the workbench shell (ui.c): the
 * transport bar, the side views of the activity bar, the main area (the
 * arrangement) and the bottom panel (the selected track's editor). */
#ifndef FT_VIEWS_H
#define FT_VIEWS_H

#include "ui.h"

bool views_init(void);
void views_shutdown(void);
const char *views_title(void);           /* the project's name, centred in the menu bar */
void views_status_right(char *buf, size_t n);
void views_command(int cmd);
bool views_key(const Event *e);          /* true when consumed */
void views_draw_transport(int x, int y, int w, int h);
void views_draw_side(int side, int x, int y, int w, int h);
void views_draw_main(int x, int y, int w, int h);
void views_draw_panel(int x, int y, int w, int h);
void views_draw_overlay(void);           /* dialogs over everything (under menus) */
void views_tick(void);                   /* once per frame before drawing */

#endif
