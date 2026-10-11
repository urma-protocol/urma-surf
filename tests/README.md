# Offline Browser gates

These explicit gates do not build or install the Browser or URMA CLI. Run them
against specified binaries, with their adjacent web extension. Generated stores,
profiles, registries, X server and buses belong to temporary directories. No
operator configuration or live node is used.

```
PYTHONDONTWRITEBYTECODE=1 python3 tests/bridge_gate.py --source .
PYTHONDONTWRITEBYTECODE=1 python3 tests/store_gate.py --urma /absolute/path/urma
PYTHONDONTWRITEBYTECODE=1 python3 tests/browser_gate.py --browser /absolute/path/urma-surf --urma /absolute/path/urma
```

The bridge gate compiles a small ASan/UBSan executable and requires the explicit
expected exit status for all 44 parser, URI-tail and initiator cases. A signal,
sanitizer failure or unrelated CLI failure is not a successful rejection. It
never runs a resolver or GUI.

The store gate exercises the actual CLI with four independently signed offline
URMAWEB1 fixture publications. It checks exact manifest metadata/files and
rejects corrupted signatures, payload bytes and network pointers, preserving
input bytes. The optional `--core /path/to/core` records Git provenance; packaged
source needs no Git checkout. This does not prove chain inclusion, funding
consensus or verification of every retained child transaction.

The GUI gate controls the production GTK/WebKit callbacks through a private
Xvfb/accessibility bus (classic dbus-daemon activation, without a user-systemd
session; GIO uses the local filesystem backend). `names resolve` is an explicit mock, `web manifest`
delegates to the supplied real CLI, and `web fetch` can only refuse or wait for a
test-owned release file. Omitting `--urma` selects mock manifests and provides
no CLI authenticity evidence. Each command/poll and the whole session have
bounded deadlines; an owned outer process group cleans descendants on failure.
The default GUI matrix checks visible document content, exact copied addresses
and mock-call provenance for temporary registries across roots/networks, reload,
relative paths, fragment history, parser-time/after-load query pushState, derived
link/script children versus independent tabs, cancellation/refusal, reset epochs,
readable-but-unwritable registry saves, and delayed failure/reset/repeated-URI/tab
close. Permission-based SAVE tests report a qualified skip under root.
The child-frame case separately attempts an undeclared iframe, checks the rendered
parent and unchanged context with no fetch CLI, and observes an explicit fetch
catch marker for a refused non-main resource. It does not assert an iframe load
event or infer an iframe callback from fetch.
`--focus relative`, `--focus query`, `--focus refusal` and `--focus reset` select small diagnostics; they
are not substitutes for the complete matrix. `--focus contexts` additionally
checks old-epoch Back while a newer temporary tuple is active, successful shell
direct-PUB reset, and Global persistence followed by fetch failure preserving
the old document and temporary tuple.

Dependencies include Python GI/AT-SPI, Xvfb, xdotool, xprop, xclip, dbus-daemon,
dbus-run-session, and the Browser's normal GTK/WebKit dependencies.

`--evidence /private/output/directory` optionally saves screenshots of fixture
pages and the registry dialog. Those artifacts are not release inputs.

The fixture contains only public test keys and generated publication bytes.
Optional regeneration uses existing independent core-vector primitives, writing
only the explicit output and its own temporary directory:

```
PYTHONDONTWRITEBYTECODE=1 python3 tests/generate_fixture.py --core /path/to/core --output /path/to/authentic.json
```

Names mocks do not establish authenticated registry interoperability. Delayed
mock fetches only refuse: stale failure/cancellation ordering is observed, not
successful obsolete verification. Callback checks observe actual WebKit ordering, not independent
per-callback request tokens or proof of every identical-URI race. No live-network
acceptance is claimed.

The start-page gate launches the Browser with no URL and checks its native
about:blank presentation, actions, independent tabs, reload, explicit-blank
history, compact layout, and page-created blank-window content. It requires
zero CLI calls and uses no real CLI or network:

```
PYTHONDONTWRITEBYTECODE=1 python3 tests/startpage_gate.py --browser /absolute/path/urma-surf --evidence /private/output/directory
```
