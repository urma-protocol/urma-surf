# Embedded fonts

Compiled into the `urma-browser` executable (`fonts.c`, `.incbin`) so the chrome and the
engine's default families need no font package and nothing from the network.

| File | Family | Source | License |
| --- | --- | --- | --- |
| `AdwaitaSans-Regular.ttf` | Adwaita Sans (variable, derived from Inter) | adwaita-fonts 51.0, https://gitlab.gnome.org/GNOME/adwaita-fonts | SIL OFL 1.1 (`LICENSE`) |
| `AdwaitaMono-Regular.ttf` | Adwaita Mono (derived from Iosevka) | adwaita-fonts 51.0, same | SIL OFL 1.1 (`LICENSE`) |

SHA-256 of the copies taken on 2026-10-03 from the Arch package `adwaita-fonts 51.0-2`:

```
8381c33b9a44f066f2b99dba3d416a2342891e28c956a35dfd8d16ee2987e6d4  AdwaitaSans-Regular.ttf
ffc549b7fc28057d624086111f1ba0b1f752d2d7ff7c5363dfa064e4dca17d47  AdwaitaMono-Regular.ttf
```

The files are unmodified and retain their original names. `LICENSE` is the
package's license text, copyright lines included.

How they reach the screen: fontconfig only loads fonts from files, so at start-up
the browser writes the two files into `$XDG_RUNTIME_DIR/urma-surf/fonts/` (0700/0600,
rewritten only when missing or of another size) and registers that directory as an
application font directory in its own process (GTK chrome) and, through the web
extension's initialization user data, in every WebKit web process (page rendering).
The engine's default, sans-serif and monospace families are set to these two
families. Pages that name their own fonts are unaffected; serif stays whatever the
system has.

The byte hashes above match the recorded copies. Package provenance is the
original import record; the source release does not claim a fresh verification
against a downloaded Arch/GNOME package. The embedded Mono copyright metadata
says 2015–2026; the package's bundled notice says 2015–2025. Both original files
are preserved, without changing the notice or font metadata. `make install`
copies the complete OFL text and this provenance into `share/doc/urma-browser/fonts/`.
