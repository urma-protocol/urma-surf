/* Bridge unit probe: compiles urma.c in and exercises the parser and the
 * resolver without a browser. usage: bridge_test parse URI | tail URI | initiator TARGET VALUE | resolve NET NAME GENESIS */
#include "../urma.c"
#include <stdio.h>

int
main(int argc, char **argv)
{
	if (argc > 2 && !strcmp(argv[1], "parse")) {
		UrmaAddr a;
		char *path = NULL;
		gboolean ok = urma_parse_uri(argv[2], &a, &path);
		printf("accepted=%d kind=%d net=%s txid=%s name=%s genesis=%s path=%s\n", ok, a.kind,
		       a.net, a.txid, a.name, a.genesis, path ? path : "");
		g_free(path);
		return ok ? 0 : 1;
	}
	if (argc > 2 && !strcmp(argv[1], "tail")) {
		char *t = urma_tail(argv[2]);
		printf("tail=%s\n", t);
		g_free(t);
		return 0;
	}
	if (argc > 3 && !strcmp(argv[1], "initiator")) {
		/* value "-" = header absent; the copy has the exact size, so a read
		 * past a short value is a heap overflow under AddressSanitizer */
		UrmaAddr target;
		char *v = strcmp(argv[3], "-") ? g_strdup(argv[3]) : NULL;
		gboolean ok;
		if (!urma_parse_uri(argv[2], &target, NULL) || target.kind != URMA_ADDR_PUB) {
			fprintf(stderr, "target must be a publication address\n");
			return 2;
		}
		ok = initiatorallowed(v, &target);
		printf("allowed=%d value=%s\n", ok, v ? v : "(absent)");
		g_free(v);
		return ok ? 0 : 1;
	}
	if (argc > 4 && !strcmp(argv[1], "resolve")) {
		char tx[65];
		GError *err = NULL;
		gboolean ok = urma_resolve(argv[2], argv[3], argv[4], tx, &err);
		printf("resolved=%d txid=%s err=%s\n", ok, ok ? tx : "", err ? err->message : "");
		return ok ? 0 : 1;
	}
	fprintf(stderr, "usage: bridge_test parse URI | tail URI | initiator TARGET VALUE | resolve NET NAME GENESIS\n");
	return 2;
}
