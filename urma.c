/* URMA shell logic for the surf experiment (F1, 2026-09-22).
 *
 * Real bridge, no fixtures:
 *   - publications come from the verified store written by `urma web fetch`
 *     (STORE/publications/<chain>/<root>.json + STORE/objects/<sha256>);
 *     a missing publication is fetched asynchronously by spawning the CLI;
 *     every object is re-verified (SHA-256, length) before it is served;
 *   - names resolve through `urma names resolve` over the local names index
 *     of the registry (STORE-independent: NAMES_DIR/<genesis>.json);
 *   - address forms, per-publication origin, declared set, explicit MIME +
 *     Content-Type + CSP, page network black hole, active registry per
 *     network (ini, global across processes) as decided in the contract.
 *
 * Environment (all optional): URMA_BIN (the urma executable), URMA_WEB_STORE,
 * URMA_NAMES_DIR, URMA_SURF_REGTEST=1 (adds the experiment-only net `rbtc`
 * = bitcoin-regtest for loopback tests). The CLI inherits URMA_RPC_URL,
 * URMA_NODE_AUTH_FILE and URMA_CONFIG for its chain source. */
#include <string.h>
#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

#include "urma.h"

/* ---- networks and paths ------------------------------------------------- */

struct net { const char *suffix, *chain; gboolean testonly; };
static const struct net nets[] = {
	{ "ltc",  "litecoin-mainnet",  FALSE },
	{ "btc",  "bitcoin-mainnet",   FALSE },
	{ "tltc", "litecoin-testnet",  FALSE },
	{ "tbtc", "bitcoin-testnet4",  FALSE },
	{ "rbtc", "bitcoin-regtest",   TRUE  },
};

static const char csp[] =
	"default-src 'self'; script-src 'self' 'unsafe-inline' 'wasm-unsafe-eval'; "
	"style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; "
	"media-src 'self' blob:; font-src 'self'; connect-src 'self'; "
	"worker-src 'self'; frame-src 'self'; object-src 'none'; "
	"base-uri 'self'; form-action 'self'";

static const struct net *
findnet(const char *s)
{
	guint i;
	for (i = 0; s && i < G_N_ELEMENTS(nets); i++)
		if (!strcmp(s, nets[i].suffix)
		    && (!nets[i].testonly || g_getenv("URMA_SURF_REGTEST")))
			return &nets[i];
	return NULL;
}

gboolean
urma_is_net(const char *s)
{
	return findnet(s) != NULL;
}

static const char *
suffixof(const char *chain)
{
	guint i;
	for (i = 0; chain && i < G_N_ELEMENTS(nets); i++)
		if (!strcmp(chain, nets[i].chain))
			return nets[i].suffix;
	return "";
}

static UrmaFetchNotify notifycb;
static gpointer notifyuser;

void
urma_set_fetch_notify(UrmaFetchNotify cb, gpointer user)
{
	notifycb = cb;
	notifyuser = user;
}

static void
notify(UrmaFetchEvent ev, const char *chain, const char *txid, const char *detail)
{
	if (notifycb)
		notifycb(ev, suffixof(chain), txid, detail, notifyuser);
}

static const char *
urmabin(void)
{
	const char *b = g_getenv("URMA_BIN");
	return b && *b ? b : "urma";
}

static char *
storedir(void)
{
	const char *s = g_getenv("URMA_WEB_STORE");
	return s && *s ? g_strdup(s)
	               : g_build_filename(g_get_user_data_dir(), "urma", "web-store", NULL);
}

/* One index per (chain, genesis): NAMES_DIR/<chain>/<genesis>.json. The
 * chain is part of the key, and the CLI refuses an index of another chain. */
static char *
indexpath(const char *chain, const char *genesis)
{
	const char *d = g_getenv("URMA_NAMES_DIR");
	char *dir = d && *d ? g_strdup(d)
	                    : g_build_filename(g_get_user_data_dir(), "urma", "names", NULL);
	char *name = g_strconcat(genesis, ".json", NULL);
	char *p = g_build_filename(dir, chain, name, NULL);
	g_free(dir);
	g_free(name);
	return p;
}

/* ---- address grammar --------------------------------------------------- */

static gboolean
ishex64(const char *s)
{
	gsize i;
	if (!s || strlen(s) != 64)
		return FALSE;
	for (i = 0; i < 64; i++)
		if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
			return FALSE;
	return TRUE;
}

/* Registry grammar: 1..63 bytes of a-z 0-9 and internal hyphens; no hyphen
 * at either end; bytes 3 and 4 not both '-' (reserved hostname form). */
static gboolean
isname(const char *s)
{
	gsize n = s ? strlen(s) : 0, i;
	if (n < 1 || n > 63)
		return FALSE;
	for (i = 0; i < n; i++) {
		char ch = s[i];
		if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-'))
			return FALSE;
	}
	if (s[0] == '-' || s[n - 1] == '-')
		return FALSE;
	if (n >= 4 && s[2] == '-' && s[3] == '-')
		return FALSE;
	return TRUE;
}

gboolean
urma_parse_host(const char *host, UrmaAddr *out)
{
	gchar **l;
	guint n;
	gboolean ok = FALSE;

	memset(out, 0, sizeof(*out));
	if (!host || !*host)
		return FALSE;
	l = g_strsplit(host, ".", -1);
	n = g_strv_length(l);
	if (n == 2 && urma_is_net(l[1])) {
		if (ishex64(l[0])) {
			out->kind = URMA_ADDR_PUB;
			g_strlcpy(out->txid, l[0], sizeof(out->txid));
			ok = TRUE;
		} else if (isname(l[0])) {
			out->kind = URMA_ADDR_NAME;
			g_strlcpy(out->name, l[0], sizeof(out->name));
			ok = TRUE;
		}
		if (ok)
			g_strlcpy(out->net, l[1], sizeof(out->net));
	} else if (n == 3 && urma_is_net(l[2]) && ishex64(l[0]) && isname(l[1])) {
		out->kind = URMA_ADDR_NAME;
		g_strlcpy(out->genesis, l[0], sizeof(out->genesis));
		g_strlcpy(out->name, l[1], sizeof(out->name));
		g_strlcpy(out->net, l[2], sizeof(out->net));
		ok = TRUE;
	}
	g_strfreev(l);
	if (!ok)
		out->kind = URMA_ADDR_INVALID;
	return ok;
}

gboolean
urma_is_uri(const char *uri)
{
	return uri && g_str_has_prefix(uri, "urma://");
}

/* Canonical addresses only: no userinfo, no port, lowercase host, no
 * percent escapes in the host. GUri gives us the pieces; we refuse the rest. */
/* The authority as written, before any parser normalises it: only the bytes
 * of the canonical grammar (a-z, 0-9, '-', '.') may appear; percent escapes,
 * uppercase, userinfo, ports and empty labels are refused here. */
static char *
rawauthority(const char *uri)
{
	const char *start = uri + strlen("urma://"), *end = start;

	while (*end && *end != '/' && *end != '?' && *end != '#')
		end++;
	for (const char *c = start; c < end; c++)
		if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '.'))
			return NULL;
	if (end == start || start[0] == '.' || end[-1] == '.' || strstr(start, ".."))
		return NULL;
	return g_strndup(start, end - start);
}

gboolean
urma_parse_uri(const char *uri, UrmaAddr *out, char **path)
{
	GUri *g;
	const char *host, *p;
	char *raw;
	gboolean ok;

	memset(out, 0, sizeof(*out));
	if (path)
		*path = NULL;
	if (!urma_is_uri(uri))
		return FALSE;
	if (!(raw = rawauthority(uri)))
		return FALSE;
	g = g_uri_parse(uri, G_URI_FLAGS_NONE, NULL);
	if (!g) {
		g_free(raw);
		return FALSE;
	}
	host = g_uri_get_host(g);
	/* The parser must see exactly the raw authority: any difference means a
	 * normalisation happened and the address is not canonical. */
	ok = host && !strcmp(host, raw) && g_uri_get_port(g) == -1 && !g_uri_get_userinfo(g)
	     && urma_parse_host(raw, out);
	if (ok && path) {
		p = g_uri_get_path(g);
		*path = g_strdup(p && *p ? p : "/");
	}
	g_uri_unref(g);
	g_free(raw);
	return ok;
}

char *
urma_pub_uri(const char *net, const char *txid, const char *path)
{
	return g_strdup_printf("urma://%s.%s%s", txid, net,
	                       path && *path ? path : "/");
}

/* Everything after the authority of an accepted address: the site path with
 * its query and fragment, "/" when absent. The store sees only the path; the
 * page keeps query and fragment across a name resolution (contract §4). */
char *
urma_tail(const char *uri)
{
	const char *p;

	if (!urma_is_uri(uri))
		return g_strdup("/");
	p = uri + strlen("urma://");
	while (*p && *p != '/' && *p != '?' && *p != '#')
		p++;
	if (!*p)
		return g_strdup("/");
	return *p == '/' ? g_strdup(p) : g_strconcat("/", p, NULL);
}

/* ---- running the CLI ---------------------------------------------------- */

static gchar **
cliargs(const char *chain, const char *first, ...)
{
	GPtrArray *a = g_ptr_array_new();
	va_list ap;
	const char *s;

	g_ptr_array_add(a, g_strdup(urmabin()));
	va_start(ap, first);
	for (s = first; s; s = va_arg(ap, const char *))
		g_ptr_array_add(a, g_strdup(s));
	va_end(ap);
	g_ptr_array_add(a, NULL);
	(void)chain;
	return (gchar **)g_ptr_array_free(a, FALSE);
}

static gchar **
clienv(const char *chain)
{
	gchar **e = g_get_environ();
	e = g_environ_setenv(e, "URMA_OUTPUT", "json", TRUE);
	if (chain)
		e = g_environ_setenv(e, "URMA_NETWORK", chain, TRUE);
	return e;
}

/* Synchronous CLI call returning the parsed JSON report (caller unrefs). */
static JsonNode *
clijson(const char *chain, gchar **argv, GError **err)
{
	gchar **env = clienv(chain);
	gchar *out = NULL, *errtxt = NULL;
	int status = 0;
	JsonParser *p;
	JsonNode *root = NULL;

	if (!g_spawn_sync(NULL, argv, env, G_SPAWN_SEARCH_PATH, NULL, NULL,
	                  &out, &errtxt, &status, err)) {
		g_strfreev(env);
		g_free(out);
		g_free(errtxt);
		return NULL;
	}
	g_strfreev(env);
	if (!g_spawn_check_wait_status(status, NULL)) {
		char *line = errtxt ? g_strrstr(g_strchomp(errtxt), "\n") : NULL;
		g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED, "%s",
		    line && line[1] ? line + 1 : (errtxt && *errtxt ? errtxt : "urma failed"));
		g_free(out);
		g_free(errtxt);
		return NULL;
	}
	p = json_parser_new();
	if (json_parser_load_from_data(p, out ? out : "", -1, err))
		root = json_node_ref(json_parser_get_root(p));
	g_object_unref(p);
	g_free(out);
	g_free(errtxt);
	return root;
}

/* ---- registries and the active slot ------------------------------------ */

static char *
activefile(void)
{
	return g_build_filename(g_get_home_dir(), ".surf", "urma-registries.ini", NULL);
}

/* Global across surf processes: read on every use, written on switch.
 * No shipped default: a registry becomes active only through an explicit
 * registry address (prompt) or the user's own ini entry. */
const char *
urma_active_genesis(const char *net)
{
	static char slot[65];
	GKeyFile *kf = g_key_file_new();
	char *f = activefile(), *v = NULL;

	slot[0] = 0;
	if (g_key_file_load_from_file(kf, f, G_KEY_FILE_NONE, NULL))
		v = g_key_file_get_string(kf, "active", net, NULL);
	if (v && ishex64(v))
		g_strlcpy(slot, v, sizeof(slot));
	g_free(v);
	g_free(f);
	g_key_file_unref(kf);
	return slot[0] ? slot : NULL;
}

gboolean
urma_set_active_genesis(const char *net, const char *genesis, GError **err)
{
	GKeyFile *kf = g_key_file_new();
	char *f = activefile(), *dir = g_path_get_dirname(f);
	GError *loaderr = NULL;
	gboolean ok = FALSE;

	if (!g_key_file_load_from_file(kf, f, G_KEY_FILE_NONE, &loaderr)
	    && !g_error_matches(loaderr, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
		g_propagate_error(err, loaderr);
		goto done;
	}
	g_clear_error(&loaderr);
	if (genesis && *genesis)
		g_key_file_set_string(kf, "active", net, genesis);
	else
		g_key_file_remove_key(kf, "active", net, NULL);
	if (g_mkdir_with_parents(dir, 0700) < 0) {
		g_set_error(err, G_FILE_ERROR, g_file_error_from_errno(errno),
		    "cannot create registry directory %s: %s", dir, g_strerror(errno));
		goto done;
	}
	ok = g_key_file_save_to_file(kf, f, err);
done:
	g_free(dir);
	g_free(f);
	g_key_file_unref(kf);
	return ok;
}

/* Published default registries, as recorded in NAMES_NETWORKS.md §6. The
 * shell never activates one by itself: it is one of the choices offered. */
struct documented { const char *net, *genesis, *title; };
static const struct documented documented[] = {
	{ "tltc", "4c53ebbea660c206c4a09d4ccc48479950d7300e39876c9772b2f72ca232d1e3",
	  "ROSINT registry" },
};

const char *const *
urma_nets(guint *count)
{
	static const char *const pub[] = { "ltc", "btc", "tltc", "tbtc" };
	*count = G_N_ELEMENTS(pub);
	return pub;
}

static UrmaRegistry *
registryslot(GArray *a, const char *genesis)
{
	guint i;
	UrmaRegistry r;

	for (i = 0; i < a->len; i++)
		if (!strcmp(g_array_index(a, UrmaRegistry, i).genesis, genesis))
			return &g_array_index(a, UrmaRegistry, i);
	memset(&r, 0, sizeof(r));
	g_strlcpy(r.genesis, genesis, sizeof(r.genesis));
	r.genesis_height = r.height = -1;
	r.title = "";
	g_array_append_val(a, r);
	return &g_array_index(a, UrmaRegistry, a->len - 1);
}

/* Display facts from a scanned index file. The file is the CLI's own
 * output; nothing here is trusted for resolution, only shown. */
static void
describeindex(UrmaRegistry *r, const char *path)
{
	JsonParser *p = json_parser_new();
	JsonObject *o, *reg, *rules, *names;

	if (json_parser_load_from_file(p, path, NULL)
	    && JSON_NODE_HOLDS_OBJECT(json_parser_get_root(p))) {
		o = json_node_get_object(json_parser_get_root(p));
		reg = json_object_has_member(o, "registry")
		      ? json_object_get_object_member(o, "registry") : NULL;
		if (reg) {
			r->scanned = TRUE;
			if (json_object_has_member(reg, "genesis_height"))
				r->genesis_height = json_object_get_int_member(reg, "genesis_height");
			if (json_object_has_member(reg, "height"))
				r->height = json_object_get_int_member(reg, "height");
			rules = json_object_has_member(reg, "rules")
			        ? json_object_get_object_member(reg, "rules") : NULL;
			if (rules && json_object_has_member(rules, "mode"))
				g_strlcpy(r->mode, json_object_get_string_member(rules, "mode"),
				          sizeof(r->mode));
			names = json_object_has_member(reg, "names")
			        ? json_object_get_object_member(reg, "names") : NULL;
			if (names)
				r->names = json_object_get_size(names);
		}
	}
	g_object_unref(p);
}

GArray *
urma_registries(const char *net)
{
	GArray *a = g_array_new(FALSE, TRUE, sizeof(UrmaRegistry));
	const struct net *n = findnet(net);
	const char *active = urma_active_genesis(net), *name;
	char *dir, *base;
	GDir *d;
	guint i;

	if (!n)
		return a;
	for (i = 0; i < G_N_ELEMENTS(documented); i++) {
		if (strcmp(documented[i].net, net))
			continue;
		UrmaRegistry *r = registryslot(a, documented[i].genesis);
		r->documented = TRUE;
		r->title = documented[i].title;
	}
	if (active)
		registryslot(a, active);
	base = indexpath(n->chain, "x");
	dir = g_path_get_dirname(base);
	g_free(base);
	if ((d = g_dir_open(dir, 0, NULL))) {
		while ((name = g_dir_read_name(d))) {
			char *stem, *path;
			if (!g_str_has_suffix(name, ".json"))
				continue;
			stem = g_strndup(name, strlen(name) - strlen(".json"));
			if (ishex64(stem)) {
				path = g_build_filename(dir, name, NULL);
				describeindex(registryslot(a, stem), path);
				g_free(path);
			}
			g_free(stem);
		}
		g_dir_close(d);
	}
	g_free(dir);
	/* An index may exist for a documented or active entry added first. */
	for (i = 0; i < a->len; i++) {
		UrmaRegistry *r = &g_array_index(a, UrmaRegistry, i);
		if (!r->scanned) {
			char *path = indexpath(n->chain, r->genesis);
			if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
				describeindex(r, path);
			g_free(path);
		}
	}
	return a;
}

const char *
urma_registry_label(const char *net, const char *genesis)
{
	static char label[96];
	const struct net *n = findnet(net);
	char *idx = indexpath(n ? n->chain : "unknown", genesis);
	gboolean scanned = g_file_test(idx, G_FILE_TEST_IS_REGULAR);

	g_snprintf(label, sizeof(label), "registry %.12s… on .%s%s", genesis, net,
	           scanned ? "" : " (not scanned locally yet)");
	g_free(idx);
	return label;
}

UrmaRegistryChoice
urma_registry_switch_prompt(GtkWindow *parent, const char *net, const char *genesis)
{
	GtkWidget *d;
	int resp;

	d = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
	    GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
	    "Choose the registry for .%s", net);
	gtk_dialog_add_buttons(GTK_DIALOG(d), "_Cancel", GTK_RESPONSE_CANCEL,
	    "Use _temporarily", GTK_RESPONSE_ACCEPT,
	    "Switch _global", GTK_RESPONSE_YES, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_CANCEL);
	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d),
	    "This link names another registry:\n\n  %s\n  (%s)\n\n"
	    "Temporary use stays in this tab and copies to tabs opened from it. "
	    "Switch global saves the registry for .%s, for every tab and window; "
	    "other name addresses may resolve elsewhere. Cancel changes nothing.",
	    genesis, urma_registry_label(net, genesis), net);
	resp = gtk_dialog_run(GTK_DIALOG(d));
	gtk_widget_destroy(d);
	return resp == GTK_RESPONSE_ACCEPT ? URMA_REGISTRY_TEMPORARY :
	    resp == GTK_RESPONSE_YES ? URMA_REGISTRY_GLOBAL : URMA_REGISTRY_CANCEL;
}

/* `urma names resolve` over the registry's local index. Positive answers are
 * cached briefly so the address bar does not spawn the CLI on every repaint. */
struct resolved { char txid[65]; gint64 when; };
static GHashTable *resolutions;

static JsonNode *
resolvejson(const char *net, const char *name, const char *genesis, GError **err)
{
	const struct net *n = findnet(net);
	char *idx;
	gchar **argv;
	JsonNode *root;
	JsonObject *o;

	if (!n) {
		g_set_error(err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "unknown network .%s", net);
		return NULL;
	}
	idx = indexpath(n->chain, genesis);
	if (!g_file_test(idx, G_FILE_TEST_IS_REGULAR)) {
		g_set_error(err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		    "registry %.12s… is not scanned locally for %s (expected %s); run: "
		    "urma names scan --genesis %s --genesis-height H --index %s",
		    genesis, n->chain, idx, genesis, idx);
		g_free(idx);
		return NULL;
	}
	argv = cliargs(n->chain, "names", "resolve", "--network", n->chain, "--index", idx, name, NULL);
	root = clijson(n->chain, argv, err);
	g_strfreev(argv);
	g_free(idx);
	if (!root)
		return NULL;
	/* The resolver must answer for this chain and this registry; anything
	 * else is a wrong index on disk, never a resolution. */
	o = JSON_NODE_HOLDS_OBJECT(root) ? json_node_get_object(root) : NULL;
	if (!o || g_strcmp0(json_object_get_string_member(o, "network"), n->chain)
	    || g_strcmp0(json_object_get_string_member(o, "registry"), genesis)) {
		g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
		    "the local index does not answer for registry %.12s… on %s", genesis, n->chain);
		json_node_unref(root);
		return NULL;
	}
	return root;
}

gboolean
urma_resolve(const char *net, const char *name, const char *genesis,
             char txid[65], GError **err)
{
	const char *g = genesis && *genesis ? genesis : urma_active_genesis(net);
	char *key;
	struct resolved *hit;
	JsonNode *root, *res;
	JsonObject *o;
	gboolean ok = FALSE;

	if (!g) {
		g_set_error(err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		    "no active registry for .%s; open an explicit registry address "
		    "urma://<GENESIS>.<name>.%s/ once, or use a direct TXID address", net, net);
		return FALSE;
	}
	if (!resolutions)
		resolutions = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	key = g_strdup_printf("%s/%s/%s", net, g, name);
	hit = g_hash_table_lookup(resolutions, key);
	if (hit && g_get_monotonic_time() - hit->when < 5 * G_USEC_PER_SEC) {
		g_strlcpy(txid, hit->txid, 65);
		g_free(key);
		return TRUE;
	}
	if (!(root = resolvejson(net, name, g, err))) {
		g_free(key);
		return FALSE;
	}
	o = json_node_get_object(root);
	res = json_object_has_member(o, "resolution") ? json_object_get_member(o, "resolution") : NULL;
	if (res && JSON_NODE_HOLDS_VALUE(res)) {
		g_set_error(err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "%s.%s is %s in registry %.12s…",
		    name, net, json_node_get_string(res), g);
	} else if (res && JSON_NODE_HOLDS_OBJECT(res)
	           && json_object_has_member(json_node_get_object(res), "Bound")) {
		JsonObject *b = json_object_get_object_member(json_node_get_object(res), "Bound");
		JsonNode *t = json_object_get_member(b, "target");
		if (t && JSON_NODE_HOLDS_OBJECT(t)
		    && json_object_has_member(json_node_get_object(t), "Publication")) {
			const char *tx = json_object_get_string_member(json_node_get_object(t), "Publication");
			if (ishex64(tx)) {
				hit = g_new0(struct resolved, 1);
				g_strlcpy(hit->txid, tx, 65);
				hit->when = g_get_monotonic_time();
				g_strlcpy(txid, tx, 65);
				g_hash_table_insert(resolutions, g_strdup(key), hit);
				ok = TRUE;
			}
		}
		if (!ok)
			g_set_error(err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			    "%s.%s is reserved without a publication", name, net);
	} else {
		g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED, "unexpected resolver output");
	}
	json_node_unref(root);
	g_free(key);
	return ok;
}

/* ---- presentation ------------------------------------------------------ */

char *
urma_display_uri(const char *docuri, const char *net, const char *name,
                 const char *genesis)
{
	UrmaAddr a;
	char *tail, *out;
	char tx[65];

	if (!name || !net || !urma_parse_uri(docuri, &a, NULL) || a.kind != URMA_ADDR_PUB
	    || strcmp(a.net, net) || !urma_resolve(net, name, genesis, tx, NULL)
	    || strcmp(tx, a.txid))
		return g_strdup(docuri);
	tail = urma_tail(docuri);
	out = g_strdup_printf("urma://%s.%s%s", name, net, tail);
	g_free(tail);
	return out;
}

char *
urma_dns_uri(const char *docuri, const char *net, const char *name, const char *genesis)
{
	UrmaAddr a;
	char *tail, *out;

	if (!name || !net || !genesis || !*genesis || !urma_parse_uri(docuri, &a, NULL)
	    || a.kind != URMA_ADDR_PUB || strcmp(a.net, net))
		return g_strdup(docuri);
	tail = urma_tail(docuri);
	out = g_strdup_printf("urma://%s.%s.%s%s", genesis, name, net, tail);
	g_free(tail);
	return out;
}

/* ---- the verified store ------------------------------------------------- */

struct entry { char *path, *mime, *sha256; gint64 length; };
struct manifest {
	char *chain, *root, *author, *label, *entry, *tiphash;
	gint64 tipheight;
	GPtrArray *entries;      /* struct entry: files then pinned objects */
	guint nfiles, npins;
};

static GHashTable *manifests;   /* "chain/root" -> struct manifest */

static void
freeentry(gpointer p)
{
	struct entry *e = p;
	g_free(e->path);
	g_free(e->mime);
	g_free(e->sha256);
	g_free(e);
}

static void
freemanifest(gpointer p)
{
	struct manifest *m = p;
	g_free(m->chain);
	g_free(m->root);
	g_free(m->author);
	g_free(m->label);
	g_free(m->entry);
	g_free(m->tiphash);
	g_ptr_array_unref(m->entries);
	g_free(m);
}

static char *
manifestpath(const char *chain, const char *root)
{
	char *store = storedir(), *name = g_strconcat(root, ".json", NULL);
	char *p = g_build_filename(store, "publications", chain, name, NULL);
	g_free(store);
	g_free(name);
	return p;
}

static void
addentries(struct manifest *m, JsonObject *o, const char *member, guint *count)
{
	JsonArray *arr;
	guint i, n;

	*count = 0;
	if (!json_object_has_member(o, member))
		return;
	arr = json_object_get_array_member(o, member);
	n = json_array_get_length(arr);
	for (i = 0; i < n; i++) {
		JsonObject *f = json_array_get_object_element(arr, i);
		struct entry *e = g_new0(struct entry, 1);
		e->path = g_strdup(json_object_get_string_member(f, "path"));
		e->mime = g_strdup(json_object_get_string_member(f, "mime"));
		e->sha256 = g_strdup(json_object_get_string_member(f, "sha256"));
		e->length = json_object_get_int_member(f, "length");
		g_ptr_array_add(m->entries, e);
		(*count)++;
	}
}

/* The manifest of a stored publication is never read from a file: the CLI
 * re-verifies the stored commit/reveal proofs and the authenticated package
 * (`urma web manifest`) and hands back the declared set; only that view is
 * cached, per process. A pointer file that is absent means "not stored";
 * every other failure is reported as it is (tampered, incomplete, ...). */
static struct manifest *
loadmanifest(const char *chain, const char *root, GError **err)
{
	char *key = g_strdup_printf("%s/%s", chain, root), *path, *store;
	struct manifest *m;
	JsonNode *node;
	JsonObject *o;
	gchar **argv;

	if (!manifests)
		manifests = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, freemanifest);
	if ((m = g_hash_table_lookup(manifests, key))) {
		g_free(key);
		return m;
	}
	path = manifestpath(chain, root);
	if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
		g_set_error(err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "not in the store: %s", path);
		g_free(path);
		g_free(key);
		return NULL;
	}
	g_free(path);
	store = storedir();
	argv = cliargs(chain, "web", "manifest", "--store", store, "--network", chain, "--root", root, NULL);
	node = clijson(chain, argv, err);
	g_strfreev(argv);
	g_free(store);
	if (!node) {
		g_free(key);
		return NULL;
	}
	o = JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
	if (!o || g_strcmp0(json_object_get_string_member(o, "format"), "URMA-WEB-STORE-2")
	    || g_strcmp0(json_object_get_string_member(o, "network"), chain)
	    || g_strcmp0(json_object_get_string_member(o, "root"), root)) {
		g_set_error(err, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		    "the verified manifest does not describe %s on %s", root, chain);
		json_node_unref(node);
		g_free(key);
		return NULL;
	}
	m = g_new0(struct manifest, 1);
	m->chain = g_strdup(chain);
	m->root = g_strdup(root);
	m->author = g_strdup(json_object_get_string_member(o, "author"));
	m->label = g_strdup(json_object_get_string_member(o, "label"));
	m->entry = g_strdup(json_object_get_string_member(o, "entry"));
	m->tiphash = g_strdup(json_object_get_string_member(o, "tip_hash"));
	m->tipheight = json_object_get_int_member(o, "tip_height");
	m->entries = g_ptr_array_new_with_free_func(freeentry);
	addentries(m, o, "files", &m->nfiles);
	addentries(m, o, "pinned", &m->npins);
	g_hash_table_insert(manifests, key, m);
	json_node_unref(node);
	return m;
}

/* Object bytes, re-verified against the manifest on every read. */
static gboolean
readobject(const struct entry *e, char **data, gsize *len, GError **err)
{
	char *store = storedir(), *path = g_build_filename(store, "objects", e->sha256, NULL);
	char *sum;
	gboolean ok;

	g_free(store);
	ok = g_file_get_contents(path, data, len, err);
	g_free(path);
	if (!ok)
		return FALSE;
	sum = g_compute_checksum_for_data(G_CHECKSUM_SHA256, (const guchar *)*data, *len);
	if ((gint64)*len != e->length || g_ascii_strcasecmp(sum, e->sha256)) {
		g_set_error(err, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		    "store object %s failed re-verification", e->sha256);
		g_free(*data);
		*data = NULL;
		ok = FALSE;
	}
	g_free(sum);
	return ok;
}

/* ---- scheme handler: the declared set, nothing else ---------------------- */

static void
refuse(WebKitURISchemeRequest *r, const char *fmt, const char *arg)
{
	GError *err = g_error_new(G_IO_ERROR, G_IO_ERROR_NOT_FOUND, fmt, arg);
	webkit_uri_scheme_request_finish_error(r, err);
	g_error_free(err);
}

static void
serve(WebKitURISchemeRequest *r, const char *mime, char *body, gsize len)
{
	GInputStream *s;
	WebKitURISchemeResponse *resp;
	SoupMessageHeaders *h;

	s = g_memory_input_stream_new_from_data(body, len, g_free);
	resp = webkit_uri_scheme_response_new(s, (gint64)len);
	webkit_uri_scheme_response_set_status(resp, 200, "OK");
	webkit_uri_scheme_response_set_content_type(resp, mime);
	h = soup_message_headers_new(SOUP_MESSAGE_HEADERS_RESPONSE);
	/* fetch() responses need the header itself: WebAssembly streaming
	 * checks Response.headers, not the resource's content type. */
	soup_message_headers_append(h, "Content-Type", mime);
	/* On every response, not only documents: a worker's policy is the one
	 * delivered with the worker script, so importScripts()/fetch()/import()
	 * inside workers stay inside the publication too. */
	soup_message_headers_append(h, "Content-Security-Policy", csp);
	webkit_uri_scheme_response_set_http_headers(resp, h);
	webkit_uri_scheme_request_finish_with_response(r, resp);
	g_object_unref(resp);
	g_object_unref(s);
}

/* "/" -> entry; "dir/" -> "dir/index.html"; exact match otherwise. */
static char *
wanted(const struct manifest *m, const char *path)
{
	if (!strcmp(path, "/"))
		return g_strdup(m->entry);
	if (g_str_has_suffix(path, "/"))
		return g_strconcat(path + 1, "index.html", NULL);
	return g_strdup(path + 1);
}

static void
servefromstore(WebKitURISchemeRequest *r, const struct manifest *m, const char *path)
{
	char *rel = wanted(m, path), *data = NULL;
	gsize len = 0;
	GError *err = NULL;
	guint i;

	for (i = 0; i < m->entries->len; i++) {
		const struct entry *e = g_ptr_array_index(m->entries, i);
		if (strcmp(e->path, rel))
			continue;
		if (!readobject(e, &data, &len, &err)) {
			refuse(r, "%s", err->message);
			g_error_free(err);
		} else {
			serve(r, e->mime, data, len);
		}
		g_free(rel);
		return;
	}
	refuse(r, "not in the declared set: %s", rel);
	g_free(rel);
}

/* Asynchronous fetch through the CLI; concurrent requests for the same
 * publication wait for the same process. */
struct waiting { WebKitURISchemeRequest *r; char *path; };
struct fetch { char *chain, *root, *key; GPtrArray *waiting; };
static GHashTable *fetches;

static void
fetchdone(GObject *src, GAsyncResult *res, gpointer data)
{
	GSubprocess *p = G_SUBPROCESS(src);
	struct fetch *f = data;
	GError *err = NULL;
	struct manifest *m = NULL;
	char *errtxt = NULL, *line;
	guint i;

	if (!g_subprocess_wait_check_finish(p, res, &err)) {
		GInputStream *es = g_subprocess_get_stderr_pipe(p);
		GDataInputStream *ds = g_data_input_stream_new(es);
		while ((line = g_data_input_stream_read_line(ds, NULL, NULL, NULL))) {
			g_free(errtxt);
			errtxt = line;   /* keep the last line: the CLI's error */
		}
		g_object_unref(ds);
	} else {
		m = loadmanifest(f->chain, f->root, &err);
	}
	if (m)
		notify(URMA_FETCH_DONE, f->chain, f->root, NULL);
	else
		notify(URMA_FETCH_FAILED, f->chain, f->root,
		       errtxt ? errtxt : (err ? err->message : "unknown"));
	for (i = 0; i < f->waiting->len; i++) {
		struct waiting *w = g_ptr_array_index(f->waiting, i);
		if (m)
			servefromstore(w->r, m, w->path);
		else
			refuse(w->r, "fetch failed: %s", errtxt ? errtxt : (err ? err->message : "unknown"));
		g_object_unref(w->r);
		g_free(w->path);
		g_free(w);
	}
	g_ptr_array_free(f->waiting, TRUE);
	g_hash_table_remove(fetches, f->key);
	g_clear_error(&err);
	g_free(errtxt);
	g_free(f->chain);
	g_free(f->root);
	g_free(f->key);
	g_free(f);
	g_object_unref(p);
}

static void
startfetch(WebKitURISchemeRequest *r, const char *chain, const char *root, const char *path)
{
	char *key = g_strdup_printf("%s/%s", chain, root), *store;
	struct fetch *f;
	struct waiting *w = g_new0(struct waiting, 1);
	GSubprocessLauncher *l;
	GSubprocess *p;
	GError *err = NULL;
	gchar **env, **argv;

	w->r = g_object_ref(r);
	w->path = g_strdup(path);
	if (!fetches)
		fetches = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	if ((f = g_hash_table_lookup(fetches, key))) {
		g_ptr_array_add(f->waiting, w);
		g_free(key);
		return;
	}
	store = storedir();
	argv = cliargs(chain, "web", "fetch", "--root", root, "--store", store, NULL);
	env = clienv(chain);
	l = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
	g_subprocess_launcher_set_environ(l, env);
	p = g_subprocess_launcher_spawnv(l, (const gchar *const *)argv, &err);
	g_object_unref(l);
	g_strfreev(argv);
	g_free(store);
	if (!p) {
		refuse(r, "cannot run the urma client: %s", err->message);
		g_error_free(err);
		g_object_unref(w->r);
		g_free(w->path);
		g_free(w);
		g_free(key);
		return;
	}
	f = g_new0(struct fetch, 1);
	f->chain = g_strdup(chain);
	f->root = g_strdup(root);
	f->key = key;
	f->waiting = g_ptr_array_new();
	g_ptr_array_add(f->waiting, w);
	g_hash_table_insert(fetches, g_strdup(key), f);
	g_subprocess_wait_check_async(p, NULL, fetchdone, f);
	notify(URMA_FETCH_STARTED, chain, root, NULL);
}

/* Initiator check, second layer under the CSP: when WebKit tells us who
 * asked (Referer for fetch/importScripts/module imports, Origin for CORS
 * loads), that initiator must be the same publication. Loads without either
 * header (documents, subframes, classic scripts, images, worker scripts)
 * cannot be attributed here and are governed by the CSP of the document or
 * worker that issued them; WebKit sends no Sec-Fetch-* to scheme handlers. */
/* Does a Referer/Origin value name the target publication? Anything else —
 * another publication or network, a name form, an opaque "null" origin, a
 * foreign scheme, a value shorter than the scheme prefix — is a foreign
 * initiator. The prefix check comes first: no offset into a short value. */
static gboolean
initiatormatches(const char *v, const UrmaAddr *target)
{
	UrmaAddr from;
	char *probe;
	gboolean ok;

	if (!urma_is_uri(v))
		return FALSE;
	/* Origin has no path; give the parser a canonical URI shape. */
	probe = strchr(v + strlen("urma://"), '/') ? g_strdup(v) : g_strconcat(v, "/", NULL);
	ok = urma_parse_uri(probe, &from, NULL) && from.kind == URMA_ADDR_PUB
	     && !strcmp(from.net, target->net) && !strcmp(from.txid, target->txid);
	g_free(probe);
	return ok;
}

/* Absent or empty attribution cannot be judged here; the CSP of the issuing
 * document or worker governs those loads. */
static gboolean
initiatorallowed(const char *v, const UrmaAddr *target)
{
	return !v || !*v || initiatormatches(v, target);
}

static gboolean
sameinitiator(WebKitURISchemeRequest *r, const UrmaAddr *target, char **who)
{
	SoupMessageHeaders *h = webkit_uri_scheme_request_get_http_headers(r);
	const char *keys[] = { "Origin", "Referer" };
	guint i;

	*who = NULL;
	for (i = 0; h && i < G_N_ELEMENTS(keys); i++) {
		const char *v = soup_message_headers_get_one(h, keys[i]);
		if (!initiatorallowed(v, target)) {
			*who = g_strdup(v);
			return FALSE;
		}
	}
	return TRUE;
}

static void
handler(WebKitURISchemeRequest *r, gpointer data)
{
	const char *uri = webkit_uri_scheme_request_get_uri(r);
	UrmaAddr a;
	char *path = NULL, *who = NULL;
	const struct net *n;
	struct manifest *m;
	GError *err = NULL;

	/* Name forms are never served: the main-frame refusal surfaces as
	 * load-failed, where the shell resolves the name; a subframe simply
	 * fails. Non-canonical hosts are refused too (CSP matches them
	 * case-insensitively, the handler must not). */
	if (!urma_parse_uri(uri, &a, &path) || a.kind != URMA_ADDR_PUB || !(n = findnet(a.net))) {
		g_free(path);
		refuse(r, "not a canonical publication address: %s", uri);
		return;
	}
	if (!sameinitiator(r, &a, &who)) {
		char *msg = g_strdup_printf("%s asked for a resource of another publication", who);
		refuse(r, "%s", msg);
		g_free(msg);
		g_free(who);
		g_free(path);
		return;
	}
	if ((m = loadmanifest(n->chain, a.txid, &err))) {
		servefromstore(r, m, path);
	} else if (g_error_matches(err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
		startfetch(r, n->chain, a.txid, path);
	} else {
		refuse(r, "%s", err->message);
	}
	g_clear_error(&err);
	g_free(path);
}

void
urma_harden(WebKitSettings *settings)
{
	g_object_set(settings,
	    "enable-dns-prefetching", FALSE,
	    "enable-webrtc", FALSE,
	    "enable-media-stream", FALSE,
	    "javascript-can-open-windows-automatically", FALSE,
	    NULL);
}

void
urma_register(WebKitWebContext *ctx)
{
	WebKitNetworkProxySettings *proxy;

	webkit_web_context_register_uri_scheme(ctx, "urma", handler, NULL, NULL);
	webkit_security_manager_register_uri_scheme_as_secure(
	    webkit_web_context_get_security_manager(ctx), "urma");
	/* Pages get no network: every http(s)/ws request dies at a closed
	 * loopback port. The URMA client that recovers objects is separate. */
	proxy = webkit_network_proxy_settings_new("http://127.0.0.1:1", NULL);
	webkit_website_data_manager_set_network_proxy_settings(
	    webkit_web_context_get_website_data_manager(ctx),
	    WEBKIT_NETWORK_PROXY_MODE_CUSTOM, proxy);
	webkit_network_proxy_settings_free(proxy);
}

/* ---- downloads: from the verified store, never through the engine ------- */

char *
urma_save(const char *uri, GtkWindow *parent)
{
	UrmaAddr a;
	const struct net *n;
	struct manifest *m;
	char *path = NULL, *rel, *data = NULL, *file = NULL, *msg, *base;
	const struct entry *found = NULL;
	gsize len = 0;
	GError *err = NULL;
	GtkWidget *chooser;
	guint i;

	if (!urma_parse_uri(uri, &a, &path) || a.kind != URMA_ADDR_PUB || !(n = findnet(a.net))) {
		g_free(path);
		return g_strdup("not a canonical publication address");
	}
	if (!(m = loadmanifest(n->chain, a.txid, &err))) {
		msg = g_strdup_printf("cannot save: %s (open the publication first)", err->message);
		g_clear_error(&err);
		g_free(path);
		return msg;
	}
	rel = wanted(m, path);
	g_free(path);
	for (i = 0; i < m->entries->len && !found; i++) {
		const struct entry *e = g_ptr_array_index(m->entries, i);
		if (!strcmp(e->path, rel))
			found = e;
	}
	if (!found) {
		msg = g_strdup_printf("not in the declared set: %s", rel);
		g_free(rel);
		return msg;
	}
	if (!readobject(found, &data, &len, &err)) {
		msg = g_strdup_printf("cannot save: %s", err->message);
		g_clear_error(&err);
		g_free(rel);
		return msg;
	}
	base = g_path_get_basename(rel);
	chooser = gtk_file_chooser_dialog_new("Save declared resource", parent,
	    GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL,
	    "_Save", GTK_RESPONSE_ACCEPT, NULL);
	gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(chooser), TRUE);
	gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser), base);
	if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT)
		file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
	gtk_widget_destroy(chooser);
	if (!file) {
		msg = g_strdup_printf("save cancelled: %s", rel);
	} else if (!g_file_set_contents(file, data, (gssize)len, &err)) {
		msg = g_strdup_printf("cannot write %s: %s", file, err->message);
		g_clear_error(&err);
	} else {
		msg = g_strdup_printf("saved %s (%" G_GSIZE_FORMAT " bytes, sha256 %.12s…, %s) to %s",
		    rel, len, found->sha256, found->mime, file);
	}
	g_free(base);
	g_free(file);
	g_free(data);
	g_free(rel);
	return msg;
}

/* ---- details ------------------------------------------------------------- */

char *
urma_summary(const char *docuri)
{
	UrmaAddr a;
	const struct net *n;
	struct manifest *m;
	char *key;

	if (!manifests || !urma_parse_uri(docuri, &a, NULL) || a.kind != URMA_ADDR_PUB
	    || !(n = findnet(a.net)))
		return NULL;
	key = g_strdup_printf("%s/%s", n->chain, a.txid);
	m = g_hash_table_lookup(manifests, key);
	g_free(key);
	if (!m)
		return NULL;
	return g_strdup_printf("Verified from the store: %s\n"
	    "%u files, %u pinned objects, entry %s\n"
	    "Author %.16s…\nRoot %.16s… on %s (verified at height %" G_GINT64_FORMAT ")",
	    m->label && *m->label ? m->label : "(no label)", m->nfiles, m->npins,
	    m->entry, m->author, m->root, n->chain, m->tipheight);
}

char *
urma_details(const char *docuri, const char *name, const char *genesis)
{
	UrmaAddr a;
	const struct net *n;
	struct manifest *m;
	GString *s = g_string_new("");
	GError *err = NULL;

	if (!urma_parse_uri(docuri, &a, NULL) || a.kind != URMA_ADDR_PUB || !(n = findnet(a.net)))
		return g_string_free(s, FALSE);
	g_string_append_printf(s, "Network: %s (.%s)\nRoot TXID: %s\n", n->chain, a.net, a.txid);
	if ((m = loadmanifest(n->chain, a.txid, &err))) {
		g_string_append_printf(s,
		    "Provenance (author key): %s\nLabel: %s\nEntry: %s\n"
		    "Declared set: %u files, %u pinned objects\n"
		    "Verified from the store written at height %" G_GINT64_FORMAT " (%s)\n",
		    m->author, m->label, m->entry, m->nfiles, m->npins, m->tipheight, m->tiphash);
	} else {
		g_string_append_printf(s, "Not verified: %s\n", err->message);
		g_clear_error(&err);
	}
	if (name && *name) {
		const char *g = genesis && *genesis ? genesis : urma_active_genesis(a.net);
		JsonNode *root = g ? resolvejson(a.net, name, g, &err) : NULL;
		g_string_append_printf(s, "\nName: %s.%s\nRegistry: %s\n", name, a.net, g ? g : "(none active)");
		if (root) {
			JsonObject *o = json_node_get_object(root);
			JsonNode *res = json_object_get_member(o, "resolution");
			g_string_append_printf(s, "Resolver height: %" G_GINT64_FORMAT " (%s)\n",
			    json_object_get_int_member(o, "height"),
			    json_object_get_string_member(o, "block_hash"));
			if (JSON_NODE_HOLDS_OBJECT(res) && json_object_has_member(json_node_get_object(res), "Bound")) {
				JsonObject *b = json_object_get_object_member(json_node_get_object(res), "Bound");
				g_string_append_printf(s, "State: bound\nOwner: %s\nExpires at height: %" G_GINT64_FORMAT
				    "\nClaim TXID: %s\nLast record: %s\n",
				    json_object_get_string_member(b, "owner"),
				    json_object_get_int_member(b, "expiry"),
				    json_object_get_string_member(b, "claim_txid"),
				    json_object_get_string_member(b, "last_txid"));
			} else if (JSON_NODE_HOLDS_VALUE(res)) {
				g_string_append_printf(s, "State: %s\n", json_node_get_string(res));
			}
			json_node_unref(root);
		} else if (err) {
			g_string_append_printf(s, "Resolver: %s\n", err->message);
			g_clear_error(&err);
		}
	}
	return g_string_free(s, FALSE);
}
