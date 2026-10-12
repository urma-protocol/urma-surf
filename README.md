# URMA Surf

URMA Surf is an experimental Linux browser based on [Surf](https://surf.suckless.org/)
and WebKitGTK for the [URMA protocol](https://urma.rosint.org/spec/).
This public source version is `0.1.0-experimental`, not a stable release. It has an inline address bar, native tabs,
publication details and registry settings. The separate `urma` CLI verifies
and recovers URMA content; it is not bundled in this repository.

## Build from a source checkout or archive

Use Linux, a C compiler compatible with GNU assembly `.incbin`, make,
pkg-config, GNU tar and gzip. Development packages must provide:

- `gtk+-3.0`, `gcr-3`, `webkit2gtk-4.1`;
- `webkit2gtk-web-extension-4.1`, `gio-2.0`, `x11`;
- `json-glib-1.0` and `fontconfig`.

The browser and its web-process extension use dynamically linked system
libraries. The two embedded fonts and eight mark rasters are source inputs,
not downloaded at build or runtime. A working graphical session is required
to browse. The regression harness also uses Python 3, Xvfb and xdotool for
its GUI gate; see `tests/README.md`.

```sh
git clone https://github.com/urma-protocol/urma-surf.git
cd urma-surf
make -j2
./urma-surf
```

The extension `webext-surf.so` must remain beside the local executable, or
be installed in the configured library directory. `make surf` creates an
optional local compatibility symlink; it is not installed. The source files
retain their upstream names. Edit `config.mk` for build paths and
`config.def.h` for defaults. The first build copies defaults to ignored
`config.h`; an existing local `config.h` is preserved.

## Install and package

The installed executable, manual and library directory use the
`urma-surf` namespace, leaving the upstream `surf` installation alone.
The default prefix is `/usr/local`. To stage an installation without
changing the system:

```sh
make -j2 install DESTDIR=/absolute/path/to/staging PREFIX=/usr/local
make uninstall DESTDIR=/absolute/path/to/staging PREFIX=/usr/local
```

Use the same prefix/library/documentation options for installation and
uninstallation. The staged tree contains the executable, extension, manual,
MIT/X notice, complete OFL text and asset provenance under
`share/doc/urma-surf/`. Uninstallation removes those named files only
and leaves unrelated files in shared directories. The legacy upstream
`surf-open.sh` remains in the source archive for provenance; native tabs do
not require it, tabbed or dmenu. `FAQ.md` and `TODO.md` are retained upstream
historical documents, rather than current URMA Surf instructions.

```sh
make dist
```

This creates `urma-surf-0.1.0-experimental.tar.gz` from explicit source
inputs and an explicit list of approved test files. Git metadata, generated configuration,
objects, executables, profiles and Python caches are excluded. The target
does not clean an existing build. Tar entries are sorted with normalized
ownership, permissions and time; gzip omits its timestamp. Override time
with `make dist SOURCE_DATE_EPOCH=SECONDS`. Identical inputs and this value
produce identical archive bytes. `make clean` removes local build products;
`make distclean` additionally removes local `config.h` and the archive.

## Install the separate CLI from the intended source revision

This development checkout targets URMA core commit
`b5013a49629f0ea27f2068d114bcc35a0027f094`, including the optional Names client, from
https://github.com/urma-protocol/urma. Local source commits may not yet be
available on that remote. Obtain the exact authorized checkout first, rather
than treating an arbitrary latest package as the tested version. From that
checkout, with a stable Rust toolchain:

```sh
git rev-parse HEAD
cargo install --path urma-cli --locked
```

Set `URMA_BIN` to the absolute installed executable, or put `urma` on
PATH. Browser build success alone does not demonstrate CLI compatibility;
the authentic-store gate below checks the actual chosen CLI separately.

## Runtime configuration

| Variable or file | Purpose |
| --- | --- |
| `URMA_BIN` | CLI executable; default `urma` on PATH. |
| `URMA_WEB_STORE` | Verified web store; default `$XDG_DATA_HOME/urma/web-store`, falling back to `~/.local/share/urma/web-store`. |
| `URMA_NAMES_DIR` | Names indexes; default `$XDG_DATA_HOME/urma/names`, falling back to `~/.local/share/urma/names`. |
| `URMA_CONFIG` | CLI configuration file; inherited by the browser's CLI calls. |
| `URMA_RPC_URL`, `URMA_NODE_AUTH_FILE` | Optional CLI local node connection and cookie-file path; both are needed for that mode. |
| `~/.surf/urma-registries.ini` | Persistent active registry slots under `[active]`, keyed by `ltc`, `btc`, `tltc`, `tbtc`. |
| `~/.surf/` | Inherited browser profile: cookies, cache and user styles. Native tabs share the process's WebKit profile. |

The CLI uses its configured chain source; a missing publication can trigger
an asynchronous `urma web fetch`. That operation can contact chain sources.
A retained store can be inspected offline. Node credentials, cookies, user
profiles, indexes and stores are local state and are not release inputs.

Addresses use `urma://ROOT_TXID.NET/`, `urma://NAME.NET/` or
`urma://GENESIS.NAME.NET/`. Networks are `.ltc` (Litecoin mainnet),
`.tltc` (Litecoin testnet), `.tbtc` (Bitcoin testnet4) and `.btc`
(Bitcoin mainnet, currently not enabled by the reference CLI). The
`.rbtc` regtest form is for isolated tests with `URMA_SURF_REGTEST=1`.

No registry is activated by default. A suggested registry is not an active
registry. The browser passes the effective network and registry explicitly to
`urma names resolve`. With an optional `archive` connection in `URMA_CONFIG`,
the CLI can read a registered Names projection from URMA Node without a local
index. The CLI requires a loopback endpoint and a private, bounded bearer file:

```json
{"archive":{"endpoint":"127.0.0.1:8118","token_file":"/absolute/path/to/node-token"}}
```

No daemon, registry or scan is started by this read. An unavailable, stale or
invalid view falls back to the existing local index for that same network and
registry. If neither is usable, navigation refuses; it never selects another
registry or implicitly scans. Without archive configuration, prepare and update
the local index explicitly:

```sh
URMA_NETWORK=litecoin-testnet urma names scan \
  --genesis GENESIS_TXID \
  --index /absolute/names/litecoin-testnet/GENESIS_TXID.json
urma names resolve --network litecoin-testnet --registry GENESIS_TXID \
  --index /absolute/names/litecoin-testnet/GENESIS_TXID.json NAME
```

Use `urma names scan --help` for source and scan limits. Registry slots
and name indexes are distinct: selecting a registry does not create an
index. A direct root TXID bypasses name resolution.

An explicit registry address that differs from the tab's effective registry
offers Temporary, Global or Cancel. Temporary keeps one network–registry pair
in that tab's memory, committed only after successful navigation. Short names
on another network use that network's global slot; a later successful
temporary choice replaces the pair. Global resolves through the selected
registry and saves its persistent slot before loading; a resolve or save
failure refuses that choice rather than falling back to another registry.
Cancel and failed temporary navigation leave the previous document/context.

The tab displays its temporary scope and a Use global action. Use global
stops a pending choice, clears that tab's temporary scope and prevents old
history entries from restoring it. Back/Forward otherwise restore the
entry's committed scope and name binding; relative links and reload retain
the publication context. A tab opened from a page gets an independent copy
of its committed scope. It keeps the copied name binding for the same
publication; another publication has its own binding. Blank independent
tabs start with the global slots. Settings changes persistent global slots,
whereas Temporary changes only the tab. Neither choice changes the
publication's authenticated bytes or origin.

## Verification and browsing limits

Before serving declared content, the browser calls `urma web manifest`
to verify the stored commit/reveal proofs and package. Resource bytes are
checked against the authenticated declared set by SHA-256 and length.
Undeclared paths and cross-publication initiators are refused. Documents
render at a publication origin keyed by network and root TXID, rather than
a mutable name. URMA pages have a content-security policy and an isolated
network context; page HTTP(S)/WebSocket traffic is blocked. The separate
CLI fetch path is still allowed to contact its configured source.

Offline proof and content verification does not establish current chain
inclusion, finality or the network independently. Store network metadata
comes from the source check during fetch; cached names are not a new chain
scan. Optional Node Names views are local operator projections, not independent
proof of current registry state. The browser retains its existing five-second
positive resolution cache; it does not subscribe to reorg events or compare the
projection against the current global tip. A missing local name is relative to
the projection returned by the CLI. Web fetch and manifest verification keep
their existing source paths. The `verified` badge reports these proof/content checks, not a fresh
consensus audit or the truth of a publication. Data availability still
depends on accessible retained records. This experimental version does not
claim mainnet testing.

Declared downloads are saved from the verified store using the save
dialog. Ordinary non-URMA download/external-handler/video actions inherit
the configurable `st` + `curl`, `xdg-open` and `mpv` helpers; install
them if using those actions. dmenu is not needed for the inline address or
find bars.

## Start page

Starting without a URL, opening a blank tab, or entering `about:blank` displays
a local start page with the approved Browser logo, an address action and native
New tab, Settings and Shortcuts buttons. It uses embedded assets and makes no
CLI or network request. The address remains `about:blank`; the presentation
leaves when navigation commits. Page-created blank windows remain available
for the page's own content.

## Keys and tests

Ctrl+L focuses the address; Ctrl+T/W opens/closes a tab; Ctrl+Tab and
Ctrl+Shift+Tab select tabs; Ctrl+1..8/9 select a numbered/last tab.
Ctrl+F finds, Ctrl+R reloads, Ctrl+0 resets zoom, Ctrl+, opens settings,
Ctrl+Shift+D opens publication details and Ctrl+? lists shortcuts.
See the installed `urma-surf(1)` manual for inherited options.

```sh
make check
PYTHONDONTWRITEBYTECODE=1 python3 tests/browser_gate.py \
  --browser /absolute/path/to/urma-surf --urma /absolute/path/to/urma
PYTHONDONTWRITEBYTECODE=1 python3 tests/store_gate.py \
  --urma /absolute/path/to/urma --core /absolute/path/to/core-checkout
```

`make check` runs only the isolated bridge/parser sanitizer gate. GUI
tests use an isolated profile and an adapter: names are synthetic and
fetch refuses without network; with `--urma`, web manifest delegates to
the selected real CLI over committed authentic fixtures. Without that
option, mock manifests are not cryptographic evidence. The separate store
gate materializes a fresh temporary store from those signed fixtures and
uses the selected CLI; `--core` optionally checks checkout provenance.
Regenerating the fixture with an explicit source checkout is separate
from executing the gates. Test results and precise limits
belong in a release receipt, not an assumption that all gates ran.

## Upstream and notices

Base: surf 2.1, official source https://git.suckless.org/surf,
commit `48517e586cdc98bc1af7115674b554cc70c8bc2e` on `surf-webkit2`.
The original copyright and MIT/X terms are preserved in `LICENSE`.
Fonts retain their SIL OFL 1.1 notice in `fonts/LICENSE`.
See `NOTICES.md`, `fonts/README.md` and `brand/README.md` for asset
hashes and provenance. The approved v5 logo and native start page are included.

This repository preserves the original Surf Git history and upstream author
attributions, followed by the URMA integration commits; it is not a new root
commit or an unrelated rewrite. The MIT/X license remains unchanged.
Upstream authors are listed in `LICENSE` and the retained commit history.
