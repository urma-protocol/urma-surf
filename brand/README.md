# Embedded URMA Browser logo

The operator approved Browser logo variant 5 on 2026-10-11 for local
integration and visual review. It replaces the simpler website mark.
`urma-browser.svg` is a byte-for-byte copy of the supplied
`urma-browser-draft-v5.svg`. The original SVG title and geometry are retained,
including the draft filename in this provenance; no element was redrawn.

The design combines the URMA U and corner frame with a globe and padlock.
The viewBox is 512 × 512. Rasterization retains the entire viewBox and its
original margins, colors, opacity and mask, on a transparent background.
The supplied PNG preview has a dark presentation background; that background
is not added to the embedded icons.

Approved vector SHA-256:
`8367a99206055d8d405b46d5481b3205e6065746709592be63a8716c3ce0b374`.
Supplied preview PNG SHA-256 (reference only, not a build input):
`3e3dd3862e2f3aad1307b63eb999bba3d91983ea514649ec76b109abad89582d`.

## Regeneration

From any working directory:

```sh
python3 /path/to/checkout/brand/render.py
```

Requires `rsvg-convert` (librsvg). The recorded rendering used librsvg 2.62.4
and Cairo 1.18.6. Two consecutive runs produced identical PNG bytes. Different
renderer versions may rasterize edges differently; the retained inputs below
are the exact tested files. No renderer is needed to build or run the Browser.

Sizes are 16, 22, 32, 44, 48, 64, 128 and 256 px. The bar uses 22 px at scale 1
and 44 px at scale 2. The window icon list includes every size. `brand.c`
embeds the PNG bytes and decodes them from memory, without runtime file or
network access. Existing raster filenames are kept as internal build names.

```text
430006336694cebfc57f10573493e1729391ea1dd5ef3002c5b4bbf14648be39  urma-mark-16.png
a96f55da144b663981f10517a0effca925811ccde15818d49fccad87c53119b5  urma-mark-22.png
d0a2a4ac52f6839aaf551b13f8c3888026117ff125a8358b09dc65f554b4726b  urma-mark-32.png
f0d1af25e950f2f3c381825eaf526383cec4d9b135a0cf8dcd107a05cd519a0d  urma-mark-44.png
069d43856985212bc570c51b16c0d68db2f3587a9ab3966d6212f047592d9c9a  urma-mark-48.png
fed0541987d291600ed9eb52b27dd9fe388c6e7bc6c82cf759667c824b23ec52  urma-mark-64.png
93806d7e33fddaa7ae223d7ba584d4e01fae6cbc5014789edee2bcfcd3a64209  urma-mark-128.png
7dff18aeae85bd0cedfa7c82184d0a0ba6b01b78ca653e39a6593286a5ca8f8e  urma-mark-256.png
```

The operator authorized this logo for local Browser preparation. Public
release in the `urma-protocol` organization awaits approval after the visual
review; this integration does not itself publish the Browser or create
additional trademark terms. See `../NOTICES.md`.
