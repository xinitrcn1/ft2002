/* The application's views (views.h). The skeleton: empty regions. */
#include "views.h"

#include <stdio.h>

bool views_init(void) { return true; }
void views_shutdown(void) {}
const char *views_title(void) { return "Untitled"; }
void views_status_right(char *buf, size_t n) { snprintf(buf, n, "48 kHz  120 BPM  4/4"); }
void views_command(int cmd) { ui_status("not yet: command %d", cmd); }
bool views_key(const Event *e) { return false; }
void views_tick(void) {}

void views_draw_transport(int x, int y, int w, int h)
{
	static const int icons[] = {ICON_TO_START, ICON_PLAY, ICON_STOP, ICON_RECORD, ICON_LOOP, ICON_METRONOME};
	int bx = x + PX(8);
	for (size_t i = 0; i < FT_COUNT(icons); i++, bx += PX(32))
		ui_icon_button(ui_id("transport", (int)i), bx, y + PX(4), PX(30), h - PX(8), icons[i], false, NULL);
}

void views_draw_side(int side, int x, int y, int w, int h) {}
void views_draw_main(int x, int y, int w, int h) {}
void views_draw_panel(int x, int y, int w, int h) {}
void views_draw_overlay(void) {}
