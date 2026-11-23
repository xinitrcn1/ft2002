/* how varied are songs: distinct verse/chorus progressions and distinct
 * arp figures (the first 16 arp notes' intervals and rhythm) per mood, over
 * seeds 1..300, geosim's compose() vs compose_ex with variations */
#include "composer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int distinct(char (*v)[256], int n)
{
	int d = 0;
	for (int i = 0; i < n; i++) {
		bool seen = false;
		for (int j = 0; j < i && !seen; j++)
			seen = !strcmp(v[i], v[j]);
		d += !seen;
	}
	return d;
}
int main(void)
{
	static char progs[300][256], arps[300][256];
	for (int m = 0; m < MOOD_COUNT; m++)
		for (int vars = 0; vars < 2; vars++) {
			for (int k = 0; k < 300; k++) {
				Song s;
				ComposeOptions o = {.variations = vars};
				compose_ex(&s, (u64)k + 1, (Mood)m, &o);
				const char *p = strstr(s.description, "prog ");
				const char *e = p ? strstr(p, ", groove") : NULL;
				snprintf(progs[k], sizeof progs[k], "%.*s", p && e ? (int)(e - p) : 0, p ? p : "");
				char *w = arps[k];
				int n = 0, first = -1;
				u32 t0 = 0;
				w[0] = 0;
				for (u32 i = 0; i < s.nevents && n < 16; i++) {
					const NoteEvent *ev = &s.events[i];
					if (ev->ch != CH_ARP)
						continue;
					if (first < 0)
						first = ev->notes[0], t0 = ev->tick;
					w += sprintf(w, "%d@%u ", ev->notes[0] - first, (ev->tick - t0) % 96);
					n++;
				}
				song_free(&s);
			}
			printf("%-10s %-10s progressions %3d distinct, arp figures %3d distinct (of 300)\n", mood_name((Mood)m),
			       vars ? "variations" : "geosim", distinct(progs, 300), distinct(arps, 300));
		}
	return 0;
}
