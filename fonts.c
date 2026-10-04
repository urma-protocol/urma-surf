/* Fonts compiled into the binary (Adwaita Sans / Adwaita Mono, SIL OFL 1.1,
 * fonts/LICENSE). Fontconfig reads fonts from files only, so the bytes are
 * written once per session into the private runtime directory and that
 * directory is registered as an application font directory: here for the GTK
 * chrome, and in every WebKit web process through the web extension. */
#include <stdio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <fontconfig/fontconfig.h>

#include "fonts.h"

#define EMBED(sym, file) \
__asm__(".section .rodata\n" \
        ".balign 16\n" \
        ".globl " #sym "\n" \
        ".type " #sym ", @object\n" \
        #sym ":\n" \
        ".incbin \"" file "\"\n" \
        #sym "_end:\n" \
        ".balign 8\n" \
        ".globl " #sym "_size\n" \
        ".type " #sym "_size, @object\n" \
        #sym "_size:\n" \
        ".quad " #sym "_end - " #sym "\n" \
        ".previous\n"); \
extern const unsigned char sym[]; \
extern const unsigned long sym##_size;

EMBED(urmafont_sans, "fonts/AdwaitaSans-Regular.ttf")
EMBED(urmafont_mono, "fonts/AdwaitaMono-Regular.ttf")

static gboolean
place(const char *dir, const char *name, const unsigned char *data, gsize len)
{
	char *path = g_build_filename(dir, name, NULL);
	GStatBuf st;
	GError *err = NULL;
	gboolean ok = TRUE;

	if (g_stat(path, &st) != 0 || (gsize)st.st_size != len) {
		ok = g_file_set_contents_full(path, (const char *)data, (gssize)len,
		    G_FILE_SET_CONTENTS_CONSISTENT, 0600, &err);
		if (!ok) {
			fprintf(stderr, "surf: embedded font not written to %s: %s\n",
			        path, err->message);
			g_error_free(err);
		}
	}
	g_free(path);
	return ok;
}

gboolean
urmafonts_use(const char *dir)
{
	return dir && *dir && FcConfigAppFontAddDir(NULL, (const FcChar8 *)dir);
}

char *
urmafonts_install(void)
{
	char *dir = g_build_filename(g_get_user_runtime_dir(), "urma-surf", "fonts", NULL);

	if (g_mkdir_with_parents(dir, 0700) != 0) {
		fprintf(stderr, "surf: cannot create %s; embedded fonts unavailable\n", dir);
		g_free(dir);
		return NULL;
	}
	if (!place(dir, "AdwaitaSans-Regular.ttf", urmafont_sans, urmafont_sans_size)
	    || !place(dir, "AdwaitaMono-Regular.ttf", urmafont_mono, urmafont_mono_size)
	    || !urmafonts_use(dir)) {
		g_free(dir);
		return NULL;
	}
	return dir;
}
