/* Icons are drawn once at start-up from distance-field primitives into the
 * glyph atlas (coverage only; the colour comes from the quad). */
#include "draw.h"
#include "font.h"

#include <math.h>

static u16 icon_u[ICON_COUNT], icon_v[ICON_COUNT];
static u8 icon_px[ICON_COUNT];

static float cov[32 * 32];
static int S; /* canvas size */
static float G = 1.0f; /* coordinate scale: the 24 px art drawn at other sizes */

static float seg_dist(float px, float py, float ax, float ay, float bx, float by)
{
	float dx = bx - ax, dy = by - ay;
	float l2 = dx * dx + dy * dy;
	float t = l2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0;
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	float ex = px - (ax + t * dx), ey = py - (ay + t * dy);
	return sqrtf(ex * ex + ey * ey);
}

static void put(int x, int y, float d)
{
	/* d = signed distance to the shape edge (negative inside). */
	float c = 0.5f - d;
	c = c < 0 ? 0 : c > 1 ? 1 : c;
	float *p = &cov[y * S + x];
	if (c > *p)
		*p = c;
}

/* Line of width w between points (pixel-centre coordinates). */
static void line(float ax, float ay, float bx, float by, float w)
{
	ax *= G;
	ay *= G;
	bx *= G;
	by *= G;
	for (int y = 0; y < S; y++)
		for (int x = 0; x < S; x++)
			put(x, y, seg_dist(x + 0.5f, y + 0.5f, ax, ay, bx, by) - w * 0.5f);
}

static void ring(float cx, float cy, float r, float w)
{
	cx *= G;
	cy *= G;
	r *= G;
	for (int y = 0; y < S; y++)
		for (int x = 0; x < S; x++) {
			float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			put(x, y, fabsf(sqrtf(dx * dx + dy * dy) - r) - w * 0.5f);
		}
}

static void disc(float cx, float cy, float r)
{
	cx *= G;
	cy *= G;
	r *= G;
	for (int y = 0; y < S; y++)
		for (int x = 0; x < S; x++) {
			float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			put(x, y, sqrtf(dx * dx + dy * dy) - r);
		}
}

/* Arc from angle a0 to a1 (radians, y down, clockwise positive). */
static void arc(float cx, float cy, float r, float a0, float a1, float w)
{
	int steps = 24;
	for (int i = 0; i < steps; i++) {
		float t0 = a0 + (a1 - a0) * i / steps, t1 = a0 + (a1 - a0) * (i + 1) / steps;
		line(cx + r * cosf(t0), cy + r * sinf(t0), cx + r * cosf(t1), cy + r * sinf(t1), w);
	}
}

static void box(float x0, float y0, float x1, float y1, float w)
{
	line(x0, y0, x1, y0, w);
	line(x1, y0, x1, y1, w);
	line(x1, y1, x0, y1, w);
	line(x0, y1, x0, y0, w);
}

static void store(int icon)
{
	u16 u, v;
	if (!font_atlas_alloc(S, S, &u, &v))
		return;
	for (int y = 0; y < S; y++)
		for (int x = 0; x < S; x++)
			font_atlas[(size_t)(v + y) * ATLAS_SIZE + u + x] = (u8)lrintf(cov[y * S + x] * 255.0f);
	gpu_atlas_dirty(u, v, S, S);
	icon_u[icon] = u;
	icon_v[icon] = v;
	icon_px[icon] = (u8)S;
}

static void begin(int size)
{
	S = size;
	memset(cov, 0, sizeof cov);
}

static int px24(void) { return (int)(24.0f * G + 0.5f); }

/* the arrangement: clips on three lanes */
static void art_tracks(float W)
{
	begin(px24());
	box(3.5f, 4.5f, 12.5f, 8.5f, W);
	box(8.5f, 10.5f, 20.5f, 14.5f, W);
	box(3.5f, 16.5f, 15.5f, 20.5f, W);
}

/* a guitar: body of two circles, neck, head */
static void art_guitar(float W)
{
	begin(px24());
	ring(8.0f, 16.5f, 4.6f, W);
	ring(11.0f, 12.0f, 3.2f, W);
	line(12.5f, 10.5f, 19.0f, 4.0f, W);
	line(18.0f, 3.0f, 21.0f, 6.0f, W + 0.6f);
	disc(8.5f, 16.0f, 1.3f);
}

/* the chord palette: a chord box (strings, frets, dots) */
static void art_chords(float W)
{
	begin(px24());
	line(5.0f, 4.0f, 19.0f, 4.0f, W + 0.8f);
	for (int i = 0; i < 4; i++)
		line(5.0f + i * 4.666f, 4.0f, 5.0f + i * 4.666f, 20.5f, W * 0.8f);
	for (int i = 1; i < 4; i++)
		line(5.0f, 4.0f + i * 5.5f, 19.0f, 4.0f + i * 5.5f, W * 0.8f);
	disc(9.67f, 6.75f, 1.9f);
	disc(14.33f, 12.25f, 1.9f);
}

/* the mixer: three faders */
static void art_mixer(float W)
{
	begin(px24());
	for (int i = 0; i < 3; i++)
		line(6.0f + i * 6.0f, 3.5f, 6.0f + i * 6.0f, 20.5f, W);
	static const float knob[3] = {14.0f, 7.0f, 17.0f};
	for (int i = 0; i < 3; i++)
		line(3.5f + i * 6.0f, knob[i], 8.5f + i * 6.0f, knob[i], W + 1.6f);
}

/* the autogenerator: a die */
static void art_dice(float W)
{
	begin(px24());
	box(4.5f, 4.5f, 19.5f, 19.5f, W);
	disc(8.5f, 8.5f, 1.5f);
	disc(12.0f, 12.0f, 1.5f);
	disc(15.5f, 15.5f, 1.5f);
}

/* export: a tray with an arrow out of it */
static void art_export(float W)
{
	begin(px24());
	line(4.5f, 13.5f, 4.5f, 19.5f, W);
	line(4.5f, 19.5f, 19.5f, 19.5f, W);
	line(19.5f, 19.5f, 19.5f, 13.5f, W);
	line(12.0f, 15.0f, 12.0f, 3.5f, W);
	line(12.0f, 3.5f, 7.5f, 8.0f, W);
	line(12.0f, 3.5f, 16.5f, 8.0f, W);
}

static void art_gear(float W)
{
	const float PI = 3.14159265f;
	begin(px24());
	ring(12, 12, 5.5f, W);
	ring(12, 12, 2.0f, W);
	for (int i = 0; i < 8; i++) {
		float a = i * PI / 4;
		line(12 + 5.5f * cosf(a), 12 + 5.5f * sinf(a), 12 + 8.5f * cosf(a), 12 + 8.5f * sinf(a), 2.4f);
	}
}

static void tri_right(float x0, float y0, float x1, float y1)
{
	float ym = (y0 + y1) * 0.5f;
	for (float k = 0; k <= (x1 - x0); k += 0.5f) {
		float t = k / (x1 - x0);
		line(x0 + k, y0 + (ym - y0) * t, x0 + k, y1 - (y1 - ym) * t, 1.0f);
	}
}

void icons_init(void)
{
	const float PI = 3.14159265f;
	const float w = 1.0f; /* stroke for 16 px icons */

	/* the activity bar at 24 px */
	G = 1.0f;
	const float W = 1.5f;
	art_tracks(W);
	store(ICON_TRACKS);
	art_guitar(W);
	store(ICON_GUITAR);
	art_chords(W);
	store(ICON_CHORDS);
	art_mixer(W);
	store(ICON_MIXER);
	art_dice(W);
	store(ICON_DICE);
	art_export(W);
	store(ICON_EXPORT);
	art_gear(W);
	store(ICON_GEAR);

	/* transport, 16 px */
	begin(16);
	tri_right(4.5f, 3.0f, 12.5f, 13.0f);
	store(ICON_PLAY);
	begin(16);
	line(5.0f, 3.5f, 5.0f, 12.5f, 2.4f);
	line(11.0f, 3.5f, 11.0f, 12.5f, 2.4f);
	store(ICON_PAUSE);
	begin(16);
	for (float k = 4.0f; k <= 12.0f; k += 1.0f)
		line(4.0f, k, 12.0f, k, 1.2f);
	store(ICON_STOP);
	begin(16);
	disc(8.0f, 8.0f, 4.8f);
	store(ICON_RECORD);
	begin(16); /* to start: a bar and a triangle pointing left */
	line(3.5f, 3.5f, 3.5f, 12.5f, 1.6f);
	for (float k = 0; k <= 8.0f; k += 0.5f)
		line(13.0f - k, 3.0f + k * 0.625f, 13.0f - k, 13.0f - k * 0.625f, 1.0f);
	store(ICON_TO_START);
	begin(16); /* loop: a ring with an arrow head */
	ring(8.0f, 8.0f, 5.0f, 1.2f);
	for (int yy = 0; yy < 7; yy++)
		for (int xx = 9; xx < S; xx++)
			cov[yy * S + xx] = 0;
	line(9.0f, 3.0f, 12.5f, 3.0f, 1.2f);
	line(12.5f, 3.0f, 12.5f, 6.5f, 1.2f);
	store(ICON_LOOP);
	begin(16); /* metronome: a trapezoid and a pendulum */
	line(5.5f, 2.5f, 10.5f, 2.5f, w);
	line(5.5f, 2.5f, 3.0f, 13.5f, w);
	line(10.5f, 2.5f, 13.0f, 13.5f, w);
	line(3.0f, 13.5f, 13.0f, 13.5f, w);
	line(8.0f, 11.5f, 11.5f, 4.0f, w);
	store(ICON_METRONOME);
	begin(16); /* speaker */
	line(2.5f, 6.0f, 5.0f, 6.0f, w);
	line(2.5f, 10.0f, 5.0f, 10.0f, w);
	line(2.5f, 6.0f, 2.5f, 10.0f, w);
	line(5.0f, 6.0f, 9.0f, 2.5f, w);
	line(5.0f, 10.0f, 9.0f, 13.5f, w);
	line(9.0f, 2.5f, 9.0f, 13.5f, w);
	arc(9.0f, 8.0f, 3.0f, -0.3f * PI, 0.3f * PI, w);
	arc(9.0f, 8.0f, 5.5f, -0.3f * PI, 0.3f * PI, w);
	store(ICON_SPEAKER);
	begin(16); /* a quaver */
	disc(5.5f, 12.0f, 2.4f);
	line(7.5f, 12.0f, 7.5f, 2.5f, w + 0.2f);
	line(7.5f, 2.5f, 12.0f, 6.0f, w + 0.2f);
	store(ICON_NOTE);
	begin(16); /* a die, small */
	box(2.5f, 2.5f, 13.5f, 13.5f, w);
	disc(5.5f, 5.5f, 1.2f);
	disc(8.0f, 8.0f, 1.2f);
	disc(10.5f, 10.5f, 1.2f);
	store(ICON_DICE_S);

	begin(16);
	line(6.0f, 4.0f, 10.0f, 8.0f, w + 0.2f);
	line(10.0f, 8.0f, 6.0f, 12.0f, w + 0.2f);
	store(ICON_CHEVRON_RIGHT);
	begin(16);
	line(4.0f, 6.0f, 8.0f, 10.0f, w + 0.2f);
	line(8.0f, 10.0f, 12.0f, 6.0f, w + 0.2f);
	store(ICON_CHEVRON_DOWN);
	begin(16);
	line(4.0f, 4.0f, 12.0f, 12.0f, w + 0.2f);
	line(12.0f, 4.0f, 4.0f, 12.0f, w + 0.2f);
	store(ICON_CLOSE);
	begin(16);
	disc(8, 8, 4.0f);
	store(ICON_DOT);
	begin(16);
	ring(8, 8, 6.0f, w);
	line(5.5f, 5.5f, 10.5f, 10.5f, w);
	line(10.5f, 5.5f, 5.5f, 10.5f, w);
	store(ICON_ERROR);
	begin(16);
	line(8.0f, 2.0f, 14.5f, 14.0f, w);
	line(14.5f, 14.0f, 1.5f, 14.0f, w);
	line(1.5f, 14.0f, 8.0f, 2.0f, w);
	line(8.0f, 6.0f, 8.0f, 10.0f, w + 0.3f);
	disc(8.0f, 12.0f, 0.8f);
	store(ICON_WARNING);
	begin(16);
	ring(8, 8, 6.0f, w);
	line(8.0f, 7.0f, 8.0f, 11.5f, w + 0.3f);
	disc(8.0f, 4.8f, 0.8f);
	store(ICON_INFO);
	begin(16);
	line(3.0f, 8.5f, 6.5f, 12.0f, w + 0.2f);
	line(6.5f, 12.0f, 13.0f, 4.5f, w + 0.2f);
	store(ICON_CHECK);
	begin(16);
	line(8.0f, 3.0f, 8.0f, 13.0f, w + 0.2f);
	line(3.0f, 8.0f, 13.0f, 8.0f, w + 0.2f);
	store(ICON_PLUS);
	begin(16);
	line(3.0f, 8.0f, 13.0f, 8.0f, w + 0.2f);
	store(ICON_MINUS);
	begin(16);
	line(8.0f, 3.0f, 8.0f, 13.0f, w);
	line(8.0f, 3.0f, 4.0f, 7.0f, w);
	line(8.0f, 3.0f, 12.0f, 7.0f, w);
	store(ICON_ARROW_UP);
	begin(16);
	line(8.0f, 3.0f, 8.0f, 13.0f, w);
	line(8.0f, 13.0f, 4.0f, 9.0f, w);
	line(8.0f, 13.0f, 12.0f, 9.0f, w);
	store(ICON_ARROW_DOWN);
}

int icon_size(int icon) { return icon_px[icon]; }

void draw_icon(int icon, int x, int y, u32 c)
{
	int s = icon_px[icon];
	if (!s)
		return;
	draw_atlas_rect(x, y, s, s, icon_u[icon], icon_v[icon], c);
}
