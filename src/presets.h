/* Instrument presets for new tracks and the instrument panel: the guitar
 * models (guitar.h), synth sounds in the spirit of geosim's composer
 * (supersaw, euro lead, pads, the formant choir, basses), and drum kits. */
#ifndef FT_PRESETS_H
#define FT_PRESETS_H

#include "song.h"

typedef struct {
	const char *name;
	InstKind inst;
	Patch (*make)(void);
} Preset;

extern const Preset presets[];
extern const int preset_count;
Patch preset_patch(int i);
int preset_find(const char *name);

#endif
