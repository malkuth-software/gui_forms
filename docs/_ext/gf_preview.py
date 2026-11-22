"""The preview directive: one captured image of a control or a feature.

Previews are captured headless in the windows-professional theme at scale 2
(tools/capture_previews.py) and kept outside this repository, in the
previews repository, one folder per GUI.Forms commit. docs/conf.py reads the
pinned capture (docs/previews.json) into two settings: ``gf_media``, the
folder's address (the {{ gf_media }} substitution), and ``gf_previews``,
each stored image's pixel size. A page names an image and its alt text:

    ```{gf-preview} controls/button
    :alt: A Button in the windows-professional theme.
    ```

(``.. gf-preview:: controls/button`` with the same option in
reStructuredText). The image is shown at half its pixel size, so a scale-2
capture is sharp on a high-density display. A preview the pin does not list
(not captured yet, or no capture stored at all) is left out without a
warning, so a page can name its preview before the image exists; in the
browser, an image that fails to load removes itself. Alt text is required:
a preview without it is a build error. Only HTML builders show previews.
"""
from __future__ import annotations

import html
import re
from collections.abc import Mapping

NAME = re.compile(r"[a-z0-9][a-z0-9_.-]*(?:/[a-z0-9][a-z0-9_.-]*)*")


def preview_size(entry: Mapping[str, object]) -> tuple[int, int] | None:
    """The shown size (half the stored pixel size, at least 1), or None for an
    entry without a usable size."""
    try:
        width = int(entry.get("width", 0))
        height = int(entry.get("height", 0))
    except (TypeError, ValueError):
        return None
    if width <= 0 or height <= 0:
        return None
    return max(1, width // 2), max(1, height // 2)


def preview_html(name: str, alt: str, media: str, previews: Mapping[str, Mapping[str, object]],
                 caption: str = "") -> str:
    """The figure for `name`, or "" when the pin does not list it (or lists it
    without a size, or there is no media address). ValueError for a name
    outside the previews' layout or empty alt text."""
    if NAME.fullmatch(name) is None:
        raise ValueError(f"gf-preview: {name!r} is not a preview name (lower case, '/' between folders)")
    if not alt.strip():
        raise ValueError(f"gf-preview {name}: alt text is required")
    entry = previews.get(name)
    if entry is None or not media:
        return ""
    size = preview_size(entry)
    if size is None:
        return ""
    source = html.escape(f"{media.rstrip('/')}/{name}.png", quote=True)
    parts = [
        '<figure class="gf-preview">',
        f'<img src="{source}" alt="{html.escape(alt.strip(), quote=True)}" width="{size[0]}" '
        f'height="{size[1]}" loading="lazy" decoding="async" onerror="this.parentNode.remove()">',
    ]
    if caption.strip():
        parts.append(f"<figcaption>{html.escape(caption.strip())}</figcaption>")
    parts.append("</figure>")
    return "".join(parts)


def setup(app) -> dict[str, object]:
    from docutils import nodes
    from docutils.parsers.rst import directives
    from sphinx.util.docutils import SphinxDirective

    class PreviewDirective(SphinxDirective):
        required_arguments = 1
        optional_arguments = 0
        has_content = False
        option_spec = {"alt": directives.unchanged_required, "caption": directives.unchanged}

        def run(self) -> list[nodes.Node]:
            config = self.env.config
            try:
                markup = preview_html(self.arguments[0], self.options.get("alt", ""), config.gf_media,
                                      config.gf_previews, self.options.get("caption", ""))
            except ValueError as error:
                raise self.error(str(error)) from error
            if not markup:
                return []
            return [nodes.raw("", markup, format="html")]

    app.add_config_value("gf_media", "", "env")
    app.add_config_value("gf_previews", {}, "env")
    app.add_directive("gf-preview", PreviewDirective)
    return {"parallel_read_safe": True, "parallel_write_safe": True}
