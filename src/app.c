/* The application's state outside the UI (app.h). The skeleton. */
#include "app.h"
#include "ui.h"

#include <strings.h>

bool app_start(const char *project, bool headless) { return true; }
void app_stop(void) {}
bool app_busy(void) { return false; }

bool app_command_by_name(const char *name)
{
	static const struct {
		const char *n;
		int cmd;
	} names[] = {{"quit", CMD_QUIT}, {"theme-light", CMD_THEME_LIGHT}, {"theme-dark", CMD_THEME_DARK}};
	for (size_t i = 0; i < FT_COUNT(names); i++)
		if (!strcasecmp(name, names[i].n)) {
			run_command(names[i].cmd);
			return true;
		}
	return false;
}
