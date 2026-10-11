# Embedded URMA mark

Rasters of the site's original mark, compiled into the `urma-browser` executable (`brand.c`,
`.incbin`) and decoded from memory at run time: no path, no file, no network.

Source: the ROSINT website repository, `subdomains/urma/assets/urma-mark-static.svg`,
served at https://urma.rosint.org/assets/urma-mark-static.svg.
(viewBox 0 0 512 512, lime trace `#d6fa52`, signal frame `#51ff0d` at 0.68 opacity,
status dot). The SVG was not modified and nothing was redrawn.

Recorded renderer: WebKitGTK itself (the experiment's `render_mark.py`, under Xvfb), because the mark
declares its colours as CSS custom properties and both librsvg 2.62 and Inkscape drop
them when rasterizing standalone (the strokes come out empty). WebKit applies them as
a browser does. Transparent background, square viewport equal to the viewBox aspect,
so proportions and the mark's own margins are kept; the mark is not padded or cropped.

Sizes: 16, 22, 32, 44, 48, 64, 128, 256 px. The bar shows the 22 px raster at scale 1
and the 44 px raster at scale 2 (HiDPI: a real raster, not an upscale); the window icon
list carries every size. SHA-256 of the files rendered on 2026-10-03:

```
24b7d6dd6f9fb6a0d6ceafc1b5d2a450cc4a461c0f7de8d49390350ed0c68e57  urma-mark-16.png
ad7ba8457919686ed8112f3d74e5778683d2c24bbcd40a760e175b287ea19dc9  urma-mark-22.png
160797b0ff551b42a7f58bb3f75e94d4bbea7edf77d31b72ccb7d18a92d9f810  urma-mark-32.png
576d267cc7cb81227e3a8ba44e08b24abc1a998dcba30a91eaefe4517395788f  urma-mark-44.png
0a3f159ef1965fe9198750cbf50cd78d901d01625dcb2a86e26d44a44c9d3861  urma-mark-48.png
bdf283efc24af6d2d8739c8a21d90f27dfe6d3fba29f19bb28f1510f6ec91d90  urma-mark-64.png
02295ac9c62b2755981774ae61f94a3093ea197892fee424466f60353ce2f395  urma-mark-128.png
cd4b687de7ecbf6d06dd4f81c6e38da42d1410326e5581dee5488557c6ee09dd  urma-mark-256.png
```

The original SVG is included as `urma-mark-static.svg`, copied byte for byte from
website asset commit `09bab3e24c56f000042ade77f5ff47808ea7e52e` during source
preparation. Its SHA-256 is
`6df728e99465fe8c7af2c08363f8748b80e800588830b678f5a22ddd2e3cafd8`.
The eight PNG hashes above match the retained source inputs. The historical
renderer script is not shipped, so this release does not claim a self-contained
PNG regeneration procedure; no renderer or private parent directory is needed
to compile the retained images.

No explicit redistribution license for the mark was found in the inspected SVG
or website repository. See `../NOTICES.md`: permission remains an operator
decision before public distribution; this provenance does not create a grant.
