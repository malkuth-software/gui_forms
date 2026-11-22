"""Sphinx configuration for the GUI.Forms documentation site.

The site is Markdown (MyST) and reStructuredText rendered with the PyData Sphinx
Theme, in the layout of the NumPy documentation, and is built with
``sphinx-build -W`` locally, in CI and on Read the Docs, so every warning is
an error. See ``docs/README.md`` for maintainer notes.
"""

from __future__ import annotations

import html
import importlib.util
import json
import os
import re
import subprocess
import sys
from collections.abc import Mapping
from pathlib import Path

DOCS: Path = Path(__file__).resolve().parent
REPOSITORY: Path = DOCS.parent
GITHUB_REPOSITORY: str = "malkuth-software/gui_forms"
# The public repository the preview images are kept in, one folder per
# GUI.Forms commit they were captured at (tools/capture_previews.py).
PREVIEWS_REPOSITORY: str = "malkuth-software/gui_forms_previews"
# The captured set the pages show: the capture's commit and each image's
# size, written by tools/capture_previews.py --pin. Absent until the first
# capture is stored, and then every preview is left out of the pages.
PREVIEWS_PIN: Path = DOCS / "previews.json"


def read_project_version() -> str:
    """Return the version declared by the root CMakeLists.txt, or a dev label.

    The version is read at build time so that it is never duplicated here.
    """
    cmake_lists: Path = REPOSITORY / "CMakeLists.txt"
    if not cmake_lists.is_file():
        return "0.0.0-dev"
    text: str = cmake_lists.read_text(encoding="utf-8")
    match: re.Match[str] | None = re.search(
        r"^\s*project\s*\(\s*\w+\s+VERSION\s+(\d+(?:\.\d+){0,3})",
        text,
        re.MULTILINE,
    )
    if match is None:
        return "0.0.0-dev"
    return match.group(1)


# --- Every number in one place (owner decision, 2026-11-21) ----------------
#
# No page types a version or ABI number. Each is read here, at build time,
# from its source of truth, and pages use it by name: {{ gf_version }} in
# Markdown (MyST substitutions) and |gf_version| in reStructuredText
# (rst_prolog). tools/readme_numbers.py writes the same values into the
# GitHub README, which cannot run code, and checks them in CI;
# tools/make_roadmap_svg.py draws with them. A number that cannot be read
# stops the build: no page shows a guessed one.


def read_source(repository: Path, relative: str) -> str:
    """The text of a source-of-truth file; ValueError when it is missing."""
    path: Path = repository / relative
    if not path.is_file():
        raise ValueError(f"the documentation's numbers: {relative} is missing")
    return path.read_text(encoding="utf-8")


def read_match(repository: Path, relative: str, pattern: str, what: str, flags: int = 0) -> re.Match[str]:
    """The first match of `pattern` in a source-of-truth file; ValueError naming
    `what` and the file when there is none."""
    match: re.Match[str] | None = re.search(pattern, read_source(repository, relative), re.MULTILINE | flags)
    if match is None:
        raise ValueError(f"the documentation's numbers: no {what} in {relative}")
    return match


def read_package_version(repository: Path) -> str:
    """The package version as major.minor, from CMake's project(GUIForms VERSION ...)."""
    match = read_match(repository, "CMakeLists.txt",
                       r"^\s*project\s*\(\s*GUIForms\s+VERSION\s+(\d+)\.(\d+)", "project version")
    return f"{match.group(1)}.{match.group(2)}"


def read_c_abi(repository: Path) -> str:
    """GF_ABI_MAJOR.GF_ABI_MINOR from include/gui_forms/c/gf_abi.h."""
    relative = "include/gui_forms/c/gf_abi.h"
    major = read_match(repository, relative, r"^#define\s+GF_ABI_MAJOR\s+(\d+)u?\b", "GF_ABI_MAJOR")
    minor = read_match(repository, relative, r"^#define\s+GF_ABI_MINOR\s+(\d+)u?\b", "GF_ABI_MINOR")
    return f"{major.group(1)}.{minor.group(1)}"


def read_host_protocol(repository: Path) -> str:
    """The portable host protocol version: the last row of
    host_protocol_revisions, which HostCapabilities::current_protocol_version is."""
    relative = "include/gui_forms/host/types/host_capabilities/host_capabilities.hpp"
    table = read_match(repository, relative,
                       r"host_protocol_revisions\s*\{(.*?)\}\s*\};", "host_protocol_revisions table", re.S)
    versions: list[int] = [int(found) for found in re.findall(r"\{\s*(\d+)U\s*,", table.group(1))]
    if not versions:
        raise ValueError(f"the documentation's numbers: no protocol rows in {relative}")
    return str(versions[-1])


def read_drawing_abi(repository: Path) -> str:
    """The drawing (gd_*) ABI: the highest GD_ABI_VERSION_<major>_<minor> in
    include/gui_forms/drawing_c_api.h whose comment does not call it
    provisional, so a provisional version shows only once it is fixed."""
    relative = "include/gui_forms/drawing_c_api.h"
    text = read_source(repository, relative)
    best: tuple[int, int] | None = None
    for match in re.finditer(r"^#define\s+GD_ABI_VERSION_(\d+)_(\d+)\b", text, re.MULTILINE):
        before = text[:match.start()].rstrip()
        comment = before[before.rfind("/*"):] if before.endswith("*/") else ""
        if re.search(r"\bprovisional\b", comment, re.IGNORECASE):
            continue
        version = (int(match.group(1)), int(match.group(2)))
        if best is None or version > best:
            best = version
    if best is None:
        raise ValueError(f"the documentation's numbers: no GD_ABI_VERSION in {relative}")
    return f"{best[0]}.{best[1]}"


def read_previews_pin(docs: Path) -> dict:
    """docs/previews.json: {"capture": <commit>, "images": {name: {"width",
    "height", "bytes"}}}, or an empty pin when no capture is stored yet."""
    path: Path = docs / PREVIEWS_PIN.name
    if not path.is_file():
        return {"capture": "", "images": {}}
    pin = json.loads(path.read_text(encoding="utf-8"))
    capture = str(pin.get("capture", ""))
    if capture and re.fullmatch(r"[0-9a-f]{7,40}", capture) is None:
        raise ValueError(f"docs/previews.json: capture {capture!r} is not a commit")
    return {"capture": capture, "images": dict(pin.get("images", {}))}


def read_media_base(docs: Path, environ: Mapping[str, str]) -> str:
    """The base address of the preview images: the previews repository's
    folder for the pinned capture (GF_MEDIA in the environment replaces it,
    for a local build over a folder of captures)."""
    override = environ.get("GF_MEDIA", "").strip()
    if override:
        return override.rstrip("/")
    base = f"https://raw.githubusercontent.com/{PREVIEWS_REPOSITORY}/main"
    capture = read_previews_pin(docs)["capture"]
    return f"{base}/{capture}" if capture else base


def read_numbers(repository: Path, environ: Mapping[str, str]) -> dict[str, str]:
    """The table: every number and address the pages use, by its substitution name."""
    return {
        "gf_version": read_package_version(repository),
        "gf_c_abi": read_c_abi(repository),
        "gf_host_protocol": read_host_protocol(repository),
        "gf_drawing_abi": read_drawing_abi(repository),
        "gf_media": read_media_base(repository / "docs", environ),
    }


def read_minimum_platforms(repository: Path) -> dict[str, str]:
    """The oldest macOS and Windows each build targets: CMAKE_OSX_DEPLOYMENT_TARGET
    and _WIN32_WINNT (0x0A00 is Windows 10). For the README's platform table."""
    macos = read_match(repository, "CMakeLists.txt",
                       r"CMAKE_OSX_DEPLOYMENT_TARGET\s+(\d+)(?:\.0)?\b", "CMAKE_OSX_DEPLOYMENT_TARGET")
    windows = read_match(repository, "CMakeLists.txt", r"_WIN32_WINNT=0x([0-9A-Fa-f]{2})00\b", "_WIN32_WINNT")
    return {"macos": macos.group(1), "windows": str(int(windows.group(1), 16))}


# The platforms the README's table and badge name, in order: (platform, host,
# notes). The minimum versions come from read_minimum_platforms.
PLATFORMS: tuple[tuple[str, str, str], ...] = (
    ("macOS {macos}+", "AppKit", "Apple silicon (arm64)"),
    ("Windows {windows}+", "Win32", "x64; built with LLVM 22 under MSYS2 CLANG64"),
    ("Linux", "X11", "x64 and arm64; also runs under XWayland. Enable with `-DGUI_FORMS_ENABLE_LINUX_HOST=ON`"),
)


GF_NUMBERS: dict[str, str] = read_numbers(REPOSITORY, os.environ)
myst_substitutions: dict[str, str] = dict(GF_NUMBERS)
rst_prolog: str = "".join(f".. |{name}| replace:: {value}\n" for name, value in GF_NUMBERS.items())


# --- Source links at the commit the site is built from (D3) ----------------


def resolve_commit(environ: Mapping[str, str], repository: Path) -> str:
    """The commit links point at: Read the Docs' or GitHub Actions' commit,
    else the checkout's HEAD, else main. Resolved here, not in the generated
    pages, so that generate_library_docs.py --check stays stable."""
    for key in ("READTHEDOCS_GIT_COMMIT_HASH", "GITHUB_SHA"):
        value = environ.get(key, "").strip().lower()
        if re.fullmatch(r"[0-9a-f]{7,40}", value):
            return value
    try:
        completed = subprocess.run(["git", "rev-parse", "HEAD"], cwd=repository, capture_output=True,
                                   text=True, timeout=10, check=False)
    except (OSError, subprocess.SubprocessError):
        return "main"
    head = completed.stdout.strip().lower()
    return head if completed.returncode == 0 and re.fullmatch(r"[0-9a-f]{40}", head) else "main"


GF_COMMIT: str = resolve_commit(os.environ, REPOSITORY)
GF_BLOB: str = f"https://github.com/{GITHUB_REPOSITORY}/blob/{GF_COMMIT}"
# {src}`include/gui_forms/controls/button.hpp` (MyST) or :src:`...` (reST):
# a repository file at the commit the site is built from.
extlinks = {"src": (f"{GF_BLOB}/%s", "%s")}

_source_locations: dict[str, tuple[str, int]] | None = None


def source_locations() -> dict[str, tuple[str, int]]:
    """Every public declaration's header and line (tools/api_docs_parse.py),
    read once per build; empty when the tools are not beside the docs."""
    global _source_locations
    if _source_locations is None:
        _source_locations = {}
        # Loaded by path: tools/ on sys.path would let its modules shadow others.
        parser_path: Path = REPOSITORY / "tools" / "api_docs_parse.py"
        spec = importlib.util.spec_from_file_location("gf_api_docs_parse", parser_path)
        if parser_path.is_file() and spec is not None and spec.loader is not None:
            parser = importlib.util.module_from_spec(spec)
            sys.modules[spec.name] = parser
            spec.loader.exec_module(parser)
            _source_locations = parser.source_locations(*parser.declarations())
    return _source_locations


def source_name(name: str) -> str:
    """A C++ domain name as source_locations keys it: no leading ::, no
    template arguments."""
    plain = name.strip().lstrip(":")
    while True:
        shorter = re.sub(r"<[^<>]*>", "", plain)
        if shorter == plain:
            return plain
        plain = shorter


def linkcode_resolve(domain: str, info: Mapping[str, object],
                     locations: Mapping[str, tuple[str, int]] | None = None) -> str | None:
    """The [source] link beside a C++ declaration: its header and line at GF_COMMIT."""
    if domain not in ("cpp", "c"):
        return None
    names = info.get("names", [])
    if isinstance(names, str):
        names = [names]
    table = source_locations() if locations is None else locations
    for name in names if isinstance(names, (list, tuple)) else []:
        found = table.get(source_name(str(name)))
        if found is not None:
            return f"{GF_BLOB}/{found[0]}#L{found[1]}"
    return None


project = "GUI.Forms"
author = "GUI.Forms contributors"
copyright = "GUI.Forms contributors"
release = read_project_version()
version = release

# The site follows the layout and appearance of the NumPy documentation:
# the PyData Sphinx Theme, sphinx-design overview cards, numpydoc-style
# reference pages and copy buttons on code blocks.
extensions = [
    "myst_parser",
    "sphinx_design",
    "sphinx_copybutton",
    "sphinx.ext.extlinks",
    "sphinx.ext.linkcode",
    # docs/_ext: the preview directive (gf-preview).
    "gf_preview",
]
sys.path.insert(0, str(DOCS / "_ext"))
# The preview directive's settings (docs/_ext/gf_preview.py).
gf_media: str = GF_NUMBERS["gf_media"]
gf_previews: dict = read_previews_pin(DOCS)["images"]
templates_path = ["_templates"]

source_suffix = {
    ".rst": "restructuredtext",
    ".md": "markdown",
}

primary_domain = "cpp"
# Like NumPy's pages, "On this page" lists sections, not every declaration.
toc_object_entries = False
highlight_language = "cpp"

master_doc = "index"

myst_heading_anchors = 3
myst_enable_extensions = [
    "colon_fence",
    "deflist",
    "substitution",
]

# Everything under docs/ that is not part of the published site. Each entry is
# deliberate: the build runs with -W, so an unlisted stray page is an error.
exclude_patterns = [
    # Sphinx output and host metadata files.
    "_build",
    "Thumbs.db",
    ".DS_Store",
    # Maintainer notes for this directory; not a user page.
    "README.md",
    # The GitHub wiki's pages, kept here and published to the wiki by hand;
    # they link to this site rather than being part of it.
    "wiki/**",
    # Maintainer notes for the generated control indexes; not a user page.
    "controls/generated/README.md",
    # Hand-written narratives and capture evidence read by
    # tools/generate_library_docs.py; not pages.
    "library/**",
    # Inputs of tools/api_reference.py (the clang declaration inventory and the
    # reviewed contracts) and any HTML it renders. Not Sphinx sources; the
    # site's API reference is docs/api/, generated by
    # tools/generate_library_docs.py.
    "reference/**",
]

html_theme = "pydata_sphinx_theme"
html_title = f"{project} v{version} Manual"
html_favicon = "_static/gui_forms_mark.svg"
html_static_path = ["_static"]
html_css_files = ["gui_forms.css"]
html_last_updated_fmt = "%b %d, %Y"
html_context = {
    "default_mode": "light",
    # docs/_templates/page-source.html: each page's source at GF_COMMIT.
    "gf_commit": GF_COMMIT,
    "gf_blob": GF_BLOB,
}
html_copy_source = False
html_domain_indices = False
html_theme_options = {
    "logo": {
        "text": "GUI.Forms",
        "image_light": "_static/gui_forms_mark.svg",
        "image_dark": "_static/gui_forms_mark_dark.svg",
    },
    "github_url": "https://github.com/malkuth-software/gui_forms",
    "collapse_navigation": True,
    "header_links_before_dropdown": 6,
    "navbar_end": [
        "search-button",
        "theme-switcher",
        "navbar-icon-links",
    ],
    "navbar_persistent": [],
    "secondary_sidebar_items": {"**": ["page-toc", "page-source"], "index": []},
    "footer_start": ["copyright"],
    "footer_end": ["sphinx-version", "theme-version"],
}
# As on NumPy's site: section navigation only (no collapse button), and no
# primary sidebar on the landing page.
html_sidebars = {
    "**": ["sidebar-nav-bs"],
    "index": [],
}
copybutton_prompt_text = r"\$ "
copybutton_prompt_is_regexp = True

# Pages that moved when the site took the NumPy layout (2026-10-11). Each old
# address keeps a small page that forwards to the new one, so bookmarks and
# links into the previous layout still arrive. Keys and values are docnames.
USER_GUIDE_PAGES: tuple[str, ...] = (
    "retained-tree",
    "update-scopes-and-dirtiness",
    "layout",
    "painting-and-damage",
    "text-engine",
    "input-and-focus",
    "threading",
    "lifecycle",
    "hosts-and-capabilities",
)
LIBRARY_AREAS: tuple[str, ...] = (
    "animation-and-threading",
    "application-and-hosts",
    "audio",
    "c-abi",
    "control-and-component",
    "data-binding",
    "drawing",
    "events-values-and-commands",
    "geometry-and-appearance",
    "images-and-resources",
    "input-and-semantics",
    "text",
    "window",
)
CONTROL_CATEGORIES: tuple[str, ...] = (
    "commands",
    "components",
    "date-setting",
    "diagnostics",
    "dialogs-and-overlays",
    "drawing",
    "grouping",
    "information-display",
    "layout",
    "menus-and-commands",
    "scrolling",
    "selection-from-a-list",
    "text-editing",
    "value-setting",
)
DECISION_RECORDS: tuple[str, ...] = ("001", "002", "003", "004", "006", "013", "014")
MOVED_PAGES: dict[str, str] = {
    "start/install": "user/install",
    "start/consume-the-sdk": "user/consume-the-sdk",
    "start/first-window": "user/first-window",
    "start/download": "user/download",
    "start/build-from-source": "building/index",
    "controls/index": "user/controls",
    "project/decisions": "dev/decisions",
    "project/roadmap": "dev/roadmap",
    "project/contributing": "dev/contributing",
    "project/evidence": "dev/evidence",
    **{f"concepts/{page}": f"user/{page}" for page in USER_GUIDE_PAGES},
    **{f"api/{area}": f"api/library.{area}" for area in LIBRARY_AREAS},
    **{f"api/{category}-controls": f"api/controls.{category}" for category in CONTROL_CATEGORIES},
    # The records moved to the notes repository; dev/decisions links to each.
    **{f"project/adr/ADR-{number}": "dev/decisions" for number in DECISION_RECORDS},
    **{f"dev/adr/ADR-{number}": "dev/decisions" for number in DECISION_RECORDS},
}

REDIRECT_PAGE: str = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="robots" content="noindex">
<meta http-equiv="refresh" content="0; url={target}">
<title>Page moved</title>
<script>location.replace("{target}" + location.hash);</script>
</head>
<body><p>This page has moved to <a href="{target}">{target}</a>.</p></body>
</html>
"""


def write_moved_pages(app, exception) -> None:
    """Write a forwarding page at each old address after a successful HTML build."""
    if exception is not None or app.builder.name != "html":
        return
    output: Path = Path(app.outdir)
    for old, new in MOVED_PAGES.items():
        if old in app.env.all_docs:
            raise ValueError(f"moved page {old} is still a page of the site")
        if new not in app.env.all_docs:
            raise ValueError(f"moved page {old} forwards to {new}, which is not a page")
        source: Path = output / f"{old}.html"
        target: str = Path(os.path.relpath(output / f"{new}.html", source.parent)).as_posix()
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_text(REDIRECT_PAGE.format(target=html.escape(target)), encoding="utf-8")


def setup(app) -> None:
    app.connect("build-finished", write_moved_pages)
