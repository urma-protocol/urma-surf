/* See LICENSE for copyright and license details. */
#include "startpage.h"
#include "brand.h"
#include "fonts.h"

static GtkWidget *
label(const char *text, const char *style)
{
	GtkWidget *widget = gtk_label_new(text);
	gtk_style_context_add_class(gtk_widget_get_style_context(widget), style);
	return widget;
}

static GtkWidget *
action(const char *text, const char *key, const char *style,
       GCallback callback, gpointer user)
{
	GtkWidget *button = gtk_button_new();
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
	GtkWidget *name = label(text, "start-action-name");
	GtkWidget *hint = label(key, "start-key");

	gtk_style_context_add_class(gtk_widget_get_style_context(button), style);
	gtk_label_set_xalign(GTK_LABEL(name), 0.0);
	gtk_box_pack_start(GTK_BOX(row), name, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(row), hint, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(button), row);
	atk_object_set_name(gtk_widget_get_accessible(button), text);
	g_signal_connect_swapped(button, "clicked", callback, user);
	return button;
}

GtkWidget *
urmastart_new(GtkWidget *context, GCallback address, GCallback tab,
              GCallback settings, GCallback shortcuts, gpointer user)
{
	static gboolean styled;
	static const char css[] =
	    ".urma-start { background-image: linear-gradient(125deg, #101e16, #0a120e 65%, #0e1a12); }"
	    ".urma-start label { font-family: \"" URMA_FONT_SANS "\", sans-serif; color: #e9f0e6; }"
	    ".urma-start .start-eyebrow { color: #8ca491; font-size: 10px; letter-spacing: 3px; font-weight: 700; }"
	    ".urma-start .start-title { font-size: 40px; font-weight: 600; letter-spacing: -1px; }"
	    ".urma-start .start-subtitle { font-size: 15px; color: #9caf9f; }"
	    ".urma-start .start-rule { background: #26382b; min-height: 1px; margin: 30px 0 20px; }"
	    ".urma-start button { background-image: none; box-shadow: none; text-shadow: none;"
	    " border-radius: 12px; transition: background-color 140ms ease; }"
	    ".urma-start button:focus { outline: 2px solid #d6fa52; outline-offset: 4px; }"
	    ".urma-start button.start-primary { background-color: #d6fa52; border: 1px solid #d6fa52;"
	    " padding: 16px 22px; margin-top: 26px; }"
	    ".urma-start button.start-primary:hover { background-color: #e3ff84; }"
	    ".urma-start button.start-primary:active { background-color: #bfdc45; }"
	    ".urma-start .start-primary label { color: #14200d; font-size: 14px; font-weight: 600; }"
	    ".urma-start .start-primary .start-key { color: #40501e; font-size: 11px; font-weight: 400; }"
	    ".urma-start button.start-secondary { background-color: #132019; border: 1px solid #27392d; padding: 14px 16px; }"
	    ".urma-start button.start-secondary:hover { background-color: #203225; border-color: #607947; }"
	    ".urma-start button.start-secondary:active { background-color: #283e2d; }"
	    ".urma-start .start-secondary label { color: #cedacc; font-size: 12px; }"
	    ".urma-start label.start-key { font-family: \"" URMA_FONT_MONO "\", monospace; color: #829780; font-size: 10px; }"
	    ".urma-start .start-footer { color: #718676; font-size: 11px; }";
	GtkWidget *root, *scroll, *layout, *hero, *title, *subtitle, *button, *row, *footer;

	if (!styled) {
		GtkCssProvider *provider = gtk_css_provider_new();
		gtk_css_provider_load_from_data(provider, css, -1, NULL);
		gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
		    GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		g_object_unref(provider);
		styled = TRUE;
	}
	root = gtk_event_box_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(root), "urma-start");
	gtk_widget_set_hexpand(root, TRUE);
	gtk_widget_set_vexpand(root, TRUE);
	atk_object_set_name(gtk_widget_get_accessible(root), "URMA Surf start page");

	scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(root), scroll);
	layout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_margin_start(layout, 40);
	gtk_widget_set_margin_end(layout, 40);
	gtk_widget_set_margin_top(layout, 32);
	gtk_widget_set_margin_bottom(layout, 24);
	gtk_container_add(GTK_CONTAINER(scroll), layout);

	hero = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_halign(hero, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(hero, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_top(hero, 28);
	gtk_widget_set_margin_bottom(hero, 36);
	gtk_box_pack_start(GTK_BOX(layout), hero, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hero), urmabrand_image(context, 128), FALSE, FALSE, 0);
	button = label("URMA SURF", "start-eyebrow");
	gtk_widget_set_margin_top(button, 22);
	gtk_widget_set_margin_bottom(button, 12);
	gtk_box_pack_start(GTK_BOX(hero), button, FALSE, FALSE, 0);
	title = label("A new place to explore.", "start-title");
	gtk_label_set_line_wrap(GTK_LABEL(title), TRUE);
	gtk_label_set_justify(GTK_LABEL(title), GTK_JUSTIFY_CENTER);
	gtk_box_pack_start(GTK_BOX(hero), title, FALSE, FALSE, 0);
	subtitle = label("Open an URMA address to begin.", "start-subtitle");
	gtk_widget_set_margin_top(subtitle, 12);
	gtk_box_pack_start(GTK_BOX(hero), subtitle, FALSE, FALSE, 0);
	button = action("Open an address", "Ctrl L", "start-primary", address, user);
	gtk_widget_set_halign(button, GTK_ALIGN_CENTER);
	gtk_widget_set_size_request(button, 340, -1);
	gtk_box_pack_start(GTK_BOX(hero), button, FALSE, FALSE, 0);

	button = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_style_context_add_class(gtk_widget_get_style_context(button), "start-rule");
	gtk_box_pack_start(GTK_BOX(hero), button, FALSE, FALSE, 0);
	row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_box_set_homogeneous(GTK_BOX(row), TRUE);
	gtk_box_pack_start(GTK_BOX(row), action("New tab", "Ctrl T", "start-secondary", tab, user), TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(row), action("Settings", "Ctrl ,", "start-secondary", settings, user), TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(row), action("Shortcuts", "Ctrl ?", "start-secondary", shortcuts, user), TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hero), row, FALSE, FALSE, 0);

	footer = label("Your browser. Your starting point.", "start-footer");
	gtk_box_pack_end(GTK_BOX(layout), footer, FALSE, FALSE, 0);
	gtk_widget_show_all(root);
	return root;
}
