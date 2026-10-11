/* The approved URMA Surf logo compiled into the binary;
 * see brand/README.md. */
#ifndef BRAND_H
#define BRAND_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>

/* The mark at the embedded size nearest to `px` (caller unrefs), NULL if the
 * embedded PNG cannot be decoded. */
GdkPixbuf *urmabrand_pixbuf(int px);

/* A GtkImage of the mark rendered `px` CSS pixels tall at the widget's own
 * scale factor (HiDPI: the 2x raster, not an upscale). */
GtkWidget *urmabrand_image(GtkWidget *context, int px);

/* Every embedded size, for gtk_window_set_icon_list (caller frees list and
 * unrefs the pixbufs). */
GList *urmabrand_icons(void);

#endif
