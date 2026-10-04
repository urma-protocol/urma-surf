/* Fonts compiled into the binary; see fonts/README.md. */
#ifndef FONTS_H
#define FONTS_H

#include <glib.h>

#define URMA_FONT_SANS "Adwaita Sans"
#define URMA_FONT_MONO "Adwaita Mono"

/* Write the embedded fonts into the user's runtime directory and register it
 * as an application font directory of this process. Returns the directory
 * (caller frees) or NULL when nothing could be registered. */
char *urmafonts_install(void);

/* Register an already written directory in this process (web extension). */
gboolean urmafonts_use(const char *dir);

#endif
