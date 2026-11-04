# Transparency, glass and reflections

A GUI.Forms window can be see-through in three ways, which build on one
another:

1. the window asks the window system for a **backdrop**, and its clear
   pixels then show the desktop or a system material
   ([Hosts and capabilities](hosts-and-capabilities.md), "Compositor
   cooperation");
2. a part drawn as **glass** blurs and tints whatever the window drew
   under it, its *backplane*;
3. a part's material may carry a **reflection** of its own face or of the
   backplane under it.

GUI.Forms draws all of it on the CPU, into the window's own frame. It
never reads, copies or blurs the desktop: what lies outside the window
reaches the screen only through the compositor, behind the window's clear
pixels.

## The window's backplane over each backdrop

A window draws first its *backplane*: the window's background, and every
control drawn in the backplane plane (`Control::set_paint_plane(
PaintPlane::backplane)`). Glass and backplane reflections sample it.

Over `opaque`, the backplane starts from the window recipe's material, as
always. Over a granted non-opaque backdrop it starts from the theme's base
for that backdrop (`ThemeDefinition::window_backdrop.bases`), typically a
partly clear tint over a material and clear over `transparent`. A theme
with no base for a backdrop draws the window recipe's material there,
which hides the backdrop wherever that material is opaque. The theme may
also make the window's request (`window_backdrop.request`) until the
application makes one.

## Glass

A rule that selects `primitive: glass` for a part draws that part as glass:
it blurs the backplane under the part and tints it. Glass never samples
the desktop. Over a window granted `transparent`, glass over a clear
backplane is its tint alone, partly clear; over a material it is the
material's base, blurred and tinted, over the system's material.

## Reflections

`reflection` (theme language: `reflection: below 6px 30%`) mirrors the
part's finished face past one edge, at most 8 units out. With the final
keyword `backplane` it mirrors the backplane under the part instead, so a
part reflects what is drawn behind it, and is drawn again when that
changes. See [Theme language](theme-language.md) for the full syntax.

## Fallbacks

Every combination renders; nothing is an error. What each setting does:

| Setting or state | Backdrop granted | Glass | Reflections |
|---|---|---|---|
| backdrop granted | as requested | blurred backplane, tinted | drawn |
| backdrop refused (`os_version`, `host_unsupported`, `no_compositor`, `not_declared`) | the request's next entry, else `opaque` | over the opaque backplane | drawn |
| transparency effects off (`transparency_disabled`) | `opaque` | over the opaque backplane | drawn |
| reduced transparency | `opaque` | flattened to its opaque tint | dropped |
| high contrast | `opaque` | dropped | dropped |

Windows reports transparency effects turned off as reduced transparency
too, so there the fourth row applies.

A compositor that stops makes the window opaque at the next frame; the
request stands, and the backdrop returns when the compositor does. Each
change raises `Window::backdrop_changed` with the new grant and its
reason, and only the parts that sampled the backplane are drawn again.

## The compositor lab

`gui_forms_compositor_lab` (`gui_forms_compositor_lab_windows` on Windows,
`gui_forms_compositor_lab_linux` on X11) shows a window declared
non-opaque with scenery in its backplane, a glass region straddling the
scenery and the clear part of the window, a face reflecting itself and
one reflecting the backplane, and a status line naming the grant. Choose
the request with `--backdrop`, preferred first:

```text
gui_forms_compositor_lab_windows --backdrop window_material,transparent
```

Each grant is printed as one JSON line: the host's capabilities, the
compositor state, the request, the grant and its reason.
`tools/t5_gate_record.py` runs the lab for each request with `--capture`
and records the grants beside the presented frame's alpha, which is how
stage T5's gate is recorded on each host.
