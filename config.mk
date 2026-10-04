# surf version
VERSION = 2.1

# Customize below to fit your system

# paths
PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man
LIBPREFIX = $(PREFIX)/lib
LIBDIR = $(LIBPREFIX)/surf

X11INC = `pkg-config --cflags x11`
X11LIB = `pkg-config --libs x11`

GTKINC = `pkg-config --cflags gtk+-3.0 gcr-3 webkit2gtk-4.1 json-glib-1.0 fontconfig`
GTKLIB = `pkg-config --libs gtk+-3.0 gcr-3 webkit2gtk-4.1 json-glib-1.0 fontconfig`
WEBEXTINC = `pkg-config --cflags webkit2gtk-4.1 webkit2gtk-web-extension-4.1 gio-2.0 fontconfig`
WEBEXTLIBS = `pkg-config --libs webkit2gtk-4.1 webkit2gtk-web-extension-4.1 gio-2.0 fontconfig`

# includes and libs
INCS = $(X11INC) $(GTKINC)
LIBS = $(X11LIB) $(GTKLIB) -lgthread-2.0

# flags
CPPFLAGS = -DVERSION=\"$(VERSION)\" -DGCR_API_SUBJECT_TO_CHANGE \
           -DLIBPREFIX=\"$(LIBPREFIX)\" -DWEBEXTDIR=\"$(LIBDIR)\" \
           -D_DEFAULT_SOURCE
# Warnings are errors in this project. The only silenced class is
# missing-field-initializers: partial initializers in config.h rely on the
# C guarantee that the rest is zeroed, which is not a defect. Unused callback
# parameters are the GTK signal convention.
STRICT = -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers
SURFCFLAGS = -fPIC $(STRICT) $(INCS) $(CPPFLAGS)
WEBEXTCFLAGS = -fPIC $(STRICT) $(WEBEXTINC)

# compiler
#CC = c99
