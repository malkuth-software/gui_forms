# Heroicons provenance

GUI.Forms' stock icons (`include/gui_forms/resources/stock_icons/`) are the
Heroicons vectors, converted at build time into the library's own path
records. No SVG file ships and nothing parses SVG at run time.

- Project: Heroicons, by Tailwind Labs (https://github.com/tailwindlabs/heroicons)
- Version: v2.2.0, commit `ca7b62e`
- Archive: `heroicons-v2.2.0.tar.gz`, SHA-256
  `c02ffbf2ab304e6b927209083541a9e13a99d76339da480ad0a8eb5ef0fcba6c`
- Licence: MIT (`LICENSE`, copied unchanged from the archive)
- Directories used: `optimized/24/outline` (324 icons), `optimized/24/solid`
  (324), `optimized/20/solid` (324) and `optimized/16/solid` (316). Each
  size is its own drawing; GUI.Forms picks the nearest authored size and
  never scales one size to stand for another.

## Conversion

`tools/stock_icon_codec.py` reads the SVG subset these files use (the
`viewBox`, `path` `d` data, `rect` with corner radii, and the fill, stroke,
cap, join and fill-rule attributes) and refuses anything else. Arcs and
rounded corners become cubic curves of at most a quarter turn; coordinates
are stored as 16-bit integers in thousandths of a grid unit. The output is
`src/core/resources/stock_icons/stock_icon_data.inc`, committed:

```text
python3 -B tools/stock_icon_codec.py generate --source <archive>/optimized
python3 -B tools/stock_icon_codec.py check --source <archive>/optimized
```

`tools/test_stock_icon_codec.py` holds four of the source files and checks
the round trip and the committed records against them.
