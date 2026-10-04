/* URMA shell logic for the surf experiment: addresses, registries, the
 * verified store bridge, downloads and the details view. See urma.c. */
#ifndef URMA_H
#define URMA_H

#include <glib.h>
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

typedef enum { URMA_ADDR_INVALID = 0, URMA_ADDR_PUB, URMA_ADDR_NAME } UrmaAddrKind;

/* Parsed authority of an urma:// address, canonical form only:
 *   <64hex>.<net>            publication (kind PUB)
 *   <name>.<net>             short form  (kind NAME, genesis[0] == 0)
 *   <64hex>.<name>.<net>     explicit registry (kind NAME, genesis set) */
typedef struct {
	UrmaAddrKind kind;
	char net[8];
	char txid[65];
	char genesis[65];
	char name[64];
} UrmaAddr;

gboolean urma_parse_host(const char *host, UrmaAddr *out);
gboolean urma_parse_uri(const char *uri, UrmaAddr *out, char **path);
gboolean urma_is_net(const char *s);

/* Engine wiring. urma_register: scheme handler over the verified store,
 * secure scheme, page network black hole, once per WebKitWebContext.
 * urma_harden: the settings that close side channels, on every settings
 * object a view starts with. */
void urma_register(WebKitWebContext *ctx);
void urma_harden(WebKitSettings *settings);

/* Active registry per network, global across processes (small ini file).
 * Setting "" (or NULL) clears the slot: name addresses on that network fail
 * until a registry is chosen again; TXID addresses are unaffected. */
const char *urma_active_genesis(const char *net);          /* NULL when none */
void urma_set_active_genesis(const char *net, const char *genesis);

/* Registries the shell can offer for a network: every index scanned locally
 * (NAMES_DIR/<chain>/<genesis>.json, read for display only; resolution still
 * goes through the CLI) plus the genesis documented for the network in
 * docs/urma/names/NAMES_NETWORKS.md, marked `documented`. The active slot is
 * always included so the UI can show it even when no index exists. */
typedef struct {
	char genesis[65];
	char mode[24];        /* "Administered", "Open", "" when unknown */
	gint64 genesis_height, height;   /* -1 when unknown */
	guint names;
	gboolean scanned, documented;
	const char *title;    /* documented registries carry a name, else "" */
} UrmaRegistry;
GArray *urma_registries(const char *net);    /* of UrmaRegistry; caller frees */
const char *const *urma_nets(guint *count);   /* public suffixes, no regtest */
const char *urma_registry_label(const char *net, const char *genesis);
gboolean urma_registry_switch_prompt(GtkWindow *parent, const char *net,
                                     const char *genesis);

/* Resolve a name through the local names index of a registry (genesis NULL =
 * the network's active slot) by running `urma names resolve`. Fills txid. */
gboolean urma_resolve(const char *net, const char *name, const char *genesis,
                      char txid[65], GError **err);

/* Address presentation. */
char *urma_display_uri(const char *docuri, const char *net, const char *name);
char *urma_dns_uri(const char *docuri, const char *net, const char *name,
                   const char *genesis);
char *urma_pub_uri(const char *net, const char *txid, const char *path);
/* Path + query + fragment of an accepted address ("/" when absent). */
char *urma_tail(const char *uri);

/* Downloads of declared resources come straight from the verified store
 * (re-verified), never through the engine: the user picks the destination.
 * Returns a status message (caller frees). */
gboolean urma_is_uri(const char *uri);
char *urma_save(const char *uri, GtkWindow *parent);

/* Human-readable details of the loaded publication and its name binding. */
char *urma_details(const char *docuri, const char *name, const char *genesis);

/* Short description of a publication whose verified manifest this process
 * already holds (the document was served from it); NULL otherwise. Reads the
 * cache only: no CLI call, no new verification. */
char *urma_summary(const char *docuri);

/* On-demand recovery through the CLI, reported to the shell so the chrome can
 * show it: one STARTED per spawned fetch, then DONE or FAILED (detail = the
 * CLI's last error line, or NULL). */
typedef enum { URMA_FETCH_STARTED, URMA_FETCH_DONE, URMA_FETCH_FAILED } UrmaFetchEvent;
typedef void (*UrmaFetchNotify)(UrmaFetchEvent ev, const char *net,
                                const char *txid, const char *detail,
                                gpointer user);
void urma_set_fetch_notify(UrmaFetchNotify cb, gpointer user);

#endif
