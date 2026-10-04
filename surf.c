/* See LICENSE file for copyright and license details.
 *
 * To understand surf, start reading main().
 */
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <glib.h>
#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <pwd.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <gio/gunixfdlist.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <gtk/gtkx.h>
#include <gcr/gcr.h>
#include <JavaScriptCore/JavaScript.h>
#include <webkit2/webkit2.h>
#include <X11/X.h>
#include <X11/Xatom.h>
#include <glib.h>

#include "arg.h"
#include "brand.h"
#include "common.h"
#include "fonts.h"
#include "urma.h"

#define LENGTH(x)               (sizeof(x) / sizeof(x[0]))
#define FALLTHROUGH             __attribute__((fallthrough))
/* Alt (MOD1) takes part so Alt-Left/Right can be bound like in any browser. */
#define CLEANMASK(mask)         (mask & (MODKEY|GDK_SHIFT_MASK|GDK_MOD1_MASK))

enum { AtomFind, AtomGo, AtomUri, AtomUTF8, AtomLast };

enum {
	OnDoc   = WEBKIT_HIT_TEST_RESULT_CONTEXT_DOCUMENT,
	OnLink  = WEBKIT_HIT_TEST_RESULT_CONTEXT_LINK,
	OnImg   = WEBKIT_HIT_TEST_RESULT_CONTEXT_IMAGE,
	OnMedia = WEBKIT_HIT_TEST_RESULT_CONTEXT_MEDIA,
	OnEdit  = WEBKIT_HIT_TEST_RESULT_CONTEXT_EDITABLE,
	OnBar   = WEBKIT_HIT_TEST_RESULT_CONTEXT_SCROLLBAR,
	OnSel   = WEBKIT_HIT_TEST_RESULT_CONTEXT_SELECTION,
	OnAny   = OnDoc | OnLink | OnImg | OnMedia | OnEdit | OnBar | OnSel,
};

typedef enum {
	AccessMicrophone,
	AccessWebcam,
	CaretBrowsing,
	Certificate,
	CookiePolicies,
	DarkMode,
	DiskCache,
	DefaultCharset,
	DNSPrefetch,
	Ephemeral,
	FileURLsCrossAccess,
	FontSize,
	Geolocation,
	HideBackground,
	Inspector,
	JavaScript,
	KioskMode,
	LoadImages,
	MediaManualPlay,
	PDFJSviewer,
	PreferredLanguages,
	RunInFullscreen,
	ScrollBars,
	ShowIndicators,
	SiteQuirks,
	SmoothScrolling,
	SpellChecking,
	SpellLanguages,
	StrictTLS,
	Style,
	WebGL,
	ZoomLevel,
	ParameterLast
} ParamName;

typedef union {
	int i;
	float f;
	const void *v;
} Arg;

typedef struct {
	Arg val;
	int prio;
} Parameter;

typedef struct Client {
	GtkWidget *win;
	/* MODERNISE (urma-surf): inline chrome, so the browser is usable
	 * without an external dmenu. */
	GtkWidget *box, *bar, *entry, *load, *status, *overlay;
	GtkWidget *back, *forward, *reload;
	/* FACELIFT: brand mark, address pill, verification badge, recovery spinner,
	 * settings popover, inline find bar. */
	GtkWidget *brand, *pill, *badge, *spin;
	GtkWidget *gear, *popover, *findbar, *findentry, *findcount, *focuscombo;
	guint zoomflash;
	int loading;
	/* URMA: how this tab arrived (name context) and what is shown. */
	char *urmaname, *urmanet, *urmagenesis, *urmatxid, *urmadisp;
	WebKitWebView *view;
	WebKitSettings *settings;
	WebKitWebContext *context;
	WebKitWebInspector *inspector;
	WebKitFindController *finder;
	WebKitHitTestResult *mousepos;
	GTlsCertificate *cert, *failedcert;
	GTlsCertificateFlags tlserr;
	Window xid;
	guint64 pageid;
	int progress, fullscreen, https, insecure, errorpage, errbadge;
	const char *title, *overtitle, *targeturi;
	const char *needle;
	struct Client *next;
} Client;

typedef struct {
	guint mod;
	guint keyval;
	void (*func)(Client *c, const Arg *a);
	const Arg arg;
} Key;

typedef struct {
	unsigned int target;
	unsigned int mask;
	guint button;
	void (*func)(Client *c, const Arg *a, WebKitHitTestResult *h);
	const Arg arg;
	unsigned int stopevent;
} Button;

typedef struct {
	const char *uri;
	Parameter config[ParameterLast];
	regex_t re;
} UriParameters;

typedef struct {
	char *regex;
	char *file;
	regex_t re;
} SiteSpecific;

/* Surf */
static void die(const char *errstr, ...) G_GNUC_NORETURN;
static void usage(void);
static void setup(void);
static void sigchld(int unused);
static void sighup(int unused);
static char *buildfile(const char *path);
static char *buildpath(const char *path);
static char *untildepath(const char *path);
static const char *getuserhomedir(const char *user);
static const char *getcurrentuserhomedir(void);
static Client *newclient(Client *c);
static void loaduri(Client *c, const Arg *a);
static const char *geturi(Client *c);
static void setatom(Client *c, int a, const char *v);
static const char *getatom(Client *c, int a);
static void updatetitle(Client *c);
static void gettogglestats(Client *c);
static void getpagestats(Client *c);
static WebKitCookieAcceptPolicy cookiepolicy_get(void);
static char cookiepolicy_set(const WebKitCookieAcceptPolicy p);
static void seturiparameters(Client *c, const char *uri, ParamName *params);
static void setparameter(Client *c, int refresh, ParamName p, const Arg *a);
static const char *getcert(const char *uri);
static void setcert(Client *c, const char *file);
static const char *getstyle(const char *uri);
static void setstyle(Client *c, const char *file);
static void runscript(Client *c);
static void evalscript(Client *c, const char *jsstr, ...);
static void updatewinid(Client *c);
static void handleplumb(Client *c, const char *uri);
static void newwindow(Client *c, const Arg *a, int noembed);
static void spawn(Client *c, const Arg *a);
static void destroyclient(Client *c);
static void cleanup(void);

/* GTK/WebKit */
static WebKitWebView *newview(Client *c, WebKitWebView *rv);
static void initwebextensions(WebKitWebContext *wc, Client *c);
static gboolean urmaresolveload(Client *c, const UrmaAddr *ad, const char *tail);
static void urmadetails(Client *c, const Arg *a);
static const char *displayuri(Client *c);
static void urmaclearctx(Client *c);
static void copylink(Client *c, const Arg *a);
static void buildchrome(Client *c);
static GtkWidget *navbutton(const char *label, const char *tip, GCallback cb,
                            Client *c);
static void goback(GtkButton *b, Client *c);
static void goforward(GtkButton *b, Client *c);
static void syncnav(Client *c);
static void reloadstop(GtkButton *b, Client *c);
static void setloading(Client *c, int loading);
static void setbadge(Client *c, const char *state, const char *text,
                     const char *tip);
static void brandclicked(GtkButton *b, Client *c);
static gboolean pillfocus(GtkWidget *w, GdkEventFocus *ev, Client *c);
static void fetchnotify(UrmaFetchEvent ev, const char *net, const char *txid,
                        const char *detail, gpointer user);
static char *webextdir(void);
static void showfind(Client *c, const Arg *a);
static void hidefind(Client *c);
static void findprev(GtkButton *b, Client *c);
static void findnext(GtkButton *b, Client *c);
static void findclose(GtkButton *b, Client *c);
static void findchanged(GtkEditable *e, Client *c);
static gboolean findkey(GtkWidget *w, GdkEventKey *ev, Client *c);
static void foundtext(WebKitFindController *f, guint count, Client *c);
static void failedtext(WebKitFindController *f, Client *c);
static void showsettings(Client *c, const Arg *a);
static gboolean rebuildsettings(gpointer data);
static GtkWidget *settingsbody(Client *c);
static GtkWidget *registryrow(Client *c, const char *net);
static void registrychanged(GtkComboBox *box, Client *c);
static void applyregistry(Client *c, const char *net, const char *genesis);
static char *otherregistry(Client *c, const char *net);
static void refreshaddresses(void);
static gboolean switchset(GtkSwitch *s, gboolean state, Client *c);
static GtkWidget *menuitem(const char *text, const char *accel, GCallback cb,
                           Client *c);
static void showshortcuts(Client *c, const Arg *a);
static void gearclicked(GtkButton *b, Client *c);
static gboolean scrollzoom(GtkWidget *w, GdkEvent *e, Client *c);
static void flashzoom(Client *c);
static gboolean clearzoomflash(gpointer data);
static void entryactivate(GtkEntry *e, Client *c);
static gboolean entrykey(GtkWidget *w, GdkEventKey *ev, Client *c);
static void showaddress(Client *c, const Arg *a);
static void togglebar(Client *c, const Arg *a);
static void setstatus(Client *c, const char *text);
static GtkWidget *createview(WebKitWebView *v, WebKitNavigationAction *a,
                             Client *c);
static gboolean buttonreleased(GtkWidget *w, GdkEvent *e, Client *c);
static GdkFilterReturn processx(GdkXEvent *xevent, GdkEvent *event,
                                gpointer d);
static gboolean winevent(GtkWidget *w, GdkEvent *e, Client *c);
static void showview(WebKitWebView *v, Client *c);
static GtkWidget *createwindow(Client *c);
static gboolean loadfailed(WebKitWebView *v, WebKitLoadEvent e, gchar *uri,
                           GError *err, Client *c);
static gboolean loadfailedtls(WebKitWebView *v, gchar *uri,
                              GTlsCertificate *cert,
                              GTlsCertificateFlags err, Client *c);
static void loadchanged(WebKitWebView *v, WebKitLoadEvent e, Client *c);
static void progresschanged(WebKitWebView *v, GParamSpec *ps, Client *c);
static void titlechanged(WebKitWebView *view, GParamSpec *ps, Client *c);
static void mousetargetchanged(WebKitWebView *v, WebKitHitTestResult *h,
                               guint modifiers, Client *c);
static gboolean permissionrequested(WebKitWebView *v,
                                    WebKitPermissionRequest *r, Client *c);
static gboolean decidepolicy(WebKitWebView *v, WebKitPolicyDecision *d,
                             WebKitPolicyDecisionType dt, Client *c);
static void decidenavigation(WebKitPolicyDecision *d, Client *c);
static void decidenewwindow(WebKitPolicyDecision *d, Client *c);
static void decideresource(WebKitPolicyDecision *d, Client *c);
static void insecurecontent(WebKitWebView *v, WebKitInsecureContentEvent e,
                            Client *c);
static void downloadstarted(WebKitWebContext *wc, WebKitDownload *d,
                            Client *c);
static void responsereceived(WebKitDownload *d, GParamSpec *ps, Client *c);
static void download(Client *c, WebKitURIResponse *r);
static void webprocessterminated(WebKitWebView *v,
                                 WebKitWebProcessTerminationReason r,
                                 Client *c);
static void closeview(WebKitWebView *v, Client *c);
static void destroywin(GtkWidget* w, Client *c);

/* Hotkeys */
static void pasteuri(GtkClipboard *clipboard, const char *text, gpointer d);
static void reload(Client *c, const Arg *a);
static void print(Client *c, const Arg *a);
static void showcert(Client *c, const Arg *a);
static void clipboard(Client *c, const Arg *a);
static void zoom(Client *c, const Arg *a);
static void scrollv(Client *c, const Arg *a);
static void scrollh(Client *c, const Arg *a);
static void navigate(Client *c, const Arg *a);
static void stop(Client *c, const Arg *a);
static void toggle(Client *c, const Arg *a);
static void togglefullscreen(Client *c, const Arg *a);
static void togglecookiepolicy(Client *c, const Arg *a);
static void toggleinspector(Client *c, const Arg *a);
static void find(Client *c, const Arg *a);

/* Buttons */
static void clicknavigate(Client *c, const Arg *a, WebKitHitTestResult *h);
static void clicknewwindow(Client *c, const Arg *a, WebKitHitTestResult *h);
static void clickexternplayer(Client *c, const Arg *a, WebKitHitTestResult *h);

static char winid[64];
static char togglestats[11];
static char pagestats[2];
static Atom atoms[AtomLast];
static Window embed;
static int showxid;
/* MODERNISE (urma-surf): start with the address bar visible; Ctrl-b hides it. */
static int showbar = 1;
static int cookiepolicy;
static Display *dpy;
static Client *clients;
static GdkDevice *gdkkb;
static char *stylefile;
static char *fontsdir;
static const char *useragent;
static Parameter *curconfig;
static int modparams[ParameterLast];
char *argv0;

static ParamName loadtransient[] = {
	Certificate,
	CookiePolicies,
	DiskCache,
	DNSPrefetch,
	FileURLsCrossAccess,
	JavaScript,
	LoadImages,
	PreferredLanguages,
	ShowIndicators,
	StrictTLS,
	ParameterLast
};

static ParamName loadcommitted[] = {
//	AccessMicrophone,
//	AccessWebcam,
	CaretBrowsing,
	DarkMode,
	DefaultCharset,
	FontSize,
	Geolocation,
	HideBackground,
	Inspector,
//	KioskMode,
	MediaManualPlay,
	PDFJSviewer,
	RunInFullscreen,
	ScrollBars,
	SiteQuirks,
	SmoothScrolling,
	SpellChecking,
	SpellLanguages,
	Style,
	ZoomLevel,
	ParameterLast
};

static ParamName loadfinished[] = {
	ParameterLast
};

/* configuration, allows nested code to access above variables */
#include "config.h"

void
die(const char *errstr, ...)
{
       va_list ap;

       va_start(ap, errstr);
       vfprintf(stderr, errstr, ap);
       va_end(ap);
       exit(1);
}

void
usage(void)
{
	die("usage: surf [-bBdDfFgGiIkKmMnNsStTvwxX]\n"
	    "[-a cookiepolicies ] [-c cookiefile] [-C stylefile] [-e xid]\n"
	    "[-r scriptfile] [-u useragent] [-z zoomlevel] [uri]\n");
}

void
setup(void)
{
	GdkDisplay *gdpy;
	size_t i;
	int j;
	char *prg;

	/* clean up any zombies immediately */
	sigchld(0);
	if (signal(SIGHUP, sighup) == SIG_ERR)
		die("Can't install SIGHUP handler");

	if (!(dpy = XOpenDisplay(NULL)))
		die("Can't open default display");

	/* atoms */
	atoms[AtomFind] = XInternAtom(dpy, "_SURF_FIND", False);
	atoms[AtomGo] = XInternAtom(dpy, "_SURF_GO", False);
	atoms[AtomUri] = XInternAtom(dpy, "_SURF_URI", False);
	atoms[AtomUTF8] = XInternAtom(dpy, "UTF8_STRING", False);

	/* FACELIFT: the embedded fonts must be known to fontconfig before GTK
	 * builds its first font map, so this precedes gtk_init(). */
	fontsdir = urmafonts_install();

	/* WM_CLASS = (basename, "Surf"), what gtk_window_set_wmclass did. */
	prg = g_path_get_basename(argv0);
	g_set_prgname(prg);
	g_free(prg);
	gtk_init(NULL, NULL);
	gdk_set_program_class("Surf");
	urma_set_fetch_notify(fetchnotify, NULL);

	gdpy = gdk_display_get_default();

	curconfig = defconfig;

	/* dirs and files */
	cookiefile = buildfile(cookiefile);
	scriptfile = buildfile(scriptfile);
	certdir    = buildpath(certdir);
	if (curconfig[Ephemeral].val.i)
		cachedir = NULL;
	else
		cachedir   = buildpath(cachedir);

	gdkkb = gdk_seat_get_keyboard(gdk_display_get_default_seat(gdpy));

	for (i = 0; i < LENGTH(certs); ++i) {
		if (!regcomp(&(certs[i].re), certs[i].regex, REG_EXTENDED)) {
			certs[i].file = g_strconcat(certdir, "/", certs[i].file,
			                            NULL);
		} else {
			fprintf(stderr, "Could not compile regex: %s\n",
			        certs[i].regex);
			certs[i].regex = NULL;
		}
	}

	if (!stylefile) {
		styledir = buildpath(styledir);
		for (i = 0; i < LENGTH(styles); ++i) {
			if (!regcomp(&(styles[i].re), styles[i].regex,
			    REG_EXTENDED)) {
				styles[i].file = g_strconcat(styledir, "/",
				                    styles[i].file, NULL);
			} else {
				fprintf(stderr, "Could not compile regex: %s\n",
				        styles[i].regex);
				styles[i].regex = NULL;
			}
		}
		g_free(styledir);
	} else {
		stylefile = buildfile(stylefile);
	}

	for (i = 0; i < LENGTH(uriparams); ++i) {
		if (regcomp(&(uriparams[i].re), uriparams[i].uri,
		    REG_EXTENDED)) {
			fprintf(stderr, "Could not compile regex: %s\n",
			        uriparams[i].uri);
			uriparams[i].uri = NULL;
			continue;
		}

		/* copy default parameters with higher priority */
		for (j = 0; j < ParameterLast; ++j) {
			if (defconfig[j].prio >= uriparams[i].config[j].prio)
				uriparams[i].config[j] = defconfig[j];
		}
	}
}

void
sigchld(int unused)
{
	/* URMA: no global reaper. GLib waits for the urma client it spawns
	 * (g_spawn_sync, GSubprocess) and needs the exit status; a waitpid(-1)
	 * loop stole it. Helpers are double-forked instead (see spawn). */
}

void
sighup(int unused)
{
	Arg a = { .i = 0 };
	Client *c;

	for (c = clients; c; c = c->next)
		reload(c, &a);
}

char *
buildfile(const char *path)
{
	char *dname, *bname, *bpath, *fpath;
	FILE *f;

	dname = g_path_get_dirname(path);
	bname = g_path_get_basename(path);

	bpath = buildpath(dname);
	g_free(dname);

	fpath = g_build_filename(bpath, bname, NULL);
	g_free(bpath);
	g_free(bname);

	if (!(f = fopen(fpath, "a")))
		die("Could not open file: %s\n", fpath);

	g_chmod(fpath, 0600); /* always */
	fclose(f);

	return fpath;
}

static const char*
getuserhomedir(const char *user)
{
	struct passwd *pw = getpwnam(user);

	if (!pw)
		die("Can't get user %s login information.\n", user);

	return pw->pw_dir;
}

static const char*
getcurrentuserhomedir(void)
{
	const char *homedir;
	const char *user;
	struct passwd *pw;

	homedir = getenv("HOME");
	if (homedir)
		return homedir;

	user = getenv("USER");
	if (user)
		return getuserhomedir(user);

	pw = getpwuid(getuid());
	if (!pw)
		die("Can't get current user home directory\n");

	return pw->pw_dir;
}

char *
buildpath(const char *path)
{
	char *apath, *fpath;

	if (path[0] == '~')
		apath = untildepath(path);
	else
		apath = g_strdup(path);

	/* creating directory */
	if (g_mkdir_with_parents(apath, 0700) < 0)
		die("Could not access directory: %s\n", apath);

	fpath = realpath(apath, NULL);
	g_free(apath);

	return fpath;
}

char *
untildepath(const char *path)
{
       char *apath, *name, *p;
       const char *homedir;

       if (path[1] == '/' || path[1] == '\0') {
               p = (char *)&path[1];
               homedir = getcurrentuserhomedir();
       } else {
               if ((p = strchr(path, '/')))
                       name = g_strndup(&path[1], p - (path + 1));
               else
                       name = g_strdup(&path[1]);

               homedir = getuserhomedir(name);
               g_free(name);
       }
       apath = g_build_filename(homedir, p, NULL);
       return apath;
}

Client *
newclient(Client *rc)
{
	Client *c;

	if (!(c = calloc(1, sizeof(Client))))
		die("Cannot malloc!\n");

	c->next = clients;
	clients = c;

	c->progress = 100;
	c->view = newview(c, rc ? rc->view : NULL);

	return c;
}

void
loaduri(Client *c, const Arg *a)
{
	struct stat st;
	char *url, *path, *apath;
	const char *uri = a->v;

	if (g_strcmp0(uri, "") == 0)
		return;

	/* URMA: a bare host of the decided grammar is an urma:// address. */
	{
		UrmaAddr ad;
		char *path = NULL, *tmp = NULL;

		if (!strstr(uri, "://") && !strchr(uri, '/') && urma_parse_host(uri, &ad))
			uri = tmp = g_strdup_printf("urma://%s/", uri);
		if (g_str_has_prefix(uri, "urma://")) {
			if (!urma_parse_uri(uri, &ad, &path)) {
				setstatus(c, "invalid urma address (canonical form required)");
			} else if (ad.kind == URMA_ADDR_NAME) {
				char *tail = urma_tail(uri);
				urmaresolveload(c, &ad, tail);
				g_free(tail);
			} else {
				urmaclearctx(c);
				setatom(c, AtomUri, uri);
				webkit_web_view_load_uri(c->view, uri);
				updatetitle(c);
			}
			g_free(path);
			g_free(tmp);
			return;
		}
		g_free(tmp);
	}
	if (g_str_has_prefix(uri, "http://") || g_str_has_prefix(uri, "https://")) {
		setstatus(c, "HTTP(S) is not an URMA destination");
		return;
	}
	if (g_str_has_prefix(uri, "file://")  ||
	    g_str_has_prefix(uri, "webkit://") ||
	    g_str_has_prefix(uri, "about:")) {
		url = g_strdup(uri);
	} else {
		if (uri[0] == '~')
			apath = untildepath(uri);
		else
			apath = (char *)uri;
		if (!stat(apath, &st) && (path = realpath(apath, NULL))) {
			url = g_strdup_printf("file://%s", path);
			free(path);
		} else {
			setstatus(c, "not a file and not an URMA address");
			if (apath != uri)
				free(apath);
			return;
		}
		if (apath != uri)
			free(apath);
	}

	setatom(c, AtomUri, url);

	if (strcmp(url, geturi(c)) == 0) {
		reload(c, a);
	} else {
		webkit_web_view_load_uri(c->view, url);
		updatetitle(c);
	}

	g_free(url);
}

const char *
geturi(Client *c)
{
	const char *uri;

	if (!(uri = webkit_web_view_get_uri(c->view)))
		uri = "about:blank";
	return uri;
}

void
setatom(Client *c, int a, const char *v)
{
	XChangeProperty(dpy, c->xid,
	                atoms[a], atoms[AtomUTF8], 8, PropModeReplace,
	                (unsigned char *)v, strlen(v) + 1);
	XSync(dpy, False);
}

const char *
getatom(Client *c, int a)
{
	static char buf[BUFSIZ];
	Atom adummy;
	int idummy;
	unsigned long ldummy;
	unsigned char *p = NULL;

	XSync(dpy, False);
	XGetWindowProperty(dpy, c->xid,
	                   atoms[a], 0L, BUFSIZ, False, atoms[AtomUTF8],
	                   &adummy, &idummy, &ldummy, &ldummy, &p);
	if (p)
		strncpy(buf, (char *)p, LENGTH(buf) - 1);
	else
		buf[0] = '\0';
	XFree(p);

	return buf;
}

void
updatetitle(Client *c)
{
	char *title;
	const char *name = c->overtitle ? c->overtitle :
	                   c->title ? c->title : "";

	if (curconfig[ShowIndicators].val.i) {
		gettogglestats(c);
		getpagestats(c);

		if (c->progress != 100)
			title = g_strdup_printf("[%i%%] %s:%s | %s",
			        c->progress, togglestats, pagestats, name);
		else
			title = g_strdup_printf("%s:%s | %s",
			        togglestats, pagestats, name);

		gtk_window_set_title(GTK_WINDOW(c->win), title);
		g_free(title);
	} else {
		title = *name ? g_strdup_printf("%s — URMA", name) : g_strdup("URMA");
		gtk_window_set_title(GTK_WINDOW(c->win), title);
		g_free(title);
	}
}

void
gettogglestats(Client *c)
{
	togglestats[0] = cookiepolicy_set(cookiepolicy_get());
	togglestats[1] = curconfig[CaretBrowsing].val.i ?   'C' : 'c';
	togglestats[2] = curconfig[Geolocation].val.i ?     'G' : 'g';
	togglestats[3] = curconfig[DiskCache].val.i ?       'D' : 'd';
	togglestats[4] = curconfig[LoadImages].val.i ?      'I' : 'i';
	togglestats[5] = curconfig[JavaScript].val.i ?      'S' : 's';
	togglestats[6] = curconfig[Style].val.i ?           'M' : 'm';
	togglestats[8] = curconfig[Certificate].val.i ?     'X' : 'x';
	togglestats[9] = curconfig[StrictTLS].val.i ?       'T' : 't';
}

void
getpagestats(Client *c)
{
	if (c->https)
		pagestats[0] = (c->tlserr || c->insecure) ?  'U' : 'T';
	else
		pagestats[0] = '-';
	pagestats[1] = '\0';
}

WebKitCookieAcceptPolicy
cookiepolicy_get(void)
{
	switch (((char *)curconfig[CookiePolicies].val.v)[cookiepolicy]) {
	case 'a':
		return WEBKIT_COOKIE_POLICY_ACCEPT_NEVER;
	case '@':
		return WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY;
	default: /* fallthrough */
	case 'A':
		return WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS;
	}
}

char
cookiepolicy_set(const WebKitCookieAcceptPolicy p)
{
	switch (p) {
	case WEBKIT_COOKIE_POLICY_ACCEPT_NEVER:
		return 'a';
	case WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY:
		return '@';
	default: /* fallthrough */
	case WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS:
		return 'A';
	}
}

void
seturiparameters(Client *c, const char *uri, ParamName *params)
{
	Parameter *uriconfig = NULL;
	size_t i;
	int p;

	for (i = 0; i < LENGTH(uriparams); ++i) {
		if (uriparams[i].uri &&
		    !regexec(&(uriparams[i].re), uri, 0, NULL, 0)) {
			uriconfig = uriparams[i].config;
			break;
		}
	}

	curconfig = uriconfig ? uriconfig : defconfig;

	for (i = 0; (p = params[i]) != ParameterLast; ++i) {
		switch(p) {
		default:
			if (!(defconfig[p].prio < curconfig[p].prio ||
			    defconfig[p].prio < modparams[p]))
				continue;
			FALLTHROUGH;
		case Certificate:
		case CookiePolicies:
		case Style:
			setparameter(c, 0, p, &curconfig[p].val);
		}
	}
}

void
setparameter(Client *c, int refresh, ParamName p, const Arg *a)
{
	GdkRGBA bgcolor = { 0 };

	modparams[p] = curconfig[p].prio;

	switch (p) {
	case AccessMicrophone:
		return; /* do nothing */
	case AccessWebcam:
		return; /* do nothing */
	case CaretBrowsing:
		webkit_settings_set_enable_caret_browsing(c->settings, a->i);
		refresh = 0;
		break;
	case Certificate:
		if (a->i)
			setcert(c, geturi(c));
		return; /* do not update */
	case CookiePolicies:
		webkit_cookie_manager_set_accept_policy(
		    webkit_web_context_get_cookie_manager(c->context),
		    cookiepolicy_get());
		refresh = 0;
		break;
	case DarkMode:
		g_object_set(gtk_settings_get_default(),
		             "gtk-application-prefer-dark-theme", a->i, NULL);
		return;
	case DiskCache:
		webkit_web_context_set_cache_model(c->context, a->i ?
		    WEBKIT_CACHE_MODEL_WEB_BROWSER :
		    WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);
		return; /* do not update */
	case DefaultCharset:
		webkit_settings_set_default_charset(c->settings, a->v);
		return; /* do not update */
	case DNSPrefetch:
		/* The setter is deprecated; the property is not. */
		g_object_set(c->settings, "enable-dns-prefetching", a->i, NULL);
		return; /* do not update */
	case FileURLsCrossAccess:
		webkit_settings_set_allow_file_access_from_file_urls(
		    c->settings, a->i);
		webkit_settings_set_allow_universal_access_from_file_urls(
		    c->settings, a->i);
		return; /* do not update */
	case FontSize:
		webkit_settings_set_default_font_size(c->settings, a->i);
		return; /* do not update */
	case Geolocation:
		refresh = 0;
		break;
	case HideBackground:
		if (a->i)
			webkit_web_view_set_background_color(c->view, &bgcolor);
		return; /* do not update */
	case Inspector:
		webkit_settings_set_enable_developer_extras(c->settings, a->i);
		return; /* do not update */
	case JavaScript:
		webkit_settings_set_enable_javascript(c->settings, a->i);
		break;
	case KioskMode:
		return; /* do nothing */
	case LoadImages:
		webkit_settings_set_auto_load_images(c->settings, a->i);
		break;
	case MediaManualPlay:
		webkit_settings_set_media_playback_requires_user_gesture(
		    c->settings, a->i);
		break;
	case PDFJSviewer:
		return; /* do nothing */
	case PreferredLanguages:
		return; /* do nothing */
	case RunInFullscreen:
		return; /* do nothing */
	case ScrollBars:
		/* Disabled until we write some WebKitWebExtension for
		 * manipulating the DOM directly.
		enablescrollbars = !enablescrollbars;
		evalscript(c, "document.documentElement.style.overflow = '%s'",
		    enablescrollbars ? "auto" : "hidden");
		*/
		return; /* do not update */
	case ShowIndicators:
		break;
	case SmoothScrolling:
		webkit_settings_set_enable_smooth_scrolling(c->settings, a->i);
		return; /* do not update */
	case SiteQuirks:
		webkit_settings_set_enable_site_specific_quirks(
		    c->settings, a->i);
		break;
	case SpellChecking:
		webkit_web_context_set_spell_checking_enabled(
		    c->context, a->i);
		return; /* do not update */
	case SpellLanguages:
		return; /* do nothing */
	case StrictTLS:
		webkit_website_data_manager_set_tls_errors_policy(
		    webkit_web_view_get_website_data_manager(c->view), a->i ?
		    WEBKIT_TLS_ERRORS_POLICY_FAIL :
		    WEBKIT_TLS_ERRORS_POLICY_IGNORE);
		break;
	case Style:
		webkit_user_content_manager_remove_all_style_sheets(
		    webkit_web_view_get_user_content_manager(c->view));
		if (a->i)
			setstyle(c, getstyle(geturi(c)));
		refresh = 0;
		break;
	case WebGL:
		webkit_settings_set_enable_webgl(c->settings, a->i);
		break;
	case ZoomLevel:
		webkit_web_view_set_zoom_level(c->view, a->f);
		return; /* do not update */
	default:
		return; /* do nothing */
	}

	updatetitle(c);
	if (refresh)
		reload(c, a);
}

const char *
getcert(const char *uri)
{
	size_t i;

	for (i = 0; i < LENGTH(certs); ++i) {
		if (certs[i].regex &&
		    !regexec(&(certs[i].re), uri, 0, NULL, 0))
			return certs[i].file;
	}

	return NULL;
}

void
setcert(Client *c, const char *uri)
{
	const char *file = getcert(uri);
	char *host;
	GTlsCertificate *cert;

	if (!file)
		return;

	if (!(cert = g_tls_certificate_new_from_file(file, NULL))) {
		fprintf(stderr, "Could not read certificate file: %s\n", file);
		return;
	}

	if ((uri = strstr(uri, "https://"))) {
		uri += sizeof("https://") - 1;
		host = g_strndup(uri, strchr(uri, '/') - uri);
		webkit_web_context_allow_tls_certificate_for_host(c->context,
		    cert, host);
		g_free(host);
	}

	g_object_unref(cert);

}

const char *
getstyle(const char *uri)
{
	size_t i;

	if (stylefile)
		return stylefile;

	for (i = 0; i < LENGTH(styles); ++i) {
		if (styles[i].regex &&
		    !regexec(&(styles[i].re), uri, 0, NULL, 0))
			return styles[i].file;
	}

	return "";
}

void
setstyle(Client *c, const char *file)
{
	gchar *style;

	/* No user style sheet is the normal state, not an error. */
	if (!file || !*file || !g_file_test(file, G_FILE_TEST_EXISTS))
		return;
	if (!g_file_get_contents(file, &style, NULL, NULL)) {
		fprintf(stderr, "Could not read style file: %s\n", file);
		return;
	}

	webkit_user_content_manager_add_style_sheet(
	    webkit_web_view_get_user_content_manager(c->view),
	    webkit_user_style_sheet_new(style,
	    WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
	    WEBKIT_USER_STYLE_LEVEL_USER,
	    NULL, NULL));

	g_free(style);
}

void
runscript(Client *c)
{
	gchar *script;
	gsize l;

	if (g_file_get_contents(scriptfile, &script, &l, NULL) && l)
		evalscript(c, "%s", script);
	g_free(script);
}

void
evalscript(Client *c, const char *jsstr, ...)
{
	va_list ap;
	gchar *script;

	va_start(ap, jsstr);
	script = g_strdup_vprintf(jsstr, ap);
	va_end(ap);

	webkit_web_view_evaluate_javascript(c->view, script, -1,
	    NULL, NULL, NULL, NULL, NULL);
	g_free(script);
}

void
updatewinid(Client *c)
{
	snprintf(winid, LENGTH(winid), "%lu", c->xid);
}

void
handleplumb(Client *c, const char *uri)
{
	Arg a = (Arg)PLUMB(uri);
	spawn(c, &a);
}

void
newwindow(Client *c, const Arg *a, int noembed)
{
	int i = 0;
	char tmp[64];
	const char *cmd[29], *uri;
	const Arg arg = { .v = cmd };

	cmd[i++] = argv0;
	cmd[i++] = "-a";
	cmd[i++] = curconfig[CookiePolicies].val.v;
	cmd[i++] = curconfig[ScrollBars].val.i ? "-B" : "-b";
	if (cookiefile && g_strcmp0(cookiefile, "")) {
		cmd[i++] = "-c";
		cmd[i++] = cookiefile;
	}
	if (stylefile && g_strcmp0(stylefile, "")) {
		cmd[i++] = "-C";
		cmd[i++] = stylefile;
	}
	cmd[i++] = curconfig[DiskCache].val.i ? "-D" : "-d";
	if (embed && !noembed) {
		cmd[i++] = "-e";
		snprintf(tmp, LENGTH(tmp), "%lu", embed);
		cmd[i++] = tmp;
	}
	cmd[i++] = curconfig[RunInFullscreen].val.i ? "-F" : "-f" ;
	cmd[i++] = curconfig[Geolocation].val.i ?     "-G" : "-g" ;
	cmd[i++] = curconfig[LoadImages].val.i ?      "-I" : "-i" ;
	cmd[i++] = curconfig[KioskMode].val.i ?       "-K" : "-k" ;
	cmd[i++] = curconfig[Style].val.i ?           "-M" : "-m" ;
	cmd[i++] = curconfig[Inspector].val.i ?       "-N" : "-n" ;
	if (scriptfile && g_strcmp0(scriptfile, "")) {
		cmd[i++] = "-r";
		cmd[i++] = scriptfile;
	}
	cmd[i++] = curconfig[JavaScript].val.i ? "-S" : "-s";
	cmd[i++] = curconfig[StrictTLS].val.i ? "-T" : "-t";
	if (fulluseragent && g_strcmp0(fulluseragent, "")) {
		cmd[i++] = "-u";
		cmd[i++] = fulluseragent;
	}
	if (showxid)
		cmd[i++] = "-w";
	cmd[i++] = curconfig[Certificate].val.i ? "-X" : "-x" ;
	/* do not keep zoom level */
	cmd[i++] = "--";
	if ((uri = a->v))
		cmd[i++] = uri;
	cmd[i] = NULL;

	spawn(c, &arg);
}

void
spawn(Client *c, const Arg *a)
{
	pid_t pid = fork();

	if (pid == 0) {
		/* Double fork: the helper is reparented to init, no zombie, no
		 * SIGCHLD reaper that would swallow GLib's child statuses. */
		if (fork() != 0)
			_exit(0);
		if (dpy)
			close(ConnectionNumber(dpy));
		setsid();
		execvp(((char **)a->v)[0], (char **)a->v);
		fprintf(stderr, "%s: execvp %s", argv0, ((char **)a->v)[0]);
		perror(" failed");
		_exit(1);
	} else if (pid > 0) {
		waitpid(pid, NULL, 0);
	}
}

void
destroyclient(Client *c)
{
	Client *p;

	/* The zoom readout timer holds this client; it must not outlive it. */
	if (c->zoomflash) {
		g_source_remove(c->zoomflash);
		c->zoomflash = 0;
	}
	urmaclearctx(c);
	webkit_web_view_stop_loading(c->view);
	/* Not needed, has already been called
	gtk_widget_destroy(c->win);
	 */

	for (p = clients; p && p->next != c; p = p->next)
		;
	if (p)
		p->next = c->next;
	else
		clients = c->next;
	free(c);
}

void
cleanup(void)
{
	while (clients)
		destroyclient(clients);

	g_free(cookiefile);
	g_free(scriptfile);
	g_free(stylefile);
	g_free(cachedir);
	g_free(fontsdir);
	XCloseDisplay(dpy);
}

WebKitWebView *
newview(Client *c, WebKitWebView *rv)
{
	WebKitWebView *v;
	WebKitSettings *settings;
	WebKitWebContext *context;
	WebKitCookieManager *cookiemanager;
	WebKitUserContentManager *contentmanager;

	/* Webview */
	if (rv) {
		v = WEBKIT_WEB_VIEW(webkit_web_view_new_with_related_view(rv));
		context = webkit_web_view_get_context(v);
		settings = webkit_web_view_get_settings(v);
	} else {
		settings = webkit_settings_new_with_settings(
		   "allow-file-access-from-file-urls", curconfig[FileURLsCrossAccess].val.i,
		   "allow-universal-access-from-file-urls", curconfig[FileURLsCrossAccess].val.i,
		   "auto-load-images", curconfig[LoadImages].val.i,
		   "default-charset", curconfig[DefaultCharset].val.v,
		   "default-font-size", curconfig[FontSize].val.i,
		   "enable-caret-browsing", curconfig[CaretBrowsing].val.i,
		   "enable-developer-extras", curconfig[Inspector].val.i,
		   "enable-dns-prefetching", curconfig[DNSPrefetch].val.i,
		   "enable-html5-database", curconfig[DiskCache].val.i,
		   "enable-html5-local-storage", curconfig[DiskCache].val.i,
		   "enable-javascript", curconfig[JavaScript].val.i,
		   "enable-site-specific-quirks", curconfig[SiteQuirks].val.i,
		   "enable-smooth-scrolling", curconfig[SmoothScrolling].val.i,
		   "enable-webgl", curconfig[WebGL].val.i,
		   "media-playback-requires-user-gesture", curconfig[MediaManualPlay].val.i,
		   NULL);
/* For more interesting settings, have a look at
 * http://webkitgtk.org/reference/webkit2gtk/stable/WebKitSettings.html */

		/* FACELIFT: pages that name no font get the embedded families
		 * (registered in the web process by the extension). Serif is
		 * left to the system; pages that name their fonts are untouched. */
		if (fontsdir) {
			webkit_settings_set_default_font_family(settings, URMA_FONT_SANS);
			webkit_settings_set_sans_serif_font_family(settings, URMA_FONT_SANS);
			webkit_settings_set_monospace_font_family(settings, URMA_FONT_MONO);
		}

		if (strcmp(fulluseragent, "")) {
			webkit_settings_set_user_agent(settings, fulluseragent);
		} else if (surfuseragent) {
			webkit_settings_set_user_agent_with_application_details(
			    settings, "Surf", VERSION);
		}
		useragent = webkit_settings_get_user_agent(settings);

		contentmanager = webkit_user_content_manager_new();

		if (curconfig[Ephemeral].val.i) {
			context = webkit_web_context_new_ephemeral();
		} else {
			context = webkit_web_context_new_with_website_data_manager(
			          webkit_website_data_manager_new(
			          "base-cache-directory", cachedir,
			          "base-data-directory", cachedir,
			          NULL));
		}

		cookiemanager = webkit_web_context_get_cookie_manager(context);

		/* TLS */
		webkit_website_data_manager_set_tls_errors_policy(
		    webkit_web_context_get_website_data_manager(context),
		    curconfig[StrictTLS].val.i ? WEBKIT_TLS_ERRORS_POLICY_FAIL :
		    WEBKIT_TLS_ERRORS_POLICY_IGNORE);
		/* disk cache */
		webkit_web_context_set_cache_model(context,
		    curconfig[DiskCache].val.i ? WEBKIT_CACHE_MODEL_WEB_BROWSER :
		    WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);

		/* Currently only works with text file to be compatible with curl */
		if (!curconfig[Ephemeral].val.i)
			webkit_cookie_manager_set_persistent_storage(cookiemanager,
			    cookiefile, WEBKIT_COOKIE_PERSISTENT_STORAGE_TEXT);
		/* cookie policy */
		webkit_cookie_manager_set_accept_policy(cookiemanager,
		    cookiepolicy_get());
		/* languages */
		webkit_web_context_set_preferred_languages(context,
		    curconfig[PreferredLanguages].val.v);
		/* Touching the spell checker loads the enchant brokers (and
		 * their missing-plugin warnings); only do it when it is on. */
		if (curconfig[SpellChecking].val.i) {
			webkit_web_context_set_spell_checking_languages(context,
			    curconfig[SpellLanguages].val.v);
			webkit_web_context_set_spell_checking_enabled(context, TRUE);
		}

		g_signal_connect(G_OBJECT(context), "download-started",
		                 G_CALLBACK(downloadstarted), c);
		g_signal_connect(G_OBJECT(context), "initialize-web-extensions",
		                 G_CALLBACK(initwebextensions), c);

		/* URMA: scheme handler, secure scheme, page network black hole. */
		urma_register(context, settings);

		v = g_object_new(WEBKIT_TYPE_WEB_VIEW,
		    "settings", settings,
		    "user-content-manager", contentmanager,
		    "web-context", context,
		    NULL);
	}

	g_signal_connect(G_OBJECT(v), "notify::estimated-load-progress",
			 G_CALLBACK(progresschanged), c);
	g_signal_connect(G_OBJECT(v), "notify::title",
			 G_CALLBACK(titlechanged), c);
	g_signal_connect(G_OBJECT(v), "button-release-event",
			 G_CALLBACK(buttonreleased), c);
	g_signal_connect(G_OBJECT(v), "scroll-event",
			 G_CALLBACK(scrollzoom), c);
	g_signal_connect(G_OBJECT(v), "close",
			G_CALLBACK(closeview), c);
	g_signal_connect(G_OBJECT(v), "create",
			 G_CALLBACK(createview), c);
	g_signal_connect(G_OBJECT(v), "decide-policy",
			 G_CALLBACK(decidepolicy), c);
	g_signal_connect(G_OBJECT(v), "insecure-content-detected",
			 G_CALLBACK(insecurecontent), c);
	g_signal_connect(G_OBJECT(v), "load-failed-with-tls-errors",
			 G_CALLBACK(loadfailedtls), c);
	g_signal_connect(G_OBJECT(v), "load-failed",
			 G_CALLBACK(loadfailed), c);
	g_signal_connect(G_OBJECT(v), "load-changed",
			 G_CALLBACK(loadchanged), c);
	g_signal_connect(G_OBJECT(v), "mouse-target-changed",
			 G_CALLBACK(mousetargetchanged), c);
	g_signal_connect(G_OBJECT(v), "permission-request",
			 G_CALLBACK(permissionrequested), c);
	g_signal_connect(G_OBJECT(v), "ready-to-show",
			 G_CALLBACK(showview), c);
	g_signal_connect(G_OBJECT(v), "web-process-terminated",
			 G_CALLBACK(webprocessterminated), c);

	c->context = context;
	c->settings = settings;

	setparameter(c, 0, DarkMode, &curconfig[DarkMode].val);

	return v;
}

/* The extension next to the executable wins over the install prefix, so a
 * build run from its own directory is complete without `make install`. */
char *
webextdir(void)
{
	char *exe = g_file_read_link("/proc/self/exe", NULL), *dir, *so;

	if (!exe)
		return g_strdup(WEBEXTDIR);
	dir = g_path_get_dirname(exe);
	g_free(exe);
	so = g_build_filename(dir, "webext-surf.so", NULL);
	if (!g_file_test(so, G_FILE_TEST_IS_REGULAR)) {
		g_free(dir);
		dir = g_strdup(WEBEXTDIR);
	}
	g_free(so);
	return dir;
}

void
initwebextensions(WebKitWebContext *wc, Client *c)
{
	char *dir = webextdir();

	webkit_web_context_set_web_extensions_directory(wc, dir);
	/* Where the embedded fonts were written; the extension registers that
	 * directory with fontconfig inside the web process. */
	webkit_web_context_set_web_extensions_initialization_user_data(wc,
	    g_variant_new_string(fontsdir ? fontsdir : ""));
	g_free(dir);
}

/* ---- URMA shell ---------------------------------------------------------
 * The engine only ever loads publication-form document URLs (origin per
 * (network, root TXID)); names are resolved here, before or instead of a
 * load, and the friendly address is kept for presentation only. */
void
urmaclearctx(Client *c)
{
	g_clear_pointer(&c->urmaname, g_free);
	g_clear_pointer(&c->urmanet, g_free);
	g_clear_pointer(&c->urmagenesis, g_free);
	g_clear_pointer(&c->urmatxid, g_free);
	g_clear_pointer(&c->urmadisp, g_free);
}

/* Resolve a name form (short or explicit registry) and load the publication
 * form. An explicit genesis that differs from the network's active slot asks
 * for the global switch first; declined means nothing is loaded. */
gboolean
urmaresolveload(Client *c, const UrmaAddr *ad, const char *tail)
{
	char txid[65], *url, *msg;
	const char *genesis;
	GError *err = NULL;

	if (ad->genesis[0]
	    && !urma_registry_switch_prompt(GTK_WINDOW(c->win), ad->net, ad->genesis)) {
		setstatus(c, "registry switch declined; nothing resolved");
		return FALSE;
	}
	genesis = urma_active_genesis(ad->net);
	setbadge(c, "busy", "resolving", "Resolving the name through the active registry");
	if (!urma_resolve(ad->net, ad->name, NULL, txid, &err)) {
		msg = g_strdup_printf("cannot resolve %s.%s: %s", ad->name, ad->net,
		    err ? err->message : "unknown error");
		setstatus(c, msg);
		setbadge(c, "error", "unresolved", msg);
		g_free(msg);
		g_clear_error(&err);
		return FALSE;
	}
	urmaclearctx(c);
	c->urmaname = g_strdup(ad->name);
	c->urmanet = g_strdup(ad->net);
	c->urmagenesis = g_strdup(genesis ? genesis : "");
	c->urmatxid = g_strdup(txid);
	url = urma_pub_uri(ad->net, txid, tail);
	setatom(c, AtomUri, url);
	webkit_web_view_load_uri(c->view, url);
	updatetitle(c);
	g_free(url);
	return TRUE;
}

/* What the address bar shows: the short form while the active registry maps
 * the tab's name to the loaded publication, else the publication form. */
const char *
displayuri(Client *c)
{
	g_free(c->urmadisp);
	c->urmadisp = urma_display_uri(geturi(c), c->urmanet, c->urmaname);
	return c->urmadisp;
}

/* Copy link (short form when truthful) / Copy link with DNS (explicit
 * genesis that produced the shown binding). */
void
copylink(Client *c, const Arg *a)
{
	char *link = a->i ?
	    urma_dns_uri(geturi(c), c->urmanet, c->urmaname, c->urmagenesis) :
	    g_strdup(displayuri(c));

	gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), link, -1);
	gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_PRIMARY), link, -1);
	setstatus(c, a->i ? "copied link with DNS" : "copied link");
	g_free(link);
}

/* Page details: publication, provenance, verification, name binding. */
void
urmadetails(Client *c, const Arg *a)
{
	char *text = urma_details(geturi(c), c->urmaname, c->urmagenesis);
	GtkWidget *d, *grid, *k, *v, *area, *head;
	gchar **lines;
	int i, row = 0;

	if (!text || !*text) {
		setstatus(c, "no URMA publication loaded");
		g_free(text);
		return;
	}
	/* FACELIFT: the same facts as before, laid out as a key/value sheet:
	 * dim keys, selectable monospace values, wrapped hex. */
	d = gtk_dialog_new_with_buttons("URMA details", GTK_WINDOW(c->win),
	    GTK_DIALOG_DESTROY_WITH_PARENT, "_Close", GTK_RESPONSE_CLOSE, NULL);
	gtk_style_context_add_class(gtk_widget_get_style_context(d), "urma-details");
	gtk_window_set_resizable(GTK_WINDOW(d), FALSE);
	area = gtk_dialog_get_content_area(GTK_DIALOG(d));
	head = gtk_label_new("Publication details");
	gtk_style_context_add_class(gtk_widget_get_style_context(head), "urma-details-title");
	gtk_label_set_xalign(GTK_LABEL(head), 0.0);
	gtk_box_pack_start(GTK_BOX(area), head, FALSE, FALSE, 0);
	grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 18);
	lines = g_strsplit(text, "\n", -1);
	for (i = 0; lines[i]; i++) {
		char *sep = strstr(lines[i], ": ");
		if (!*lines[i]) {
			gtk_grid_attach(GTK_GRID(grid), gtk_label_new(""), 0, row++, 2, 1);
			continue;
		}
		if (sep) {
			*sep = '\0';
			k = gtk_label_new(lines[i]);
			v = gtk_label_new(sep + 2);
		} else {
			k = gtk_label_new("");
			v = gtk_label_new(lines[i]);
		}
		gtk_style_context_add_class(gtk_widget_get_style_context(k), "urma-details-key");
		gtk_style_context_add_class(gtk_widget_get_style_context(v), "urma-details-value");
		gtk_label_set_xalign(GTK_LABEL(k), 0.0);
		gtk_label_set_xalign(GTK_LABEL(v), 0.0);
		gtk_widget_set_valign(k, GTK_ALIGN_START);
		gtk_label_set_selectable(GTK_LABEL(v), TRUE);
		gtk_label_set_line_wrap(GTK_LABEL(v), TRUE);
		gtk_label_set_line_wrap_mode(GTK_LABEL(v), PANGO_WRAP_WORD_CHAR);
		/* A wrapping label without a width cap asks GTK for a one-column
		 * layout and the dialog grows to the screen; cap it at a TXID. */
		gtk_label_set_width_chars(GTK_LABEL(v), 40);
		gtk_label_set_max_width_chars(GTK_LABEL(v), 66);
		gtk_widget_set_hexpand(v, TRUE);
		gtk_grid_attach(GTK_GRID(grid), k, 0, row, 1, 1);
		gtk_grid_attach(GTK_GRID(grid), v, 1, row++, 1, 1);
	}
	g_strfreev(lines);
	gtk_box_pack_start(GTK_BOX(area), grid, TRUE, TRUE, 0);
	g_signal_connect_swapped(d, "response", G_CALLBACK(gtk_widget_destroy), d);
	gtk_widget_show_all(d);
	g_free(text);
}

GtkWidget *
createview(WebKitWebView *v, WebKitNavigationAction *a, Client *c)
{
	Client *n;

	switch (webkit_navigation_action_get_navigation_type(a)) {
	case WEBKIT_NAVIGATION_TYPE_OTHER: /* fallthrough */
		/*
		 * popup windows of type “other” are almost always triggered
		 * by user gesture, so inverse the logic here
		 */
/* instead of this, compare destination uri to mouse-over uri for validating window */
		if (webkit_navigation_action_is_user_gesture(a))
			return NULL;
		FALLTHROUGH;
	case WEBKIT_NAVIGATION_TYPE_LINK_CLICKED: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_FORM_SUBMITTED: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_BACK_FORWARD: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_RELOAD: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_FORM_RESUBMITTED:
		n = newclient(c);
		break;
	default:
		return NULL;
	}

	return GTK_WIDGET(n->view);
}

gboolean
buttonreleased(GtkWidget *w, GdkEvent *e, Client *c)
{
	WebKitHitTestResultContext element;
	size_t i;

	element = webkit_hit_test_result_get_context(c->mousepos);

	for (i = 0; i < LENGTH(buttons); ++i) {
		if (element & buttons[i].target &&
		    e->button.button == buttons[i].button &&
		    CLEANMASK(e->button.state) == CLEANMASK(buttons[i].mask) &&
		    buttons[i].func) {
			buttons[i].func(c, &buttons[i].arg, c->mousepos);
			return buttons[i].stopevent;
		}
	}

	return FALSE;
}

GdkFilterReturn
processx(GdkXEvent *e, GdkEvent *event, gpointer d)
{
	Client *c = (Client *)d;
	XPropertyEvent *ev;
	Arg a;

	if (((XEvent *)e)->type == PropertyNotify) {
		ev = &((XEvent *)e)->xproperty;
		if (ev->state == PropertyNewValue) {
			if (ev->atom == atoms[AtomFind]) {
				find(c, NULL);

				return GDK_FILTER_REMOVE;
			} else if (ev->atom == atoms[AtomGo]) {
				a.v = getatom(c, AtomGo);
				loaduri(c, &a);

				return GDK_FILTER_REMOVE;
			}
		}
	}
	return GDK_FILTER_CONTINUE;
}

gboolean
winevent(GtkWidget *w, GdkEvent *e, Client *c)
{
	size_t i;

	switch (e->type) {
	case GDK_ENTER_NOTIFY:
		c->overtitle = c->targeturi;
		updatetitle(c);
		break;
	case GDK_KEY_PRESS:
		/* MODERNISE: while a text entry (address, find, settings) has
		 * focus it owns the keyboard; those entries handle Esc and
		 * Ctrl-L themselves. Everything else goes to the shell first,
		 * so a page never swallows a shell shortcut. */
		if (GTK_IS_ENTRY(gtk_window_get_focus(GTK_WINDOW(c->win))))
			return FALSE;
		if (!curconfig[KioskMode].val.i) {
			for (i = 0; i < LENGTH(keys); ++i) {
				if (gdk_keyval_to_lower(e->key.keyval) ==
				    keys[i].keyval &&
				    CLEANMASK(e->key.state) == keys[i].mod &&
				    keys[i].func) {
					updatewinid(c);
					keys[i].func(c, &(keys[i].arg));
					return TRUE;
				}
			}
		}
		FALLTHROUGH;
	case GDK_LEAVE_NOTIFY:
		c->overtitle = NULL;
		updatetitle(c);
		break;
	case GDK_WINDOW_STATE:
		if (e->window_state.changed_mask ==
		    GDK_WINDOW_STATE_FULLSCREEN)
			c->fullscreen = e->window_state.new_window_state &
			                GDK_WINDOW_STATE_FULLSCREEN;
		break;
	default:
		break;
	}

	return FALSE;
}

void
showview(WebKitWebView *v, Client *c)
{
	GdkRGBA bgcolor = { 0 };
	GdkWindow *gwin;

	c->finder = webkit_web_view_get_find_controller(c->view);
	c->inspector = webkit_web_view_get_inspector(c->view);

	c->pageid = webkit_web_view_get_page_id(c->view);
	c->win = createwindow(c);

	buildchrome(c);
	gtk_container_add(GTK_CONTAINER(c->win), c->box);
	gtk_widget_show_all(c->win);
	gtk_widget_hide(c->load);
	if (!showbar)
		gtk_widget_hide(c->bar);
	gtk_widget_grab_focus(GTK_WIDGET(c->view));

	gwin = gtk_widget_get_window(GTK_WIDGET(c->win));
	c->xid = gdk_x11_window_get_xid(gwin);
	updatewinid(c);
	if (showxid) {
		gdk_display_sync(gtk_widget_get_display(c->win));
		puts(winid);
		fflush(stdout);
	}

	if (curconfig[HideBackground].val.i)
		webkit_web_view_set_background_color(c->view, &bgcolor);

	if (!curconfig[KioskMode].val.i) {
		gdk_window_set_events(gwin, GDK_ALL_EVENTS_MASK);
		gdk_window_add_filter(gwin, processx, c);
	}

	if (curconfig[RunInFullscreen].val.i)
		togglefullscreen(c, NULL);

	if (curconfig[ZoomLevel].val.f != 1.0)
		webkit_web_view_set_zoom_level(c->view,
		                               curconfig[ZoomLevel].val.f);

	setatom(c, AtomFind, "");
	setatom(c, AtomUri, "about:blank");
}

/* MODERNISE (urma-surf): minimal inline chrome. One entry for the address,
 * one thin progress bar, one status line. Plain GTK, no new dependency and
 * no toolbar of buttons; keyboard stays the primary interface. */
void
entryactivate(GtkEntry *e, Client *c)
{
	Arg a = { .v = gtk_entry_get_text(e) };

	loaduri(c, &a);
	gtk_widget_grab_focus(GTK_WIDGET(c->view));
}

gboolean
entrykey(GtkWidget *w, GdkEventKey *ev, Client *c)
{
	if (ev->keyval == GDK_KEY_Escape) {
		gtk_entry_set_text(GTK_ENTRY(c->entry), displayuri(c));
		gtk_widget_grab_focus(GTK_WIDGET(c->view));
		return TRUE;
	}
	/* Ctrl-L inside the address re-selects everything, like a browser. */
	if ((ev->state & GDK_CONTROL_MASK) && gdk_keyval_to_lower(ev->keyval) == GDK_KEY_l) {
		gtk_editable_select_region(GTK_EDITABLE(c->entry), 0, -1);
		return TRUE;
	}
	if ((ev->state & GDK_CONTROL_MASK) && gdk_keyval_to_lower(ev->keyval) == GDK_KEY_f) {
		showfind(c, NULL);
		return TRUE;
	}
	return FALSE;
}

void
showaddress(Client *c, const Arg *a)
{
	gtk_widget_show(c->bar);
	gtk_entry_set_text(GTK_ENTRY(c->entry), displayuri(c));
	gtk_widget_grab_focus(c->entry);
	gtk_editable_select_region(GTK_EDITABLE(c->entry), 0, -1);
}

void
togglebar(Client *c, const Arg *a)
{
	if (gtk_widget_get_visible(c->bar)) {
		gtk_widget_hide(c->bar);
		gtk_widget_grab_focus(GTK_WIDGET(c->view));
	} else {
		showaddress(c, a);
	}
}

void
setstatus(Client *c, const char *text)
{
	if (text && *text) {
		gtk_label_set_text(GTK_LABEL(c->status), text);
		gtk_widget_show(c->status);
	} else {
		gtk_widget_hide(c->status);
	}
}

/* Transient zoom readout: it must not fight the link status, and it must not
 * linger on screen after the gesture is over. */
gboolean
clearzoomflash(gpointer data)
{
	Client *c = data;

	c->zoomflash = 0;
	if (!c->targeturi)
		setstatus(c, NULL);
	else
		setstatus(c, c->targeturi);
	return G_SOURCE_REMOVE;
}

void
flashzoom(Client *c)
{
	char msg[32];

	if (!c->status)
		return;
	if (c->zoomflash)
		g_source_remove(c->zoomflash);
	g_snprintf(msg, sizeof(msg), "zoom %.0f%%",
	    webkit_web_view_get_zoom_level(c->view) * 100);
	setstatus(c, msg);
	c->zoomflash = g_timeout_add(1200, clearzoomflash, c);
}

/* Ctrl+wheel zoom. Returning FALSE without Ctrl leaves ordinary scrolling
 * untouched; returning TRUE when we did zoom stops the view from also
 * scrolling, which is what makes it fire twice. */
gboolean
scrollzoom(GtkWidget *w, GdkEvent *e, Client *c)
{
	GdkScrollDirection dir;
	gdouble dx, dy;
	Arg a;

	if (!(e->scroll.state & GDK_CONTROL_MASK))
		return FALSE;

	if (gdk_event_get_scroll_direction(e, &dir)) {
		if (dir == GDK_SCROLL_UP)
			a.i = +1;
		else if (dir == GDK_SCROLL_DOWN)
			a.i = -1;
		else
			return FALSE;
	} else if (gdk_event_get_scroll_deltas(e, &dx, &dy)) {
		/* Smooth/touchpad events report deltas instead; ignore the
		 * many near-zero ones so one gesture is not many steps. */
		if (dy <= -0.1)
			a.i = +1;
		else if (dy >= 0.1)
			a.i = -1;
		else
			return TRUE;
	} else {
		return FALSE;
	}

	zoom(c, &a);
	return TRUE;
}

void
reloadstop(GtkButton *b, Client *c)
{
	if (c->loading)
		webkit_web_view_stop_loading(c->view);
	else
		webkit_web_view_reload(c->view);
	gtk_widget_grab_focus(GTK_WIDGET(c->view));
}

void
setloading(Client *c, int loading)
{
	c->loading = loading;
	if (!c->reload)
		return;
	gtk_button_set_image(GTK_BUTTON(c->reload), gtk_image_new_from_icon_name(
	    loading ? "process-stop-symbolic" : "view-refresh-symbolic",
	    GTK_ICON_SIZE_BUTTON));
	gtk_widget_set_tooltip_text(c->reload,
	    loading ? "Stop (Esc)" : "Reload (Ctrl-r)");
}

/* One word about what the address stands for, next to it. States: verified
 * (served from the re-verified store), busy (resolving / recovering), error
 * (refused, unresolved, failed), plain (local file). Empty hides it. */
void
setbadge(Client *c, const char *state, const char *text, const char *tip)
{
	GtkStyleContext *sc;
	const char *states[] = { "verified", "busy", "error", "plain" };
	guint i;

	if (!c->badge)
		return;
	sc = gtk_widget_get_style_context(c->badge);
	for (i = 0; i < LENGTH(states); i++)
		gtk_style_context_remove_class(sc, states[i]);
	if (!text || !*text) {
		gtk_widget_hide(c->badge);
		return;
	}
	gtk_style_context_add_class(sc, state);
	gtk_label_set_text(GTK_LABEL(c->badge), text);
	gtk_widget_set_tooltip_text(c->badge, tip);
	gtk_widget_show(c->badge);
}

void
brandclicked(GtkButton *b, Client *c)
{
	urmadetails(c, NULL);
}

/* GTK 3 has no :focus-within; mark the pill while its entry has focus. */
gboolean
pillfocus(GtkWidget *w, GdkEventFocus *ev, Client *c)
{
	GtkStyleContext *sc = gtk_widget_get_style_context(c->pill);

	if (ev->in)
		gtk_style_context_add_class(sc, "focused");
	else
		gtk_style_context_remove_class(sc, "focused");
	return FALSE;
}

/* ---- find bar ----------------------------------------------------------- */

void
showfind(Client *c, const Arg *a)
{
	gtk_widget_show(c->findbar);
	gtk_widget_grab_focus(c->findentry);
	gtk_editable_select_region(GTK_EDITABLE(c->findentry), 0, -1);
	if (*gtk_entry_get_text(GTK_ENTRY(c->findentry)))
		findchanged(GTK_EDITABLE(c->findentry), c);
}

void
hidefind(Client *c)
{
	if (!c->findbar || !gtk_widget_get_visible(c->findbar))
		return;
	webkit_find_controller_search_finish(c->finder);
	gtk_label_set_text(GTK_LABEL(c->findcount), "");
	gtk_widget_hide(c->findbar);
	gtk_widget_grab_focus(GTK_WIDGET(c->view));
}

void
findchanged(GtkEditable *e, Client *c)
{
	const char *s = gtk_entry_get_text(GTK_ENTRY(c->findentry));

	if (!*s) {
		webkit_find_controller_search_finish(c->finder);
		gtk_label_set_text(GTK_LABEL(c->findcount), "");
		return;
	}
	webkit_find_controller_search(c->finder, s, findopts, G_MAXUINT);
}

gboolean
findkey(GtkWidget *w, GdkEventKey *ev, Client *c)
{
	guint k = gdk_keyval_to_lower(ev->keyval);

	if (ev->keyval == GDK_KEY_Escape) {
		hidefind(c);
		return TRUE;
	}
	if (ev->keyval == GDK_KEY_Return || ev->keyval == GDK_KEY_KP_Enter) {
		if (ev->state & GDK_SHIFT_MASK)
			webkit_find_controller_search_previous(c->finder);
		else
			webkit_find_controller_search_next(c->finder);
		return TRUE;
	}
	if ((ev->state & GDK_CONTROL_MASK) && k == GDK_KEY_l) {
		showaddress(c, NULL);
		return TRUE;
	}
	if ((ev->state & GDK_CONTROL_MASK) && (k == GDK_KEY_f || k == GDK_KEY_g)) {
		gtk_editable_select_region(GTK_EDITABLE(c->findentry), 0, -1);
		return TRUE;
	}
	return FALSE;
}

static void
findprev(GtkButton *b, Client *c)
{
	webkit_find_controller_search_previous(c->finder);
}

static void
findnext(GtkButton *b, Client *c)
{
	webkit_find_controller_search_next(c->finder);
}

static void
findclose(GtkButton *b, Client *c)
{
	hidefind(c);
}

void
foundtext(WebKitFindController *f, guint count, Client *c)
{
	char msg[48];

	g_snprintf(msg, sizeof(msg), count == 1 ? "%u match" : "%u matches",
	    count == G_MAXUINT ? 0 : count);
	gtk_label_set_text(GTK_LABEL(c->findcount), msg);
}

void
failedtext(WebKitFindController *f, Client *c)
{
	gtk_label_set_text(GTK_LABEL(c->findcount), "no matches");
}

/* ---- settings ----------------------------------------------------------- */

void
gearclicked(GtkButton *b, Client *c)
{
	showsettings(c, NULL);
}

void
showsettings(Client *c, const Arg *a)
{
	GtkWidget *old;

	if (!c->popover) {
		c->popover = gtk_popover_new(c->gear);
		gtk_style_context_add_class(gtk_widget_get_style_context(c->popover),
		    "urma-settings");
		gtk_popover_set_position(GTK_POPOVER(c->popover), GTK_POS_BOTTOM);
	}
	if ((old = gtk_bin_get_child(GTK_BIN(c->popover))))
		gtk_widget_destroy(old);
	c->focuscombo = NULL;
	gtk_container_add(GTK_CONTAINER(c->popover), settingsbody(c));
	gtk_widget_show_all(gtk_bin_get_child(GTK_BIN(c->popover)));
	gtk_popover_popup(GTK_POPOVER(c->popover));
	if (c->focuscombo)
		gtk_widget_grab_focus(c->focuscombo);
}

gboolean
rebuildsettings(gpointer data)
{
	Client *c = data;

	if (c->popover && gtk_widget_get_visible(c->popover))
		showsettings(c, NULL);
	return G_SOURCE_REMOVE;
}

static GtkWidget *
sectionlabel(const char *text)
{
	GtkWidget *l = gtk_label_new(text);

	gtk_style_context_add_class(gtk_widget_get_style_context(l), "urma-section");
	gtk_label_set_xalign(GTK_LABEL(l), 0.0);
	return l;
}

static GtkWidget *
notelabel(const char *text)
{
	GtkWidget *l = gtk_label_new(text);

	gtk_style_context_add_class(gtk_widget_get_style_context(l), "urma-note");
	gtk_label_set_xalign(GTK_LABEL(l), 0.0);
	gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(l), 56);
	return l;
}

static GtkWidget *
switchrow(Client *c, const char *text, ParamName p)
{
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *l = gtk_label_new(text), *s = gtk_switch_new();

	gtk_label_set_xalign(GTK_LABEL(l), 0.0);
	gtk_switch_set_active(GTK_SWITCH(s), curconfig[p].val.i);
	g_object_set_data(G_OBJECT(s), "param", GINT_TO_POINTER(p));
	g_signal_connect(G_OBJECT(s), "state-set", G_CALLBACK(switchset), c);
	gtk_box_pack_start(GTK_BOX(row), l, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(row), s, FALSE, FALSE, 0);
	return row;
}

gboolean
switchset(GtkSwitch *s, gboolean state, Client *c)
{
	ParamName p = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(s), "param"));

	curconfig[p].val.i = state;
	setparameter(c, 1, p, &curconfig[p].val);
	return FALSE;
}

GtkWidget *
menuitem(const char *text, const char *accel, GCallback cb, Client *c)
{
	GtkWidget *b = gtk_button_new(), *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	GtkWidget *l = gtk_label_new(text), *k = gtk_label_new(accel);

	gtk_style_context_add_class(gtk_widget_get_style_context(b), "urma-item");
	gtk_style_context_add_class(gtk_widget_get_style_context(k), "urma-accel");
	gtk_label_set_xalign(GTK_LABEL(l), 0.0);
	gtk_box_pack_start(GTK_BOX(row), l, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(row), k, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(b), row);
	gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
	g_signal_connect_swapped(G_OBJECT(b), "clicked", cb, c);
	return b;
}

static void itemdetails(Client *c) { urmadetails(c, NULL); }
static void itemshortcuts(Client *c) { showshortcuts(c, NULL); }
static void itembar(Client *c) { togglebar(c, NULL); }

GtkWidget *
settingsbody(Client *c)
{
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4), *head, *t;
	char *ini = g_build_filename(g_get_home_dir(), ".surf", "urma-registries.ini", NULL);
	char *note;
	const char *const *nets;
	guint n, i;

	gtk_container_set_border_width(GTK_CONTAINER(box), 14);
	gtk_widget_set_size_request(box, 440, -1);

	head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_pack_start(GTK_BOX(head), urmabrand_image(c->win, 20), FALSE, FALSE, 0);
	t = gtk_label_new("Settings");
	gtk_style_context_add_class(gtk_widget_get_style_context(t), "urma-settings-title");
	gtk_box_pack_start(GTK_BOX(head), t, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), head, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(box), sectionlabel("NAME REGISTRY · URMA NAMES"), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), notelabel(
	    "Which registry resolves name addresses (urma://name.net/), per network. "
	    "Resolution is local, from an index scanned with the urma client; there is no "
	    "server. The choice is global for every window and saved in the registry file. "
	    "Publication addresses by TXID never need one."), FALSE, FALSE, 0);
	nets = urma_nets(&n);
	for (i = 0; i < n; i++)
		gtk_box_pack_start(GTK_BOX(box), registryrow(c, nets[i]), FALSE, FALSE, 2);
	note = g_strdup_printf("Saved in %s", ini);
	gtk_box_pack_start(GTK_BOX(box), notelabel(note), FALSE, FALSE, 0);
	g_free(note);
	g_free(ini);

	gtk_box_pack_start(GTK_BOX(box), sectionlabel("THIS WINDOW"), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), notelabel(
	    "Engine switches of this window; not saved. Changing one reloads the page."),
	    FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), switchrow(c, "JavaScript", JavaScript), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), switchrow(c, "Load images", LoadImages), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), switchrow(c, "Caret browsing", CaretBrowsing), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), switchrow(c, "Smooth scrolling", SmoothScrolling), FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(box), sectionlabel("MORE"), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), menuitem("Publication details", "Ctrl+Shift+D",
	    G_CALLBACK(itemdetails), c), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), menuitem("Keyboard shortcuts", "Ctrl+?",
	    G_CALLBACK(itemshortcuts), c), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), menuitem("Hide the address bar", "Ctrl+Shift+B",
	    G_CALLBACK(itembar), c), FALSE, FALSE, 0);
	return box;
}

/* One network: its combo of registries (none / known / other) and a status
 * line saying what the active one is and whether it is scanned locally. */
GtkWidget *
registryrow(Client *c, const char *net)
{
	GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	GtkWidget *l, *combo, *status;
	GArray *regs = urma_registries(net);
	const char *active = urma_active_genesis(net);
	const UrmaRegistry *on = NULL;
	char *dot = g_strdup_printf(".%s", net), *text = NULL;
	guint i;

	l = gtk_label_new(dot);
	gtk_style_context_add_class(gtk_widget_get_style_context(l), "urma-net");
	gtk_label_set_xalign(GTK_LABEL(l), 0.0);
	g_free(dot);

	combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "", "No registry");
	for (i = 0; i < regs->len; i++) {
		const UrmaRegistry *r = &g_array_index(regs, UrmaRegistry, i);
		char *item = g_strdup_printf("%s%s%.12s…  %s", r->title, *r->title ? "  " : "",
		    r->genesis, r->scanned ? "scanned" : "not scanned");
		gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), r->genesis, item);
		g_free(item);
		if (active && !strcmp(active, r->genesis))
			on = r;
	}
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "+", "Other genesis…");
	gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo), active ? active : "");
	g_object_set_data_full(G_OBJECT(combo), "net", g_strdup(net), g_free);
	g_object_set_data_full(G_OBJECT(combo), "previous", g_strdup(active ? active : ""), g_free);
	g_signal_connect(G_OBJECT(combo), "changed", G_CALLBACK(registrychanged), c);
	gtk_widget_set_hexpand(combo, TRUE);

	if (!active)
		text = g_strdup("No active registry: name addresses on this network fail until one is chosen.");
	else if (on && on->scanned)
		text = g_strdup_printf("Active: %s%s%s, scanned to height %" G_GINT64_FORMAT ", %u name%s.",
		    *on->title ? on->title : "registry", *on->mode ? ", " : "",
		    *on->mode ? on->mode : "", on->height, on->names, on->names == 1 ? "" : "s");
	else
		text = g_strdup_printf("Active but not scanned locally; names cannot resolve until: "
		    "urma names scan --genesis %s%s", active,
		    !strcmp(net, "tltc") ? " --testnet" : "");
	status = gtk_label_new(text);
	gtk_style_context_add_class(gtk_widget_get_style_context(status), "urma-regstatus");
	if (on && on->scanned)
		gtk_style_context_add_class(gtk_widget_get_style_context(status), "on");
	gtk_label_set_xalign(GTK_LABEL(status), 0.0);
	gtk_label_set_line_wrap(GTK_LABEL(status), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(status), 60);
	gtk_label_set_selectable(GTK_LABEL(status), TRUE);
	gtk_widget_set_can_focus(status, FALSE);
	/* Keyboard users land on the combo of the first network that has a
	 * registry; the rest is one Tab away. */
	if (!c->focuscombo && (active || regs->len))
		c->focuscombo = combo;
	g_free(text);
	g_array_free(regs, TRUE);

	gtk_box_pack_start(GTK_BOX(row), l, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row), combo, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(col), row, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(col), status, FALSE, FALSE, 0);
	return col;
}

void
registrychanged(GtkComboBox *box, Client *c)
{
	const char *id = gtk_combo_box_get_active_id(box);
	const char *net = g_object_get_data(G_OBJECT(box), "net");
	const char *previous = g_object_get_data(G_OBJECT(box), "previous");
	char *other;

	if (!id || !net)
		return;
	if (!strcmp(id, "+")) {
		other = otherregistry(c, net);
		if (other) {
			applyregistry(c, net, other);
			g_free(other);
		} else {
			gtk_combo_box_set_active_id(box, previous);
		}
		return;
	}
	if (!strcmp(id, previous))
		return;
	applyregistry(c, net, id);
}

/* The settings path is itself an explicit user action, so no switch prompt;
 * the consequences stated in the prompt are stated in the panel instead. */
void
applyregistry(Client *c, const char *net, const char *genesis)
{
	char *msg;

	urma_set_active_genesis(net, genesis);
	refreshaddresses();
	msg = *genesis ? g_strdup_printf("active registry for .%s: %.12s… (%s)", net, genesis,
	          urma_registry_label(net, genesis))
	    : g_strdup_printf("no active registry for .%s; name addresses fail, TXID addresses work", net);
	setstatus(c, msg);
	g_free(msg);
	g_idle_add(rebuildsettings, c);
}

/* Contract §9: after a switch a bar keeps the short form only while the
 * active registry still maps the tab's name to the loaded publication. */
void
refreshaddresses(void)
{
	Client *k;

	for (k = clients; k; k = k->next)
		if (k->entry && !gtk_widget_has_focus(k->entry))
			gtk_entry_set_text(GTK_ENTRY(k->entry), displayuri(k));
}

char *
otherregistry(Client *c, const char *net)
{
	GtkWidget *d, *area, *entry, *l;
	char *text = NULL, *host;
	UrmaAddr ad;

	d = gtk_dialog_new_with_buttons("Other registry", GTK_WINDOW(c->win),
	    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
	    "_Cancel", GTK_RESPONSE_CANCEL, "_Use", GTK_RESPONSE_ACCEPT, NULL);
	gtk_style_context_add_class(gtk_widget_get_style_context(d), "urma-details");
	gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_ACCEPT);
	area = gtk_dialog_get_content_area(GTK_DIALOG(d));
	l = gtk_label_new(NULL);
	host = g_strdup_printf("Genesis TXID of a registry on .%s (64 hex characters). "
	    "It becomes the active registry for that network; the index must exist "
	    "locally for names to resolve.", net);
	gtk_label_set_text(GTK_LABEL(l), host);
	g_free(host);
	gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(l), 60);
	gtk_label_set_xalign(GTK_LABEL(l), 0.0);
	entry = gtk_entry_new();
	gtk_entry_set_width_chars(GTK_ENTRY(entry), 66);
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_style_context_add_class(gtk_widget_get_style_context(entry), "urma-field");
	gtk_box_pack_start(GTK_BOX(area), l, FALSE, FALSE, 6);
	gtk_box_pack_start(GTK_BOX(area), entry, FALSE, FALSE, 6);
	gtk_widget_show_all(d);
	if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT) {
		char *typed = g_ascii_strdown(g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)))), -1);
		host = g_strdup_printf("%s.%s", typed, net);
		/* Same grammar as an address: a 64-hex label before the suffix. */
		if (urma_parse_host(host, &ad) && ad.kind == URMA_ADDR_PUB)
			text = g_strdup(ad.txid);
		else
			setstatus(c, "not a registry genesis: 64 lowercase hex characters expected");
		g_free(host);
		g_free(typed);
	}
	gtk_widget_destroy(d);
	return text;
}

/* ---- shortcuts sheet ----------------------------------------------------- */

void
showshortcuts(Client *c, const Arg *a)
{
	static const char *rows[][2] = {
		{ "Ctrl+L · F6", "Focus the address, text selected" },
		{ "Enter · Esc", "In the address: load · revert" },
		{ "Alt+← · Ctrl+[ · Ctrl+H", "Back" },
		{ "Alt+→ · Ctrl+]", "Forward" },
		{ "Ctrl+R · F5", "Reload" },
		{ "Ctrl+Shift+R · Ctrl+F5", "Reload, bypass cache" },
		{ "Esc · Ctrl+C", "Stop loading; close the find bar" },
		{ "Ctrl+F · Ctrl+/", "Find in page: Enter next, Shift+Enter previous, Esc close" },
		{ "Ctrl+N · Ctrl+Shift+N", "Next · previous match" },
		{ "Ctrl++ · Ctrl+= · Ctrl+wheel", "Zoom in" },
		{ "Ctrl+- · Ctrl+wheel", "Zoom out" },
		{ "Ctrl+0", "Zoom 100%" },
		{ "Ctrl+,", "Settings" },
		{ "Ctrl+?", "This sheet" },
		{ "Ctrl+Shift+D", "Publication details" },
		{ "Ctrl+Shift+Y · Ctrl+Shift+U", "Copy link · copy link with registry" },
		{ "Ctrl+Y · Ctrl+P", "Copy page address · open the selected text as address" },
		{ "Ctrl+Shift+B", "Show or hide the address bar" },
		{ "F11", "Fullscreen" },
		{ "Ctrl+Shift+P", "Print" },
		{ "Ctrl+T", "Certificate of the page (TLS only)" },
		{ "Ctrl+Shift+O", "Web inspector, when enabled" },
		{ "Ctrl+J · K · Space · B", "Scroll down · up · page down · page up" },
		{ "Ctrl+Shift+S · I · C · M · T · A", "Toggle JavaScript · images · caret · user style · strict TLS · cookie policy" },
		{ "Middle click · Ctrl+click", "Open the link in a new window" },
	};
	GtkWidget *d, *area, *head, *grid, *k, *v;
	guint i;

	d = gtk_dialog_new_with_buttons("Keyboard shortcuts", GTK_WINDOW(c->win),
	    GTK_DIALOG_DESTROY_WITH_PARENT, "_Close", GTK_RESPONSE_CLOSE, NULL);
	gtk_style_context_add_class(gtk_widget_get_style_context(d), "urma-details");
	gtk_window_set_resizable(GTK_WINDOW(d), FALSE);
	area = gtk_dialog_get_content_area(GTK_DIALOG(d));
	head = gtk_label_new("Keyboard shortcuts");
	gtk_style_context_add_class(gtk_widget_get_style_context(head), "urma-details-title");
	gtk_label_set_xalign(GTK_LABEL(head), 0.0);
	gtk_box_pack_start(GTK_BOX(area), head, FALSE, FALSE, 0);
	grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 22);
	for (i = 0; i < LENGTH(rows); i++) {
		k = gtk_label_new(rows[i][0]);
		v = gtk_label_new(rows[i][1]);
		gtk_style_context_add_class(gtk_widget_get_style_context(k), "urma-key");
		gtk_label_set_xalign(GTK_LABEL(k), 0.0);
		gtk_label_set_xalign(GTK_LABEL(v), 0.0);
		gtk_label_set_line_wrap(GTK_LABEL(v), TRUE);
		gtk_label_set_max_width_chars(GTK_LABEL(v), 52);
		gtk_grid_attach(GTK_GRID(grid), k, 0, i, 1, 1);
		gtk_grid_attach(GTK_GRID(grid), v, 1, i, 1, 1);
	}
	gtk_box_pack_start(GTK_BOX(area), grid, TRUE, TRUE, 0);
	g_signal_connect_swapped(d, "response", G_CALLBACK(gtk_widget_destroy), d);
	gtk_widget_show_all(d);
}

/* Recovery of a publication that is not yet in the store runs in a CLI child;
 * every window whose current address is that publication shows it. */
void
fetchnotify(UrmaFetchEvent ev, const char *net, const char *txid,
            const char *detail, gpointer user)
{
	Client *c;
	UrmaAddr ad;
	char *msg;

	for (c = clients; c; c = c->next) {
		if (!c->badge || !urma_parse_uri(geturi(c), &ad, NULL)
		    || ad.kind != URMA_ADDR_PUB || strcmp(ad.txid, txid)
		    || strcmp(ad.net, net))
			continue;
		switch (ev) {
		case URMA_FETCH_STARTED:
			setbadge(c, "busy", "recovering",
			    "Recovering the publication from the chain (urma web fetch); "
			    "nothing renders before the whole declared set is verified");
			gtk_widget_show(c->spin);
			gtk_spinner_start(GTK_SPINNER(c->spin));
			setstatus(c, "recovering publication from the chain…");
			break;
		case URMA_FETCH_DONE:
			gtk_spinner_stop(GTK_SPINNER(c->spin));
			gtk_widget_hide(c->spin);
			setstatus(c, "recovered and verified; rendering");
			break;
		case URMA_FETCH_FAILED:
			gtk_spinner_stop(GTK_SPINNER(c->spin));
			gtk_widget_hide(c->spin);
			msg = g_strdup_printf("recovery failed: %s", detail ? detail : "unknown");
			setbadge(c, "error", "unavailable", msg);
			setstatus(c, msg);
			g_free(msg);
			break;
		}
	}
}

void
goback(GtkButton *b, Client *c)
{
	webkit_web_view_go_back(c->view);
	gtk_widget_grab_focus(GTK_WIDGET(c->view));
}

void
goforward(GtkButton *b, Client *c)
{
	webkit_web_view_go_forward(c->view);
	gtk_widget_grab_focus(GTK_WIDGET(c->view));
}

GtkWidget *
navbutton(const char *icon, const char *tip, GCallback cb, Client *c)
{
	GtkWidget *b = gtk_button_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON);

	gtk_style_context_add_class(gtk_widget_get_style_context(b),
	    "urma-nav");
	gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
	gtk_widget_set_tooltip_text(b, tip);
	gtk_widget_set_focus_on_click(b, FALSE);
	gtk_widget_set_sensitive(b, FALSE);
	g_signal_connect(G_OBJECT(b), "clicked", cb, c);
	return b;
}

/* Drive the buttons from the real history, not from a guess. */
void
syncnav(Client *c)
{
	if (!c->back)
		return;
	gtk_widget_set_sensitive(c->back,
	    webkit_web_view_can_go_back(c->view));
	gtk_widget_set_sensitive(c->forward,
	    webkit_web_view_can_go_forward(c->view));
}

void
buildchrome(Client *c)
{
	GtkCssProvider *css;
	/* FACELIFT: the chrome carries the URMA palette on its own (ink,
	 * paper, lime) so it reads the same under any GTK theme, and the
	 * embedded families so it needs no font package. Everything stays
	 * one header row: brand, three nav buttons, the address pill with its
	 * verification badge. The progress bar and status line float over the
	 * page and never take layout space. */
	const char *style =
	    ".urma-bar { background: #0f1512; padding: 6px 8px;"
	    "  border-bottom: 1px solid #1f2b25; }"
	    "button.urma-brand { font-family: \"" URMA_FONT_SANS "\", sans-serif;"
	    "  font-weight: 800; font-size: 11pt; letter-spacing: 0.1em;"
	    "  color: #d6fa52; padding: 0 8px; margin-right: 6px; min-height: 28px;"
	    "  background: none; border: none; box-shadow: none; outline: none; }"
	    "button.urma-brand:hover { background: #1f2b25; border-radius: 6px; }"
	    "button.urma-nav { min-width: 28px; min-height: 28px; padding: 0 5px;"
	    "  margin: 0 1px; border: none; border-radius: 6px; box-shadow: none;"
	    "  background: none; color: #ecebe3; -gtk-icon-shadow: none; }"
	    "button.urma-nav:hover:not(:disabled) { background: #1f2b25; }"
	    "button.urma-nav:disabled { color: #5b655e; -gtk-icon-effect: none; }"
	    "box.urma-pill { background: #17211c; border: 1px solid #1f2b25;"
	    "  border-radius: 8px; margin-left: 6px; min-height: 30px; }"
	    "box.urma-pill.focused { border-color: #d6fa52; }"
	    "entry.urma-address { font-family: \"" URMA_FONT_MONO "\", monospace;"
	    "  font-size: 10.5pt; color: #ecebe3; caret-color: #d6fa52;"
	    "  background: none; border: none; box-shadow: none; outline: none;"
	    "  padding: 0 10px; min-height: 28px; }"
	    "entry.urma-address selection { background: #d6fa52; color: #0f1512; }"
	    "label.urma-badge { font-family: \"" URMA_FONT_SANS "\", sans-serif;"
	    "  font-size: 8.5pt; font-weight: 700; letter-spacing: 0.06em;"
	    "  padding: 1px 8px; margin: 5px 5px 5px 2px; border-radius: 5px;"
	    "  color: #98a49b; background: #0f1512; }"
	    "label.urma-badge.verified { color: #0f1512; background: #d6fa52; }"
	    "label.urma-badge.busy { color: #0f1512; background: #9db83a; }"
	    "label.urma-badge.error { color: #ecebe3; background: #8a2f2f; }"
	    "label.urma-badge.plain { color: #98a49b; background: #0f1512; }"
	    "spinner.urma-spin { color: #d6fa52; margin: 0 2px 0 8px;"
	    "  min-width: 14px; min-height: 14px; }"
	    "progressbar.urma-load, progressbar.urma-load trough,"
	    "progressbar.urma-load progress { min-height: 3px; border: none;"
	    "  border-radius: 0; }"
	    "progressbar.urma-load trough { background: transparent; }"
	    "progressbar.urma-load progress { background: #d6fa52; }"
	    "label.urma-status { font-family: \"" URMA_FONT_MONO "\", monospace;"
	    "  font-size: 9pt; padding: 3px 10px; background: #0f1512;"
	    "  color: #ecebe3; border-top: 1px solid #1f2b25;"
	    "  border-right: 1px solid #1f2b25; border-top-right-radius: 6px; }"
	    /* Details sheet: paper, ink, lime rule; keys dim, values mono. */
	    ".urma-details, .urma-details .dialog-vbox { background: #ecebe3;"
	    "  color: #0f1512; }"
	    ".urma-details .dialog-vbox { padding: 18px 22px 10px; }"
	    ".urma-details label { font-family: \"" URMA_FONT_SANS "\", sans-serif;"
	    "  color: #0f1512; }"
	    "label.urma-details-title { font-size: 15pt; font-weight: 700;"
	    "  letter-spacing: -0.01em; padding-bottom: 10px; margin-bottom: 14px;"
	    "  border-bottom: 3px solid #d6fa52; }"
	    "label.urma-details-key { font-size: 9pt; font-weight: 700;"
	    "  letter-spacing: 0.06em; color: #5b655e; padding-top: 2px; }"
	    "label.urma-details-value { font-family: \"" URMA_FONT_MONO "\", monospace;"
	    "  font-size: 9.5pt; }"
	    ".urma-details button { font-family: \"" URMA_FONT_SANS "\", sans-serif;"
	    "  background: #0f1512; color: #ecebe3; border: none; border-radius: 6px;"
	    "  padding: 4px 16px; margin: 6px 0 0; box-shadow: none; }"
	    ".urma-details button:hover { background: #1f2b25; }"
	    /* Buttons on the sheet keep paper text under any theme; the label
	     * rule above must not reach them. */
	    ".urma-details button label { color: #ecebe3; }"
	    ".urma-details button:disabled { background: #98a49b; }"
	    ".urma-details button:disabled label { color: #ecebe3; }"
	    /* A text field on the paper sheet: light well, ink text, lime focus. */
	    "entry.urma-field { font-family: \"" URMA_FONT_MONO "\", monospace;"
	    "  font-size: 10pt; color: #0f1512; caret-color: #0f1512;"
	    "  background: #ffffff; border: 1px solid #98a49b; border-radius: 6px;"
	    "  padding: 4px 10px; min-height: 28px; box-shadow: none; }"
	    "entry.urma-field:focus { border-color: #9db83a; }"
	    "entry.urma-field selection { background: #d6fa52; color: #0f1512; }"
	    /* Brand button holds an image and a label now. */
	    "button.urma-brand label { color: #d6fa52; }"
	    "button.urma-gear { margin-left: 6px; }"
	    /* Find bar, floating under the progress line at the right. */
	    "box.urma-find { background: #0f1512; border: 1px solid #1f2b25;"
	    "  border-top: none; border-radius: 0 0 8px 8px; padding: 5px 6px;"
	    "  margin-right: 16px; }"
	    "box.urma-find entry { font-family: \"" URMA_FONT_MONO "\", monospace;"
	    "  font-size: 10pt; color: #ecebe3; caret-color: #d6fa52;"
	    "  background: #17211c; border: 1px solid #1f2b25; border-radius: 6px;"
	    "  padding: 0 8px; min-height: 26px; min-width: 220px; box-shadow: none; }"
	    "box.urma-find entry:focus { border-color: #d6fa52; }"
	    "box.urma-find entry selection { background: #d6fa52; color: #0f1512; }"
	    "label.urma-find-count { font-family: \"" URMA_FONT_SANS "\", sans-serif;"
	    "  font-size: 9pt; color: #98a49b; padding: 0 8px; }"
	    /* Settings popover: the paper sheet again. */
	    "popover.urma-settings { background: #ecebe3; border: 1px solid #1f2b25;"
	    "  border-radius: 8px; padding: 0; }"
	    "popover.urma-settings label { font-family: \"" URMA_FONT_SANS "\", sans-serif;"
	    "  color: #0f1512; }"
	    "label.urma-settings-title { font-size: 13pt; font-weight: 700;"
	    "  letter-spacing: -0.01em; }"
	    "label.urma-section { font-size: 8pt; font-weight: 700; letter-spacing: 0.14em;"
	    "  color: #5b655e; margin-top: 12px; }"
	    "label.urma-note { font-size: 9pt; color: #5b655e; }"
	    "label.urma-net { font-family: \"" URMA_FONT_MONO "\", monospace;"
	    "  font-weight: 700; font-size: 10pt; min-width: 48px; }"
	    "label.urma-regstatus { font-size: 8.5pt; color: #5b655e; margin-left: 58px; }"
	    "label.urma-regstatus.on { color: #3b4a1a; }"
	    "popover.urma-settings combobox button { font-family: \"" URMA_FONT_MONO "\", monospace;"
	    "  font-size: 9pt; }"
	    "popover.urma-settings switch { margin: 2px 0; }"
	    "popover.urma-settings switch:checked { background: #d6fa52; border-color: #9db83a; }"
	    "popover.urma-settings switch:checked slider { border-color: #9db83a; }"
	    "button.urma-item { background: none; border: none; box-shadow: none;"
	    "  padding: 5px 8px; border-radius: 6px; outline: none; }"
	    "button.urma-item:hover { background: #e2e3d8; }"
	    "label.urma-accel { font-family: \"" URMA_FONT_MONO "\", monospace;"
	    "  font-size: 9pt; color: #5b655e; }"
	    "label.urma-key { font-family: \"" URMA_FONT_MONO "\", monospace;"
	    "  font-size: 9.5pt; color: #0f1512; }";

	css = gtk_css_provider_new();
	gtk_css_provider_load_from_data(css, style, -1, NULL);
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
	    GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(css);

	c->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	/* Brand: the original mark (embedded raster, HiDPI-aware) and the
	 * wordmark; doubles as the way into the publication details. */
	{
		GtkWidget *hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
		gtk_box_pack_start(GTK_BOX(hb), urmabrand_image(c->win, 22), FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(hb), gtk_label_new("URMA"), FALSE, FALSE, 0);
		c->brand = gtk_button_new();
		gtk_container_add(GTK_CONTAINER(c->brand), hb);
	}
	gtk_style_context_add_class(gtk_widget_get_style_context(c->brand),
	    "urma-brand");
	gtk_button_set_relief(GTK_BUTTON(c->brand), GTK_RELIEF_NONE);
	gtk_widget_set_focus_on_click(c->brand, FALSE);
	gtk_widget_set_tooltip_text(c->brand,
	    "Publication details (Ctrl-Shift-D)");
	g_signal_connect(G_OBJECT(c->brand), "clicked",
	                 G_CALLBACK(brandclicked), c);

	/* Back/forward, bound to the real WebKit history below. */
	c->back = navbutton("go-previous-symbolic", "Back (Ctrl-h)",
	    G_CALLBACK(goback), c);
	c->forward = navbutton("go-next-symbolic", "Forward (Ctrl-l)",
	    G_CALLBACK(goforward), c);
	/* One button that reloads, and stops while a load is running: the
	 * state is already known, so it costs nothing to show it. */
	c->reload = navbutton("view-refresh-symbolic", "Reload (Ctrl-r)",
	    G_CALLBACK(reloadstop), c);
	gtk_widget_set_sensitive(c->reload, TRUE);

	c->entry = gtk_entry_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(c->entry),
	    "urma-address");
	gtk_entry_set_has_frame(GTK_ENTRY(c->entry), FALSE);
	gtk_entry_set_placeholder_text(GTK_ENTRY(c->entry),
	    "urma://name.tltc  or  urma://<root txid>.tltc");
	g_signal_connect(G_OBJECT(c->entry), "activate",
	                 G_CALLBACK(entryactivate), c);
	g_signal_connect(G_OBJECT(c->entry), "key-press-event",
	                 G_CALLBACK(entrykey), c);
	g_signal_connect(G_OBJECT(c->entry), "focus-in-event",
	                 G_CALLBACK(pillfocus), c);
	g_signal_connect(G_OBJECT(c->entry), "focus-out-event",
	                 G_CALLBACK(pillfocus), c);

	c->spin = gtk_spinner_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(c->spin),
	    "urma-spin");
	gtk_widget_set_no_show_all(c->spin, TRUE);

	c->badge = gtk_label_new(NULL);
	gtk_style_context_add_class(gtk_widget_get_style_context(c->badge),
	    "urma-badge");
	gtk_widget_set_valign(c->badge, GTK_ALIGN_CENTER);
	gtk_widget_set_no_show_all(c->badge, TRUE);

	c->pill = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(c->pill),
	    "urma-pill");
	gtk_box_pack_start(GTK_BOX(c->pill), c->spin, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(c->pill), c->entry, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(c->pill), c->badge, FALSE, FALSE, 0);

	/* Settings: one gear, a popover built fresh on every opening. */
	c->gear = navbutton("emblem-system-symbolic", "Settings (Ctrl-,)",
	    G_CALLBACK(gearclicked), c);
	gtk_style_context_add_class(gtk_widget_get_style_context(c->gear),
	    "urma-gear");
	gtk_widget_set_sensitive(c->gear, TRUE);

	c->bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(c->bar),
	    "urma-bar");
	gtk_box_pack_start(GTK_BOX(c->bar), c->brand, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(c->bar), c->back, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(c->bar), c->forward, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(c->bar), c->reload, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(c->bar), c->pill, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(c->bar), c->gear, FALSE, FALSE, 0);

	/* Find bar: floats at the top-right of the page, zero layout cost. */
	{
		GtkWidget *prev, *next, *close;

		c->findentry = gtk_entry_new();
		gtk_entry_set_has_frame(GTK_ENTRY(c->findentry), FALSE);
		gtk_entry_set_placeholder_text(GTK_ENTRY(c->findentry), "Find in page");
		g_signal_connect(G_OBJECT(c->findentry), "changed",
		                 G_CALLBACK(findchanged), c);
		g_signal_connect(G_OBJECT(c->findentry), "key-press-event",
		                 G_CALLBACK(findkey), c);
		c->findcount = gtk_label_new(NULL);
		gtk_style_context_add_class(gtk_widget_get_style_context(c->findcount),
		    "urma-find-count");
		prev = navbutton("go-up-symbolic", "Previous match (Shift-Enter)",
		    G_CALLBACK(findprev), c);
		next = navbutton("go-down-symbolic", "Next match (Enter)",
		    G_CALLBACK(findnext), c);
		close = navbutton("window-close-symbolic", "Close (Esc)",
		    G_CALLBACK(findclose), c);
		gtk_widget_set_sensitive(prev, TRUE);
		gtk_widget_set_sensitive(next, TRUE);
		gtk_widget_set_sensitive(close, TRUE);

		c->findbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
		gtk_style_context_add_class(gtk_widget_get_style_context(c->findbar),
		    "urma-find");
		gtk_box_pack_start(GTK_BOX(c->findbar), c->findentry, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(c->findbar), c->findcount, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(c->findbar), prev, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(c->findbar), next, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(c->findbar), close, FALSE, FALSE, 0);
		gtk_widget_set_halign(c->findbar, GTK_ALIGN_END);
		gtk_widget_set_valign(c->findbar, GTK_ALIGN_START);
		gtk_widget_set_no_show_all(c->findbar, TRUE);
		gtk_widget_show_all(c->findentry);
		gtk_widget_show(c->findcount);
		gtk_widget_show_all(prev);
		gtk_widget_show_all(next);
		gtk_widget_show_all(close);
		g_signal_connect(G_OBJECT(c->finder), "found-text",
		                 G_CALLBACK(foundtext), c);
		g_signal_connect(G_OBJECT(c->finder), "failed-to-find-text",
		                 G_CALLBACK(failedtext), c);
	}

	c->load = gtk_progress_bar_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(c->load),
	    "urma-load");
	gtk_widget_set_valign(c->load, GTK_ALIGN_START);
	gtk_widget_set_no_show_all(c->load, TRUE);

	c->status = gtk_label_new(NULL);
	gtk_style_context_add_class(gtk_widget_get_style_context(c->status),
	    "urma-status");
	gtk_label_set_xalign(GTK_LABEL(c->status), 0.0);
	gtk_label_set_ellipsize(GTK_LABEL(c->status), PANGO_ELLIPSIZE_MIDDLE);
	/* FIX (urma-surf): the status label used to be packed into the box, so
	 * showing it on hover resized the web view and relaid out the whole
	 * page — the "jumps from top to bottom, flickers" glitch. As an
	 * overlay child it floats above the view and costs zero layout. */
	gtk_widget_set_halign(c->status, GTK_ALIGN_START);
	gtk_widget_set_valign(c->status, GTK_ALIGN_END);
	gtk_widget_set_no_show_all(c->status, TRUE);

	c->overlay = gtk_overlay_new();
	gtk_container_add(GTK_CONTAINER(c->overlay), GTK_WIDGET(c->view));
	gtk_overlay_add_overlay(GTK_OVERLAY(c->overlay), c->status);
	gtk_overlay_add_overlay(GTK_OVERLAY(c->overlay), c->findbar);
	gtk_overlay_add_overlay(GTK_OVERLAY(c->overlay), c->load);

	gtk_box_pack_start(GTK_BOX(c->box), c->bar, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(c->box), c->overlay, TRUE, TRUE, 0);
}

GtkWidget *
createwindow(Client *c)
{
	char *wmstr;
	GtkWidget *w;

	if (embed) {
		w = gtk_plug_new(embed);
	} else {
		w = gtk_window_new(GTK_WINDOW_TOPLEVEL);

		/* WM_CLASS comes from g_set_prgname / gdk_set_program_class in
		 * setup(); the per-window setter is deprecated. */
		wmstr = g_strdup_printf("%s[%"PRIu64"]", "Surf", c->pageid);
		gtk_window_set_role(GTK_WINDOW(w), wmstr);
		g_free(wmstr);

		gtk_window_set_default_size(GTK_WINDOW(w), winsize[0], winsize[1]);
		/* FACELIFT: the URMA mark as window icon, every embedded size. */
		{
			GList *icons = urmabrand_icons();
			gtk_window_set_icon_list(GTK_WINDOW(w), icons);
			g_list_free_full(icons, g_object_unref);
		}
	}

	g_signal_connect(G_OBJECT(w), "destroy",
	                 G_CALLBACK(destroywin), c);
	g_signal_connect(G_OBJECT(w), "enter-notify-event",
	                 G_CALLBACK(winevent), c);
	g_signal_connect(G_OBJECT(w), "key-press-event",
	                 G_CALLBACK(winevent), c);
	g_signal_connect(G_OBJECT(w), "leave-notify-event",
	                 G_CALLBACK(winevent), c);
	g_signal_connect(G_OBJECT(w), "window-state-event",
	                 G_CALLBACK(winevent), c);

	return w;
}

gboolean
loadfailedtls(WebKitWebView *v, gchar *uri, GTlsCertificate *cert,
              GTlsCertificateFlags err, Client *c)
{
	GString *errmsg = g_string_new(NULL);
	gchar *html, *pem;

	c->failedcert = g_object_ref(cert);
	c->tlserr = err;
	c->errorpage = 1;
	c->errbadge = 1;
	setbadge(c, "error", "untrusted", "TLS validation failed");

	if (err & G_TLS_CERTIFICATE_UNKNOWN_CA)
		g_string_append(errmsg,
		    "The signing certificate authority is not known.<br>");
	if (err & G_TLS_CERTIFICATE_BAD_IDENTITY)
		g_string_append(errmsg,
		    "The certificate does not match the expected identity "
		    "of the site that it was retrieved from.<br>");
	if (err & G_TLS_CERTIFICATE_NOT_ACTIVATED)
		g_string_append(errmsg,
		    "The certificate's activation time "
		    "is still in the future.<br>");
	if (err & G_TLS_CERTIFICATE_EXPIRED)
		g_string_append(errmsg, "The certificate has expired.<br>");
	if (err & G_TLS_CERTIFICATE_REVOKED)
		g_string_append(errmsg,
		    "The certificate has been revoked according to "
		    "the GTlsConnection's certificate revocation list.<br>");
	if (err & G_TLS_CERTIFICATE_INSECURE)
		g_string_append(errmsg,
		    "The certificate's algorithm is considered insecure.<br>");
	if (err & G_TLS_CERTIFICATE_GENERIC_ERROR)
		g_string_append(errmsg,
		    "Some error occurred validating the certificate.<br>");

	g_object_get(cert, "certificate-pem", &pem, NULL);
	html = g_strdup_printf("<p>Could not validate TLS for “%s”<br>%s</p>"
	                       "<p>You can inspect the following certificate "
	                       "with Ctrl-t (default keybinding).</p>"
	                       "<p><pre>%s</pre></p>", uri, errmsg->str, pem);
	g_free(pem);
	g_string_free(errmsg, TRUE);

	webkit_web_view_load_alternate_html(c->view, html, uri, NULL);
	g_free(html);

	return TRUE;
}

void
loadchanged(WebKitWebView *v, WebKitLoadEvent e, Client *c)
{
	const char *uri = geturi(c);

	switch (e) {
	case WEBKIT_LOAD_STARTED:
		setatom(c, AtomUri, uri);
		setloading(c, 1);
		c->title = uri;
		c->https = c->insecure = 0;
		seturiparameters(c, uri, loadtransient);
		/* The badge of a failed load survives the alternate error page's
		 * own load cycle and ends with the next real navigation. */
		if (!c->errorpage)
			c->errbadge = 0;
		if (c->errorpage)
			c->errorpage = 0;
		else
			g_clear_object(&c->failedcert);
		break;
	case WEBKIT_LOAD_REDIRECTED:
		setatom(c, AtomUri, uri);
		c->title = uri;
		seturiparameters(c, uri, loadtransient);
		break;
	case WEBKIT_LOAD_COMMITTED:
		setatom(c, AtomUri, uri);
		c->title = uri;
		seturiparameters(c, uri, loadcommitted);
		c->https = webkit_web_view_get_tls_info(c->view, &c->cert,
		                                        &c->tlserr);
		/* URMA: a committed document outside the publication the name
		 * resolved to ends the name context (direct TXID navigation). */
		{
			UrmaAddr ad;
			if (c->urmatxid && urma_parse_uri(uri, &ad, NULL)
			    && (ad.kind != URMA_ADDR_PUB || strcmp(ad.txid, c->urmatxid)
			        || strcmp(ad.net, c->urmanet)))
				urmaclearctx(c);
		}
		if (c->entry && !gtk_widget_has_focus(c->entry))
			gtk_entry_set_text(GTK_ENTRY(c->entry), displayuri(c));
		/* FACELIFT: say what the committed document is. A publication
		 * document can only have come from the re-verified store, whose
		 * cached manifest describes it; everything else is named plainly. */
		if (!c->errorpage && !c->errbadge) {
			char *sum = urma_summary(uri);
			if (sum)
				setbadge(c, "verified", "verified", sum);
			else if (urma_is_uri(uri))
				setbadge(c, "busy", "loading", NULL);
			else if (g_str_has_prefix(uri, "file://"))
				setbadge(c, "plain", "local file", "A local file, not an URMA publication");
			else
				setbadge(c, NULL, NULL, NULL);
			g_free(sum);
		}
		syncnav(c);
		break;
	case WEBKIT_LOAD_FINISHED:
		seturiparameters(c, uri, loadfinished);
		/* MODERNISE: keep the address in sync with where we actually
		 * are, unless the user is mid-edit. */
		if (c->entry && !gtk_widget_has_focus(c->entry))
			gtk_entry_set_text(GTK_ENTRY(c->entry), displayuri(c));
		setstatus(c, NULL);
		setloading(c, 0);
		syncnav(c);
		/* Disabled until we write some WebKitWebExtension for
		 * manipulating the DOM directly.
		evalscript(c, "document.documentElement.style.overflow = '%s'",
		    enablescrollbars ? "auto" : "hidden");
		*/
		runscript(c);
		break;
	}
	updatetitle(c);
}

void
progresschanged(WebKitWebView *v, GParamSpec *ps, Client *c)
{
	c->progress = webkit_web_view_get_estimated_load_progress(c->view) *
	              100;
	/* MODERNISE: the thin bar only exists while something is loading, so
	 * progress is visible without reading the window title. */
	if (c->load) {
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(c->load),
		    c->progress / 100.0);
		if (c->progress < 100)
			gtk_widget_show(c->load);
		else
			gtk_widget_hide(c->load);
	}
	updatetitle(c);
}

void
titlechanged(WebKitWebView *view, GParamSpec *ps, Client *c)
{
	c->title = webkit_web_view_get_title(c->view);
	updatetitle(c);
}

void
mousetargetchanged(WebKitWebView *v, WebKitHitTestResult *h, guint modifiers,
    Client *c)
{
	WebKitHitTestResultContext hc = webkit_hit_test_result_get_context(h);

	/* Keep the hit test to know where is the pointer on the next click */
	c->mousepos = h;

	if (hc & OnLink)
		c->targeturi = webkit_hit_test_result_get_link_uri(h);
	else if (hc & OnImg)
		c->targeturi = webkit_hit_test_result_get_image_uri(h);
	else if (hc & OnMedia)
		c->targeturi = webkit_hit_test_result_get_media_uri(h);
	else
		c->targeturi = NULL;

	c->overtitle = c->targeturi;
	/* MODERNISE: show the link target in the status line, where a browser
	 * is expected to put it, instead of only in the window title. A live
	 * zoom readout wins until it expires, so the two do not flicker. */
	if (!c->zoomflash)
		setstatus(c, c->targeturi);
	updatetitle(c);
}

/* MODERNISE: surf had no load-failed handler at all, so a failed load left
 * the previous page on screen with no explanation. Render the reason. */
gboolean
loadfailed(WebKitWebView *v, WebKitLoadEvent e, gchar *uri, GError *err,
    Client *c)
{
	gchar *html, *euri, *emsg;

	if (g_error_matches(err, WEBKIT_NETWORK_ERROR,
	    WEBKIT_NETWORK_ERROR_CANCELLED))
		return FALSE;

	/* URMA: a declared resource saved from the store leaves the page in
	 * place; WebKit reports the ignored policy as an interrupted frame load.
	 * Keep the save status and the truthful address. */
	if (urma_is_uri(uri) && g_error_matches(err, WEBKIT_POLICY_ERROR,
	    WEBKIT_POLICY_ERROR_FRAME_LOAD_INTERRUPTED_BY_POLICY_CHANGE)) {
		setatom(c, AtomUri, geturi(c));
		updatetitle(c);
		setloading(c, 0);
		return TRUE;
	}

	/* URMA: the handler refuses name forms; a refused main-frame load in
	 * the provisional phase is a top-level navigation by name (link,
	 * script redirect). Resolve it here; nothing was committed. */
	if (e == WEBKIT_LOAD_STARTED && uri) {
		UrmaAddr ad;
		char *path = NULL;
		gboolean done = FALSE;

		if (urma_parse_uri(uri, &ad, &path) && ad.kind == URMA_ADDR_NAME) {
			char *tail = urma_tail(uri);
			done = urmaresolveload(c, &ad, tail);
			g_free(tail);
		}
		g_free(path);
		if (done)
			return TRUE;
	}

	euri = g_markup_escape_text(uri ? uri : "", -1);
	emsg = g_markup_escape_text(err && err->message ? err->message : "", -1);
	/* FACELIFT: the error page speaks the site's language: paper, ink, a
	 * lime rule, the embedded families, and the real reason. */
	html = g_strdup_printf(
	    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
	    "<title>Could not load</title><style>"
	    "body{margin:0;background:#ecebe3;color:#0f1512;"
	    "font:16px/1.55 '" URMA_FONT_SANS "',system-ui,sans-serif;"
	    "display:flex;min-height:100vh;align-items:center;justify-content:center}"
	    "main{max-width:38em;padding:2.2em 2em 2.6em;border-top:4px solid #d6fa52}"
	    ".k{font-size:11px;font-weight:700;letter-spacing:.14em;"
	    "text-transform:uppercase;color:#5b655e;margin:0 0 1.2em}"
	    "h1{font-size:1.65em;line-height:1.2;letter-spacing:-.01em;margin:0 0 .7em}"
	    "p{margin:.6em 0}"
	    ".u{font-family:'" URMA_FONT_MONO "',monospace;font-size:.88em;"
	    "word-break:break-all;background:#e2e3d8;padding:.55em .75em;border-radius:6px}"
	    ".why{color:#17211c}.note{color:#5b655e;font-size:.92em;margin-top:1.4em}"
	    "</style></head><body><main>"
	    "<p class='k'>URMA · load failed</p>"
	    "<h1>This page could not be loaded</h1>"
	    "<p class='u'>%s</p><p class='why'>%s</p>%s"
	    "</main></body></html>", euri, emsg,
	    urma_is_uri(uri) ? "<p class='note'>Nothing renders before the whole "
	    "declared set of a publication is recovered and verified.</p>" : "");

	c->errorpage = 1;
	c->errbadge = 1;
	webkit_web_view_load_alternate_html(v, html, uri, NULL);
	setstatus(c, NULL);
	setbadge(c, "error", "unavailable", err && err->message ? err->message : NULL);
	if (c->spin) {
		gtk_spinner_stop(GTK_SPINNER(c->spin));
		gtk_widget_hide(c->spin);
	}
	setloading(c, 0);
	syncnav(c);
	if (c->load)
		gtk_widget_hide(c->load);

	g_free(html);
	g_free(euri);
	g_free(emsg);
	return TRUE;
}

gboolean
permissionrequested(WebKitWebView *v, WebKitPermissionRequest *r, Client *c)
{
	ParamName param = ParameterLast;

	if (WEBKIT_IS_GEOLOCATION_PERMISSION_REQUEST(r)) {
		param = Geolocation;
	} else if (WEBKIT_IS_USER_MEDIA_PERMISSION_REQUEST(r)) {
		if (webkit_user_media_permission_is_for_audio_device(
		    WEBKIT_USER_MEDIA_PERMISSION_REQUEST(r)))
			param = AccessMicrophone;
		else if (webkit_user_media_permission_is_for_video_device(
		         WEBKIT_USER_MEDIA_PERMISSION_REQUEST(r)))
			param = AccessWebcam;
	} else {
		return FALSE;
	}

	if (curconfig[param].val.i)
		webkit_permission_request_allow(r);
	else
		webkit_permission_request_deny(r);

	return TRUE;
}

gboolean
decidepolicy(WebKitWebView *v, WebKitPolicyDecision *d,
    WebKitPolicyDecisionType dt, Client *c)
{
	switch (dt) {
	case WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION:
		decidenavigation(d, c);
		break;
	case WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION:
		decidenewwindow(d, c);
		break;
	case WEBKIT_POLICY_DECISION_TYPE_RESPONSE:
		decideresource(d, c);
		break;
	default:
		webkit_policy_decision_ignore(d);
		break;
	}
	return TRUE;
}

void
decidenavigation(WebKitPolicyDecision *d, Client *c)
{
	WebKitNavigationAction *a =
	    webkit_navigation_policy_decision_get_navigation_action(
	    WEBKIT_NAVIGATION_POLICY_DECISION(d));

	const char *uri = webkit_uri_request_get_uri(
	    webkit_navigation_action_get_request(a));
	UrmaAddr ad;

	/* URMA: HTTP(S) is not a destination; malformed urma hosts never load.
	 * Name forms proceed: the handler refuses them and the main-frame
	 * refusal is resolved in loadfailed(); a subframe just fails. */
	if (g_str_has_prefix(uri, "http://") || g_str_has_prefix(uri, "https://")) {
		setstatus(c, "HTTP(S) is not an URMA destination");
		webkit_policy_decision_ignore(d);
		return;
	}
	if (g_str_has_prefix(uri, "urma://") && !urma_parse_uri(uri, &ad, NULL)) {
		setstatus(c, "invalid urma address (canonical form required)");
		webkit_policy_decision_ignore(d);
		return;
	}

	switch (webkit_navigation_action_get_navigation_type(a)) {
	case WEBKIT_NAVIGATION_TYPE_LINK_CLICKED: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_FORM_SUBMITTED: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_BACK_FORWARD: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_RELOAD: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_FORM_RESUBMITTED: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_OTHER: /* fallthrough */
	default:
		/* Do not navigate to links with a "_blank" target (popup) */
		if (webkit_navigation_action_get_frame_name(a)) {
			webkit_policy_decision_ignore(d);
		} else {
			/* Filter out navigation to different domain ? */
			/* get action→urirequest, copy and load in new window+view
			 * on Ctrl+Click ? */
			webkit_policy_decision_use(d);
		}
		break;
	}
}

void
decidenewwindow(WebKitPolicyDecision *d, Client *c)
{
	Arg arg;
	WebKitNavigationAction *a =
	    webkit_navigation_policy_decision_get_navigation_action(
	    WEBKIT_NAVIGATION_POLICY_DECISION(d));


	switch (webkit_navigation_action_get_navigation_type(a)) {
	case WEBKIT_NAVIGATION_TYPE_LINK_CLICKED: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_FORM_SUBMITTED: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_BACK_FORWARD: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_RELOAD: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_FORM_RESUBMITTED:
		/* Filter domains here */
/* If the value of “mouse-button” is not 0, then the navigation was triggered by a mouse event.
 * test for link clicked but no button ? */
		arg.v = webkit_uri_request_get_uri(
		        webkit_navigation_action_get_request(a));
		newwindow(c, &arg, 0);
		break;
	case WEBKIT_NAVIGATION_TYPE_OTHER: /* fallthrough */
	default:
		break;
	}

	webkit_policy_decision_ignore(d);
}

void
decideresource(WebKitPolicyDecision *d, Client *c)
{
	size_t i;
	int isascii = 1;
	WebKitResponsePolicyDecision *r = WEBKIT_RESPONSE_POLICY_DECISION(d);
	WebKitURIResponse *res =
	    webkit_response_policy_decision_get_response(r);
	const gchar *uri = webkit_uri_response_get_uri(res);

	if (g_str_has_suffix(uri, "/favicon.ico")) {
		webkit_policy_decision_ignore(d);
		return;
	}

	if (!g_str_has_prefix(uri, "http://")
	    && !g_str_has_prefix(uri, "https://")
	    && !g_str_has_prefix(uri, "about:")
	    && !g_str_has_prefix(uri, "file://")
	    && !g_str_has_prefix(uri, "webkit://")
	    && !g_str_has_prefix(uri, "urma:")
	    && !g_str_has_prefix(uri, "data:")
	    && !g_str_has_prefix(uri, "blob:")
	    && !(g_str_has_prefix(uri, "webkit-pdfjs-viewer://") && curconfig[PDFJSviewer].val.i)
	    && strlen(uri) > 0) {
		for (i = 0; i < strlen(uri); i++) {
			if (!g_ascii_isprint(uri[i])) {
				isascii = 0;
				break;
			}
		}
		if (isascii) {
			handleplumb(c, uri);
			webkit_policy_decision_ignore(d);
			return;
		}
	}

	if (webkit_response_policy_decision_is_mime_type_supported(r)) {
		webkit_policy_decision_use(d);
	} else if (urma_is_uri(uri)) {
		char *msg;
		webkit_policy_decision_ignore(d);
		msg = urma_save(uri, GTK_WINDOW(c->win));
		setstatus(c, msg);
		g_free(msg);
	} else {
		webkit_policy_decision_ignore(d);
		download(c, res);
	}
}

void
insecurecontent(WebKitWebView *v, WebKitInsecureContentEvent e, Client *c)
{
	c->insecure = 1;
}

void
downloadstarted(WebKitWebContext *wc, WebKitDownload *d, Client *c)
{
	/* URMA: declared resources are served by the handler from the verified
	 * store; WebKit downloads them itself and the user picks the destination.
	 * The curl path stays for everything else. */
	WebKitURIRequest *req = webkit_download_get_request(d);
	if (req && urma_is_uri(webkit_uri_request_get_uri(req))) {
		char *msg = urma_save(webkit_uri_request_get_uri(req), GTK_WINDOW(c->win));
		webkit_download_cancel(d);
		setstatus(c, msg);
		g_free(msg);
		return;
	}
	g_signal_connect(G_OBJECT(d), "notify::response",
	                 G_CALLBACK(responsereceived), c);
}

void
responsereceived(WebKitDownload *d, GParamSpec *ps, Client *c)
{
	download(c, webkit_download_get_response(d));
	webkit_download_cancel(d);
}

void
download(Client *c, WebKitURIResponse *r)
{
	Arg a = (Arg)DOWNLOAD(webkit_uri_response_get_uri(r), geturi(c));
	spawn(c, &a);
}

void
webprocessterminated(WebKitWebView *v, WebKitWebProcessTerminationReason r,
                     Client *c)
{
	fprintf(stderr, "web process terminated: %s\n",
	        r == WEBKIT_WEB_PROCESS_CRASHED ? "crashed" : "no memory");
	closeview(v, c);
}

void
closeview(WebKitWebView *v, Client *c)
{
	gtk_widget_destroy(c->win);
}

void
destroywin(GtkWidget* w, Client *c)
{
	destroyclient(c);
	if (!clients)
		gtk_main_quit();
}

void
pasteuri(GtkClipboard *clipboard, const char *text, gpointer d)
{
	Arg a = {.v = text };
	if (text)
		loaduri((Client *) d, &a);
}

void
reload(Client *c, const Arg *a)
{
	if (a->i)
		webkit_web_view_reload_bypass_cache(c->view);
	else
		webkit_web_view_reload(c->view);
}

void
print(Client *c, const Arg *a)
{
	webkit_print_operation_run_dialog(webkit_print_operation_new(c->view),
	                                  GTK_WINDOW(c->win));
}

void
showcert(Client *c, const Arg *a)
{
	GTlsCertificate *cert = c->failedcert ? c->failedcert : c->cert;
	GcrCertificate *gcrt;
	GByteArray *crt;
	GtkWidget *win;
	GcrCertificateWidget *wcert;

	if (!cert)
		return;

	g_object_get(cert, "certificate", &crt, NULL);
	gcrt = gcr_simple_certificate_new(crt->data, crt->len);
	g_byte_array_unref(crt);

	win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	wcert = gcr_certificate_widget_new(gcrt);
	g_object_unref(gcrt);

	gtk_container_add(GTK_CONTAINER(win), GTK_WIDGET(wcert));
	gtk_widget_show_all(win);
}

void
clipboard(Client *c, const Arg *a)
{
	if (a->i) { /* load clipboard uri */
		gtk_clipboard_request_text(gtk_clipboard_get(
		                           GDK_SELECTION_PRIMARY),
		                           pasteuri, c);
	} else { /* copy uri */
		gtk_clipboard_set_text(gtk_clipboard_get(
		                       GDK_SELECTION_PRIMARY), c->targeturi
		                       ? c->targeturi : geturi(c), -1);
	}
}

void
zoom(Client *c, const Arg *a)
{
	/* Step from the view's actual zoom, not from the stored config value:
	 * Ctrl+wheel and the keys must not drift apart, and the clamp keeps
	 * repeated steps from reaching unusable extremes. */
	gdouble level = webkit_web_view_get_zoom_level(c->view);

	if (a->i > 0)
		level = MIN(level + 0.1, 5.0);
	else if (a->i < 0)
		level = MAX(level - 0.1, 0.25);
	else
		level = 1.0;

	webkit_web_view_set_zoom_level(c->view, level);
	curconfig[ZoomLevel].val.f = webkit_web_view_get_zoom_level(c->view);
	flashzoom(c);
}

/* Keyboard scrolling in viewport percent. Upstream relayed this through a
 * socket to the web extension; the extension API it used is deprecated and
 * the shell can evaluate the same two lines itself. */
void
scrollv(Client *c, const Arg *a)
{
	evalscript(c, "window.scrollBy(0,window.innerHeight/100*%d);", a->i);
}

void
scrollh(Client *c, const Arg *a)
{
	evalscript(c, "window.scrollBy(window.innerWidth/100*%d,0);", a->i);
}

void
navigate(Client *c, const Arg *a)
{
	if (a->i < 0)
		webkit_web_view_go_back(c->view);
	else if (a->i > 0)
		webkit_web_view_go_forward(c->view);
}

void
stop(Client *c, const Arg *a)
{
	webkit_web_view_stop_loading(c->view);
	hidefind(c);
}

void
toggle(Client *c, const Arg *a)
{
	curconfig[a->i].val.i ^= 1;
	setparameter(c, 1, (ParamName)a->i, &curconfig[a->i].val);
}

void
togglefullscreen(Client *c, const Arg *a)
{
	/* toggling value is handled in winevent() */
	if (c->fullscreen)
		gtk_window_unfullscreen(GTK_WINDOW(c->win));
	else
		gtk_window_fullscreen(GTK_WINDOW(c->win));
}

void
togglecookiepolicy(Client *c, const Arg *a)
{
	++cookiepolicy;
	cookiepolicy %= strlen(curconfig[CookiePolicies].val.v);

	setparameter(c, 0, CookiePolicies, NULL);
}

void
toggleinspector(Client *c, const Arg *a)
{
	if (webkit_web_inspector_is_attached(c->inspector))
		webkit_web_inspector_close(c->inspector);
	else if (curconfig[Inspector].val.i)
		webkit_web_inspector_show(c->inspector);
}

void
find(Client *c, const Arg *a)
{
	const char *s, *f;

	if (a && a->i) {
		if (a->i > 0)
			webkit_find_controller_search_next(c->finder);
		else
			webkit_find_controller_search_previous(c->finder);
	} else {
		/* The inline find bar owns the needle; the X property remains
		 * for scripted use (xprop -set _SURF_FIND). */
		s = c->findentry && *gtk_entry_get_text(GTK_ENTRY(c->findentry))
		    ? gtk_entry_get_text(GTK_ENTRY(c->findentry)) : getatom(c, AtomFind);
		f = webkit_find_controller_get_search_text(c->finder);

		if (g_strcmp0(f, s) == 0) /* reset search */
			webkit_find_controller_search(c->finder, "", findopts,
			                              G_MAXUINT);

		webkit_find_controller_search(c->finder, s, findopts,
		                              G_MAXUINT);

		if (strcmp(s, "") == 0)
			webkit_find_controller_search_finish(c->finder);
	}
}

void
clicknavigate(Client *c, const Arg *a, WebKitHitTestResult *h)
{
	navigate(c, a);
}

void
clicknewwindow(Client *c, const Arg *a, WebKitHitTestResult *h)
{
	Arg arg;

	arg.v = webkit_hit_test_result_get_link_uri(h);
	newwindow(c, &arg, a->i);
}

void
clickexternplayer(Client *c, const Arg *a, WebKitHitTestResult *h)
{
	Arg arg;

	arg = (Arg)VIDEOPLAY(webkit_hit_test_result_get_media_uri(h));
	spawn(c, &arg);
}

int
main(int argc, char *argv[])
{
	Arg arg;
	Client *c;

	memset(&arg, 0, sizeof(arg));

	/* command line args */
	ARGBEGIN {
	case 'a':
		defconfig[CookiePolicies].val.v = EARGF(usage());
		defconfig[CookiePolicies].prio = 2;
		break;
	case 'b':
		defconfig[ScrollBars].val.i = 0;
		defconfig[ScrollBars].prio = 2;
		break;
	case 'B':
		defconfig[ScrollBars].val.i = 1;
		defconfig[ScrollBars].prio = 2;
		break;
	case 'c':
		cookiefile = EARGF(usage());
		break;
	case 'C':
		stylefile = EARGF(usage());
		break;
	case 'd':
		defconfig[DiskCache].val.i = 0;
		defconfig[DiskCache].prio = 2;
		break;
	case 'D':
		defconfig[DiskCache].val.i = 1;
		defconfig[DiskCache].prio = 2;
		break;
	case 'e':
		embed = strtol(EARGF(usage()), NULL, 0);
		break;
	case 'f':
		defconfig[RunInFullscreen].val.i = 0;
		defconfig[RunInFullscreen].prio = 2;
		break;
	case 'F':
		defconfig[RunInFullscreen].val.i = 1;
		defconfig[RunInFullscreen].prio = 2;
		break;
	case 'g':
		defconfig[Geolocation].val.i = 0;
		defconfig[Geolocation].prio = 2;
		break;
	case 'G':
		defconfig[Geolocation].val.i = 1;
		defconfig[Geolocation].prio = 2;
		break;
	case 'i':
		defconfig[LoadImages].val.i = 0;
		defconfig[LoadImages].prio = 2;
		break;
	case 'I':
		defconfig[LoadImages].val.i = 1;
		defconfig[LoadImages].prio = 2;
		break;
	case 'k':
		defconfig[KioskMode].val.i = 0;
		defconfig[KioskMode].prio = 2;
		break;
	case 'K':
		defconfig[KioskMode].val.i = 1;
		defconfig[KioskMode].prio = 2;
		break;
	case 'm':
		defconfig[Style].val.i = 0;
		defconfig[Style].prio = 2;
		break;
	case 'M':
		defconfig[Style].val.i = 1;
		defconfig[Style].prio = 2;
		break;
	case 'n':
		defconfig[Inspector].val.i = 0;
		defconfig[Inspector].prio = 2;
		break;
	case 'N':
		defconfig[Inspector].val.i = 1;
		defconfig[Inspector].prio = 2;
		break;
	case 'r':
		scriptfile = EARGF(usage());
		break;
	case 's':
		defconfig[JavaScript].val.i = 0;
		defconfig[JavaScript].prio = 2;
		break;
	case 'S':
		defconfig[JavaScript].val.i = 1;
		defconfig[JavaScript].prio = 2;
		break;
	case 't':
		defconfig[StrictTLS].val.i = 0;
		defconfig[StrictTLS].prio = 2;
		break;
	case 'T':
		defconfig[StrictTLS].val.i = 1;
		defconfig[StrictTLS].prio = 2;
		break;
	case 'u':
		fulluseragent = EARGF(usage());
		break;
	case 'v':
		die("surf-"VERSION", see LICENSE for © details\n");
	case 'w':
		showxid = 1;
		break;
	case 'x':
		defconfig[Certificate].val.i = 0;
		defconfig[Certificate].prio = 2;
		break;
	case 'X':
		defconfig[Certificate].val.i = 1;
		defconfig[Certificate].prio = 2;
		break;
	case 'z':
		defconfig[ZoomLevel].val.f = strtof(EARGF(usage()), NULL);
		defconfig[ZoomLevel].prio = 2;
		break;
	default:
		usage();
	} ARGEND;
	if (argc > 0)
		arg.v = argv[0];
	else
		arg.v = "about:blank";

	setup();
	c = newclient(NULL);
	showview(NULL, c);

	loaduri(c, &arg);
	updatetitle(c);

	gtk_main();
	cleanup();

	return 0;
}
