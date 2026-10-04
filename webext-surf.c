/* Web-process side of surf. Its one job: make the fonts compiled into the
 * UI binary visible to page layout, which runs in this process. The UI
 * process hands over the directory it wrote them to as initialization user
 * data (fontconfig state is per process). */
#include <stdio.h>

#include <fontconfig/fontconfig.h>
#include <glib.h>
#include <webkit2/webkit-web-extension.h>

#include "fonts.h"

G_MODULE_EXPORT void
webkit_web_extension_initialize_with_user_data(WebKitWebExtension *e,
                                               GVariant *data)
{
	const char *fonts = NULL;

	if (data && g_variant_is_of_type(data, G_VARIANT_TYPE_STRING))
		fonts = g_variant_get_string(data, NULL);
	if (fonts && *fonts && !FcConfigAppFontAddDir(NULL, (const FcChar8 *)fonts))
		fprintf(stderr, "webext-surf: embedded fonts not registered from %s\n",
		        fonts);
}
