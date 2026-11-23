/* The application's state outside the UI: the open project, the audio
 * device and the transport. */
#ifndef FT_APP_H
#define FT_APP_H

#include "base.h"

bool app_start(const char *project, bool headless); /* project may be NULL */
void app_stop(void);
bool app_busy(void);                   /* playing / working: redraw every frame */
bool app_command_by_name(const char *name); /* scripts: "generate", "play", ... */

#endif
