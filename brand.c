/* The approved URMA Browser logo compiled into the binary (the brand/ PNGs,
 * rasterized from brand/urma-browser.svg; brand/README.md). Decoded
 * from memory with GdkPixbufLoader: no path, no file, no network at run time. */
#include <stdio.h>
#include <glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>

#include "brand.h"

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

EMBED(urmamark_16,  "brand/urma-mark-16.png")
EMBED(urmamark_22,  "brand/urma-mark-22.png")
EMBED(urmamark_32,  "brand/urma-mark-32.png")
EMBED(urmamark_44,  "brand/urma-mark-44.png")
EMBED(urmamark_48,  "brand/urma-mark-48.png")
EMBED(urmamark_64,  "brand/urma-mark-64.png")
EMBED(urmamark_128, "brand/urma-mark-128.png")
EMBED(urmamark_256, "brand/urma-mark-256.png")

struct raster { int px; const unsigned char *data; const unsigned long *size; };
static const struct raster rasters[] = {
	{ 16,  urmamark_16,  &urmamark_16_size },
	{ 22,  urmamark_22,  &urmamark_22_size },
	{ 32,  urmamark_32,  &urmamark_32_size },
	{ 44,  urmamark_44,  &urmamark_44_size },
	{ 48,  urmamark_48,  &urmamark_48_size },
	{ 64,  urmamark_64,  &urmamark_64_size },
	{ 128, urmamark_128, &urmamark_128_size },
	{ 256, urmamark_256, &urmamark_256_size },
};

static GdkPixbuf *
decode(const struct raster *r)
{
	GdkPixbufLoader *l = gdk_pixbuf_loader_new();
	GdkPixbuf *p = NULL;
	GError *err = NULL;

	if (gdk_pixbuf_loader_write(l, r->data, *r->size, &err)
	    && gdk_pixbuf_loader_close(l, &err)) {
		p = gdk_pixbuf_loader_get_pixbuf(l);
		if (p)
			g_object_ref(p);
	}
	if (err) {
		fprintf(stderr, "surf: embedded mark %dpx not decoded: %s\n", r->px, err->message);
		g_error_free(err);
	}
	g_object_unref(l);
	return p;
}

GdkPixbuf *
urmabrand_pixbuf(int px)
{
	const struct raster *best = &rasters[0];
	guint i;

	for (i = 1; i < G_N_ELEMENTS(rasters); i++)
		if (ABS(rasters[i].px - px) < ABS(best->px - px))
			best = &rasters[i];
	return decode(best);
}

GtkWidget *
urmabrand_image(GtkWidget *context, int px)
{
	int scale = gtk_widget_get_scale_factor(context);
	GdkPixbuf *p = urmabrand_pixbuf(px * scale);
	cairo_surface_t *s;
	GtkWidget *img;

	if (!p)
		return gtk_image_new();
	s = gdk_cairo_surface_create_from_pixbuf(p, scale, gtk_widget_get_window(context));
	img = gtk_image_new_from_surface(s);
	cairo_surface_destroy(s);
	g_object_unref(p);
	return img;
}

GList *
urmabrand_icons(void)
{
	GList *icons = NULL;
	guint i;

	for (i = 0; i < G_N_ELEMENTS(rasters); i++) {
		GdkPixbuf *p = decode(&rasters[i]);
		if (p)
			icons = g_list_prepend(icons, p);
	}
	return icons;
}
