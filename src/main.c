/* FT2002: a guitar-first music workstation.
 *
 * Run:        ft2002 [PROJECT.ft2]
 * Headless:   ft2002 --headless WxH [--script FILE] [--shot OUT.png] [PROJECT.ft2]
 *             (an offscreen Vulkan target; the script drives the UI, see
 *             run_script; without one, a single screenshot)
 * Options:    --scale N, --light, --darker */
#define _POSIX_C_SOURCE 200809L
#include "ui.h"
#include "gpu.h"
#include "views.h"
#include "app.h"

#include <stdio.h>
#include <strings.h>
#include <time.h>
#include <SDL3/SDL.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static float density = 1.0f;

static int map_mods(SDL_Keymod m)
{
	int r = 0;
	if (m & SDL_KMOD_CTRL)
		r |= MOD_CTRL;
	if (m & SDL_KMOD_SHIFT)
		r |= MOD_SHIFT;
	if (m & SDL_KMOD_ALT)
		r |= MOD_ALT;
	if (m & SDL_KMOD_GUI)
		r |= MOD_SUPER;
	return r;
}

static bool translate(const SDL_Event *s, Event *e)
{
	memset(e, 0, sizeof *e);
	switch (s->type) {
	case SDL_EVENT_QUIT:
		e->type = EV_QUIT;
		return true;
	case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
		e->type = EV_RESIZE;
		e->x = s->window.data1;
		e->y = s->window.data2;
		return true;
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		e->type = s->type == SDL_EVENT_KEY_DOWN ? EV_KEY : EV_KEYUP;
		e->key = (u32)s->key.key;
		e->mods = map_mods(s->key.mod);
		return !s->key.repeat || e->type == EV_KEY;
	case SDL_EVENT_TEXT_INPUT:
		e->type = EV_TEXT;
		snprintf(e->text, sizeof e->text, "%s", s->text.text);
		return true;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		e->type = s->type == SDL_EVENT_MOUSE_BUTTON_DOWN ? EV_MDOWN : EV_MUP;
		e->x = (int)(s->button.x * density);
		e->y = (int)(s->button.y * density);
		e->button = s->button.button == SDL_BUTTON_LEFT ? 1 : s->button.button == SDL_BUTTON_MIDDLE ? 2 : 3;
		e->clicks = s->button.clicks;
		e->mods = map_mods(SDL_GetModState());
		return true;
	case SDL_EVENT_MOUSE_MOTION:
		e->type = EV_MMOVE;
		e->x = (int)(s->motion.x * density);
		e->y = (int)(s->motion.y * density);
		e->mods = map_mods(SDL_GetModState());
		return true;
	case SDL_EVENT_MOUSE_WHEEL: {
		e->type = EV_WHEEL;
		float mx, my;
		SDL_GetMouseState(&mx, &my);
		e->x = (int)(mx * density);
		e->y = (int)(my * density);
		e->wx = s->wheel.x;
		e->wy = s->wheel.y;
		e->mods = map_mods(SDL_GetModState());
		return true;
	}
	case SDL_EVENT_WINDOW_FOCUS_GAINED:
		e->type = EV_FOCUS;
		return true;
	default:
		return false;
	}
}

/* ---- headless ------------------------------------------------------------ */

static void frame(void)
{
	views_tick();
	ui_frame();
}

static int shot(const char *path)
{
	frame();
	u8 *px = ft_alloc((size_t)app.w * app.h * 4);
	int r = -1;
	if (gpu_readback(px, app.w, app.h) && stbi_write_png(path, app.w, app.h, 4, px, app.w * 4))
		r = 0;
	free(px);
	fprintf(stderr, r == 0 ? "ft2002: wrote %s\n" : "ft2002: screenshot failed: %s\n", path);
	return r;
}

static u32 key_named(const char *s)
{
	static const struct {
		const char *n;
		u32 k;
	} names[] = {
	    {"space", SDLK_SPACE}, {"return", SDLK_RETURN}, {"escape", SDLK_ESCAPE}, {"tab", SDLK_TAB},
	    {"left", SDLK_LEFT},   {"right", SDLK_RIGHT},   {"up", SDLK_UP},         {"down", SDLK_DOWN},
	    {"home", SDLK_HOME},   {"end", SDLK_END},       {"delete", SDLK_DELETE}, {"backspace", SDLK_BACKSPACE},
	};
	for (size_t i = 0; i < FT_COUNT(names); i++)
		if (!strcasecmp(s, names[i].n))
			return names[i].k;
	if (s[0] && !s[1])
		return (u32)(s[0] >= 'A' && s[0] <= 'Z' ? s[0] + 32 : s[0]);
	if (s[0] == 'f' && s[1] >= '1' && s[1] <= '9')
		return SDLK_F1 + (u32)atoi(s + 1) - 1;
	return 0;
}

static void send_mouse(int type, int x, int y, int button, int clicks)
{
	Event e = {.type = type, .x = x, .y = y, .button = button, .clicks = clicks, .mods = 0};
	ui_event(&e);
	frame();
}

/* One command a line, '#' comments:
 *   click X Y | dclick X Y | rclick X Y | move X Y | drag X Y X2 Y2
 *   wheel DY X Y | key [ctrl+][shift+]NAME | type TEXT | wait FRAMES
 *   cmd NAME (a command by its menu name, e.g. "cmd generate") | shot PATH
 *   expect-status TEXT (fails unless the status bar contains TEXT) */
static int run_script(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "ft2002: cannot open script %s\n", path);
		return 1;
	}
	char line[1024];
	int rc = 0, ln = 0;
	frame();
	while (fgets(line, sizeof line, f)) {
		ln++;
		size_t n = strlen(line);
		while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = 0;
		if (!n || line[0] == '#')
			continue;
		char op[32] = {0};
		int a = 0, b = 0, c = 0, d = 0, used = 0;
		sscanf(line, "%31s %n", op, &used);
		const char *arg = line + used;
		if (!strcmp(op, "click") || !strcmp(op, "dclick") || !strcmp(op, "rclick")) {
			sscanf(arg, "%d %d", &a, &b);
			int btn = op[0] == 'r' ? 3 : 1;
			send_mouse(EV_MMOVE, a, b, 0, 0);
			int clicks = op[0] == 'd' ? 2 : 1;
			for (int k = 1; k <= clicks; k++) {
				send_mouse(EV_MDOWN, a, b, btn, k);
				send_mouse(EV_MUP, a, b, btn, k);
			}
		} else if (!strcmp(op, "move")) {
			sscanf(arg, "%d %d", &a, &b);
			send_mouse(EV_MMOVE, a, b, 0, 0);
		} else if (!strcmp(op, "drag")) {
			sscanf(arg, "%d %d %d %d", &a, &b, &c, &d);
			send_mouse(EV_MMOVE, a, b, 0, 0);
			send_mouse(EV_MDOWN, a, b, 1, 1);
			for (int k = 1; k <= 8; k++)
				send_mouse(EV_MMOVE, a + (c - a) * k / 8, b + (d - b) * k / 8, 0, 0);
			send_mouse(EV_MUP, c, d, 1, 1);
		} else if (!strcmp(op, "wheel")) {
			float dy = 0;
			sscanf(arg, "%f %d %d", &dy, &a, &b);
			send_mouse(EV_MMOVE, a, b, 0, 0);
			Event e = {.type = EV_WHEEL, .x = a, .y = b, .wy = dy};
			ui_event(&e);
			frame();
		} else if (!strcmp(op, "key")) {
			Event e = {.type = EV_KEY};
			const char *k = arg;
			for (;;) {
				if (!strncasecmp(k, "ctrl+", 5))
					e.mods |= MOD_CTRL, k += 5;
				else if (!strncasecmp(k, "shift+", 6))
					e.mods |= MOD_SHIFT, k += 6;
				else if (!strncasecmp(k, "alt+", 4))
					e.mods |= MOD_ALT, k += 4;
				else
					break;
			}
			e.key = key_named(k);
			if (!e.key) {
				fprintf(stderr, "%s:%d: unknown key %s\n", path, ln, k);
				rc = 1;
				break;
			}
			ui_event(&e);
			frame();
		} else if (!strcmp(op, "type")) {
			Event e = {.type = EV_TEXT};
			snprintf(e.text, sizeof e.text, "%s", arg);
			ui_event(&e);
			frame();
		} else if (!strcmp(op, "wait")) {
			a = atoi(arg);
			for (int k = 0; k < FT_MAX(a, 1); k++) {
				struct timespec ts = {0, 16000000};
				nanosleep(&ts, NULL);
				frame();
			}
		} else if (!strcmp(op, "cmd")) {
			if (!app_command_by_name(arg)) {
				fprintf(stderr, "%s:%d: unknown command %s\n", path, ln, arg);
				rc = 1;
				break;
			}
			frame();
		} else if (!strcmp(op, "shot")) {
			if (shot(arg) != 0) {
				rc = 1;
				break;
			}
		} else if (!strcmp(op, "expect-status")) {
			frame();
			if (!strstr(app.status, arg)) {
				fprintf(stderr, "%s:%d: status is \"%s\", expected \"%s\"\n", path, ln, app.status, arg);
				rc = 1;
				break;
			}
		} else {
			fprintf(stderr, "%s:%d: unknown command %s\n", path, ln, op);
			rc = 1;
			break;
		}
		if (app.quit)
			break;
	}
	fclose(f);
	return rc;
}

static void usage(void)
{
	fprintf(stderr, "usage: ft2002 [--scale N] [--light|--darker] [--headless WxH [--script FILE] [--shot OUT.png]] [PROJECT]\n");
	exit(2);
}

int main(int argc, char **argv)
{
	const char *script = NULL, *shot_path = NULL, *project = NULL;
	int hw = 0, hh = 0, scale = 0, theme = THEME_DARK;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--headless") && i + 1 < argc) {
			if (sscanf(argv[++i], "%dx%d", &hw, &hh) != 2)
				usage();
		} else if (!strcmp(argv[i], "--script") && i + 1 < argc) {
			script = argv[++i];
		} else if (!strcmp(argv[i], "--shot") && i + 1 < argc) {
			shot_path = argv[++i];
		} else if (!strcmp(argv[i], "--scale") && i + 1 < argc) {
			scale = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "--light")) {
			theme = THEME_LIGHT;
		} else if (!strcmp(argv[i], "--darker")) {
			theme = THEME_DARKER;
		} else if (argv[i][0] == '-') {
			usage();
		} else {
			project = argv[i];
		}
	}
	bool headless = hw > 0;
	SDL_Window *win = NULL;
	int w = hw, h = hh;
	if (!headless) {
		if (!SDL_Init(SDL_INIT_VIDEO)) {
			fprintf(stderr, "ft2002: SDL_Init: %s\n", SDL_GetError());
			return 1;
		}
		win = SDL_CreateWindow("FT2002", 1440, 900, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
		if (!win) {
			fprintf(stderr, "ft2002: SDL_CreateWindow: %s\n", SDL_GetError());
			return 1;
		}
		SDL_GetWindowSizeInPixels(win, &w, &h);
		density = SDL_GetWindowPixelDensity(win);
		if (!scale)
			scale = SDL_GetWindowDisplayScale(win) >= 1.75f ? 2 : 1;
		SDL_StartTextInput(win);
	}
	if (!scale)
		scale = 1;
	if (!gpu_init(win, w, h)) {
		fprintf(stderr, "ft2002: Vulkan initialisation failed\n");
		return 1;
	}
	if (!ui_init(w, h, scale, headless)) {
		fprintf(stderr, "ft2002: UI initialisation failed (fonts?)\n");
		return 1;
	}
	app.theme = theme;
	if (!app_start(project, headless)) {
		ui_shutdown();
		gpu_shutdown();
		return 1;
	}
	int rc = 0;
	if (headless) {
		if (script)
			rc = run_script(script);
		if (rc == 0 && (shot_path || !script))
			rc = shot(shot_path ? shot_path : "ft2002.png");
	} else {
		while (!app.quit) {
			SDL_Event se;
			if (SDL_WaitEventTimeout(&se, app_busy() ? 16 : 250)) {
				do {
					Event e;
					if (translate(&se, &e))
						ui_event(&e);
				} while (SDL_PollEvent(&se));
			}
			views_tick();
			if (app.dirty || app_busy()) {
				app.dirty = false;
				ui_frame();
			}
		}
	}
	app_stop();
	ui_shutdown();
	gpu_shutdown();
	if (win)
		SDL_DestroyWindow(win);
	if (!headless)
		SDL_Quit();
	return rc;
}
