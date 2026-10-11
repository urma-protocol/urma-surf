/* Native presentation for about:blank; no web content or resolver calls. */
#ifndef STARTPAGE_H
#define STARTPAGE_H
#include <gtk/gtk.h>

GtkWidget *urmastart_new(GtkWidget *context, GCallback address, GCallback tab,
                        GCallback settings, GCallback shortcuts, gpointer user);
#endif
