# Heroicons provenance

GUI.Forms' stock icons (`include/gui_forms/resources/stock_icons/`) are the
Heroicons vectors. The SVG files here are converted at build time into the
library's own path records; no SVG file ships in the SDK and nothing parses
SVG at run time.

- Project: Heroicons, by Tailwind Labs (https://github.com/tailwindlabs/heroicons)
- Version: tag v2.2.0, commit `ca7b62e`
- Archive: `heroicons-v2.2.0.tar.gz`, SHA-256
  `c02ffbf2ab304e6b927209083541a9e13a99d76339da480ad0a8eb5ef0fcba6c`
- Licence: MIT (`LICENSE`, copied unchanged from the archive)
- Files: the archive's `optimized/24/outline` (324 icons),
  `optimized/24/solid` (324), `optimized/20/solid` (324) and
  `optimized/16/solid` (316), 1,288 SVG files copied byte for byte into
  `24/outline`, `24/solid`, `20/solid` and `16/solid` here, keeping
  upstream's layout. Each size is its own drawing; GUI.Forms picks the
  nearest authored size and never scales one size to stand for another.

## Conversion

`tools/stock_icon_codec.py` reads the SVG subset these files use (the
`viewBox`, `path` `d` data, `rect` with corner radii, and the fill, stroke,
cap, join and fill-rule attributes) and refuses anything else. Arcs and
rounded corners become cubic curves of at most a quarter turn; coordinates
are stored as 16-bit integers in thousandths of a grid unit.

The build runs it (a custom command in `CMakeLists.txt`) and writes
`<build>/generated/stock_icons/stock_icon_data.inc`, which
`src/core/resources/stock_icons/stock_icons.cpp` compiles in. The output is
not committed. By hand:

```text
python3 -B tools/stock_icon_codec.py generate --output <file>
python3 -B tools/stock_icon_codec.py check --output <file>
```

`tools/test_stock_icon_codec.py` holds four of these files, checks that they
are the committed ones, and checks the round trip and the build's generated
records against them and against the whole tree.
