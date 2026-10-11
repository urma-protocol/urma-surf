# URMA Surf, derived from surf. See NOTICES.md.
# See LICENSE file for copyright and license details.
.POSIX:

include config.mk

BROWSER = $(PACKAGE)
HEADERS = arg.h common.h urma.h fonts.h brand.h startpage.h
SRC = surf.c urma.c fonts.c brand.c startpage.c
WSRC = webext-surf.c
FONTS = fonts/AdwaitaSans-Regular.ttf fonts/AdwaitaMono-Regular.ttf
MARKS = brand/urma-mark-16.png brand/urma-mark-22.png brand/urma-mark-32.png \
        brand/urma-mark-44.png brand/urma-mark-48.png brand/urma-mark-64.png \
        brand/urma-mark-128.png brand/urma-mark-256.png
OBJ = $(SRC:.c=.o)
WOBJ = $(WSRC:.c=.o)
WLIB = $(WSRC:.c=.so)

DISTNAME = $(PACKAGE)-$(VERSION)
SOURCE_DATE_EPOCH = 0
# Approved test source/fixture paths, coordinated with their owner.
TESTFILES = tests/README.md tests/bridge_gate.py tests/bridge_test.c \
            tests/browser_gate.py tests/gui_driver.py tests/mock_cli.py \
            tests/fixture_store.py tests/store_gate.py tests/generate_fixture.py tests/startpage_gate.py \
            tests/fixtures/bridge.json tests/fixtures/authentic.json
DISTFILES = LICENSE NOTICES.md README.md Makefile config.mk config.def.h .gitignore \
            surf.1 surf-open.sh FAQ.md TODO.md surf.png $(SRC) $(WSRC) $(HEADERS) \
            fonts/README.md fonts/LICENSE $(FONTS) \
            brand/README.md brand/urma-surf.svg brand/render.py $(MARKS)

all: options $(BROWSER) $(WLIB)

options:
	@echo URMA Surf build options:
	@echo "CC            = $(CC)"
	@echo "CFLAGS        = $(SURFCFLAGS) $(CFLAGS)"
	@echo "WEBEXTCFLAGS  = $(WEBEXTCFLAGS) $(CFLAGS)"
	@echo "LDFLAGS       = $(LDFLAGS)"

$(BROWSER): $(OBJ)
	$(CC) $(SURFLDFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)

# Local compatibility alias for upstream probes; never installed.
surf: $(BROWSER)
	ln -sf $(BROWSER) surf

$(OBJ) $(WOBJ): config.h config.mk $(HEADERS) $(FONTS) $(MARKS)

config.h:
	cp config.def.h $@

.c.o:
	$(CC) $(SURFCFLAGS) $(CFLAGS) -c $< -o $@

$(WLIB): $(WOBJ)
	$(CC) -shared -Wl,-soname,$@ $(LDFLAGS) -o $@ $(WOBJ) $(WEBEXTLIBS)

webext-surf.o: webext-surf.c
	$(CC) $(WEBEXTCFLAGS) $(CFLAGS) -c webext-surf.c -o $@

check:
	PYTHONDONTWRITEBYTECODE=1 python3 tests/bridge_gate.py --source .

clean:
	rm -f $(BROWSER) surf $(OBJ)
	rm -f $(WLIB) $(WOBJ)

distclean: clean
	rm -f config.h $(DISTNAME).tar.gz

# Explicit source inputs only; dist does not clean an existing build.
# GNU tar and gzip -n give stable bytes for the same files/SOURCE_DATE_EPOCH.
dist:
	@set -eu; \
	stage=$$(mktemp -d .dist.XXXXXX); \
	trap 'rm -rf "$$stage"' 0 1 2 3 15; \
	mkdir "$$stage/$(DISTNAME)"; \
	for file in $(DISTFILES) $(TESTFILES); do \
	    mkdir -p "$$stage/$(DISTNAME)/$$(dirname "$$file")"; \
	    cp "$$file" "$$stage/$(DISTNAME)/$$file"; \
	done; \
	tar --sort=name --mtime="@$(SOURCE_DATE_EPOCH)" --owner=0 --group=0 \
	    --numeric-owner --mode='u+rwX,go+rX,go-w' \
	    -cf "$$stage/source.tar" -C "$$stage" "$(DISTNAME)"; \
	gzip -n -c "$$stage/source.tar" > "$$stage/source.tar.gz"; \
	mv "$$stage/source.tar.gz" "$(DISTNAME).tar.gz"

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(LIBDIR) \
	    $(DESTDIR)$(MANPREFIX)/man1 $(DESTDIR)$(DOCDIR)/fonts $(DESTDIR)$(DOCDIR)/brand
	cp -f $(BROWSER) $(DESTDIR)$(PREFIX)/bin/$(PACKAGE)
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(PACKAGE)
	cp -f $(WLIB) $(DESTDIR)$(LIBDIR)/$(WLIB)
	chmod 644 $(DESTDIR)$(LIBDIR)/$(WLIB)
	sed 's/VERSION/$(VERSION)/g' < surf.1 > $(DESTDIR)$(MANPREFIX)/man1/$(PACKAGE).1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/$(PACKAGE).1
	cp -f LICENSE NOTICES.md README.md $(DESTDIR)$(DOCDIR)/
	cp -f fonts/LICENSE fonts/README.md $(DESTDIR)$(DOCDIR)/fonts/
	cp -f brand/README.md $(DESTDIR)$(DOCDIR)/brand/
	chmod 644 $(DESTDIR)$(DOCDIR)/LICENSE $(DESTDIR)$(DOCDIR)/NOTICES.md \
	    $(DESTDIR)$(DOCDIR)/README.md $(DESTDIR)$(DOCDIR)/fonts/LICENSE \
	    $(DESTDIR)$(DOCDIR)/fonts/README.md $(DESTDIR)$(DOCDIR)/brand/README.md

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PACKAGE) $(DESTDIR)$(LIBDIR)/$(WLIB) \
	    $(DESTDIR)$(MANPREFIX)/man1/$(PACKAGE).1 \
	    $(DESTDIR)$(DOCDIR)/LICENSE $(DESTDIR)$(DOCDIR)/NOTICES.md \
	    $(DESTDIR)$(DOCDIR)/README.md $(DESTDIR)$(DOCDIR)/fonts/LICENSE \
	    $(DESTDIR)$(DOCDIR)/fonts/README.md $(DESTDIR)$(DOCDIR)/brand/README.md
	@for dir in $(DESTDIR)$(DOCDIR)/fonts $(DESTDIR)$(DOCDIR)/brand \
	    $(DESTDIR)$(DOCDIR) $(DESTDIR)$(LIBDIR); do \
	    if test -d "$$dir"; then rmdir "$$dir" 2>/dev/null || :; fi; \
	done

.PHONY: all options surf check clean distclean dist install uninstall
