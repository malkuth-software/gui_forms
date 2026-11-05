A container that divides one axis into tracks which shrink, grow and collapse in an order you set.

A responsive track panel lays its children out in a single row or column of
tracks. Each track is fixed, sized to its content, or takes a weighted share
of the space left over. When the panel becomes too small for every track's
minimum, tracks that have a collapse priority give way one at a time, lowest
priority first, until the rest fit. Use it for application shells, toolbars
and status bars that must degrade in a predictable way on small windows, and
nest a vertical panel inside a horizontal one to build a grid. Use a
`GridPanel` instead when nothing should ever collapse, and a
`CommandOverflowPanel` for a command row whose groups move into a menu.

Parameters
----------
stable_id
    Identity of the panel, unique within its window.

Properties
----------
orientation : default ``ResponsiveTrackOrientation::horizontal``
    The axis the tracks run along. Horizontal tracks are columns that fill
    the panel's height; vertical tracks are rows that fill its width.
track_gap : default ``0.0``
    Space between neighbouring tracks that are not collapsed. It must be
    finite and between 0 and 256.
track_specs : default ``{ResponsiveTrackSpec{}}``
    The tracks, in order along the axis, from 1 through 64 of them. Each
    `ResponsiveTrackSpec` gives a size mode with a `minimum`, `preferred` and
    `maximum` extent (a `maximum` of 0 means unbounded), a `remaining_weight`
    for `remaining` tracks, a `hidden` flag, and an optional unique
    `collapse_priority`. Set them with `set_track_specs`, which validates the
    whole list before changing anything and forgets child assignments,
    reveals and focus fallbacks that refer to tracks beyond the new count.
    Extents must be finite, at most 1,000,000, and ordered so that `minimum`
    is not above `preferred` and a nonzero `maximum` is not below it; a
    `remaining` track's weight must be above 0 and at most 1024; priorities
    must be unique. Otherwise `std::invalid_argument` is thrown.
layout_snapshot
    The result of the last layout: the orientation, each track's minimum,
    desired and allocated extent, its start and end in logical and device
    pixels, whether and why it collapsed, the collapse order, whether the
    tracks overflow the panel, and a revision number that increases only
    when the result changes. Reading it brings the layout up to date first.

Methods
-------
set_track_spec
    Replaces one track's spec. The track index must be below the track
    count, or `std::out_of_range` is thrown; the resulting list is validated
    as a whole, so a duplicate priority throws `std::invalid_argument`.
set_child_track
    Puts `child` in a track. Several children may share a track; they are
    laid over one another in the same cell. The child must be a live direct
    child (`std::invalid_argument`) and the track must exist
    (`std::out_of_range`).
child_track
    The track `child` was put in with `set_child_track`, or no value when it
    is placed automatically or is not a direct child.
set_track_revealed
    `set_track_revealed(track, true)` protects a track from automatic
    collapse until you clear it, for example while the user works in it. A
    protected track that cannot fit makes the tracks overflow instead. The
    track must exist, or `std::out_of_range` is thrown.
track_revealed
    Whether a track is protected by `set_track_revealed`.
set_track_focus_fallback
    Names the control that receives focus when the track holding the focused
    control must collapse. The track must exist (`std::out_of_range`) and the
    target must be a live descendant of the panel (`std::invalid_argument`).
clear_track_focus_fallback
    Removes a track's focus fallback.

See Also
--------
GridPanel : A grid of tracks that never collapse.
CommandOverflowPanel : A command row whose groups collapse into a menu.
FlexPanel : Items one after another with wrapping.

Notes
-----
Track sizes are resolved in this order:

- A `fixed` track wants its `preferred` extent and can shrink to its
  `minimum`. A `content` track wants the larger of `preferred` and the
  largest extent, with margins, of a child in it along the axis, and cannot
  shrink below that content, so text that grows with the text scale widens
  its track before anything collapses. A `remaining` track wants its
  `preferred` extent and can shrink to its `minimum`. Every extent is
  limited by the track's `maximum`.
- A `hidden` track takes no space and never shows its children.
- If the minimums and gaps do not fit, tracks with a `collapse_priority`
  collapse in increasing priority order, each only while the rest still do
  not fit. Tracks without a priority never collapse automatically.
- If the space is still short, each track shrinks from its desired extent
  toward its minimum in proportion to how much it can give; below the sum of
  the minimums every track is scaled down and the result reports overflow.
- Space left over goes to the `remaining` tracks by `remaining_weight`, each
  up to its `maximum`. Fixed and content tracks never grow past their
  desired extent.

Track boundaries are snapped to device pixels at the window's scale, so
neighbouring tracks never overlap or leave a crack.

Visible children are assigned to tracks in child order: a child put in a
track with `set_child_track` uses it, and any other child takes the next
track that no child was explicitly put in. Inside its track, after its
margins, a child keeps its preferred size, but never more than the track, and
Dock and Anchor place it as in a `TableLayoutPanelCompat` cell. A visible child left
over when there are no free tracks gets no new slot from the panel: it keeps
the slot it last had or, if it never had one, its requested bounds.

Collapse never changes a child's `visible` property. A collapsed track's
children are marked `layout_collapsed` and leave painting, hit testing,
focus traversal and the accessibility tree until the track has room again.
The track that holds the focused control is protected from collapse. If
keeping it would make the tracks overflow and it has a focus fallback, focus
moves to the fallback and the track collapses; without a fallback the track
stays and the result reports overflow.

With `auto_size` on, the panel measures to its tracks' desired extent along
its axis and takes the full available extent across it. Otherwise it takes
its requested size, or all available space when a requested dimension is
zero.

The panel is not focusable. It publishes the accessibility role `group` only
when it has an accessible name or description.

Examples
--------
A content area beside an inspector that collapses first when the window is
narrow:

.. code-block:: cpp

   std::shared_ptr<gf::ResponsiveTrackPanel> shell =
       gf::make_control<gf::ResponsiveTrackPanel>(gf::StableId("shell"));

   gf::ResponsiveTrackSpec content_track;
   content_track.size_mode = gf::ResponsiveTrackSizeMode::remaining;
   content_track.minimum = 80.0;
   content_track.preferred = 120.0;
   gf::ResponsiveTrackSpec inspector_track;
   inspector_track.size_mode = gf::ResponsiveTrackSizeMode::fixed;
   inspector_track.minimum = 120.0;
   inspector_track.preferred = 120.0;
   inspector_track.maximum = 120.0;
   inspector_track.collapse_priority = std::uint16_t{10};
   std::vector<gf::ResponsiveTrackSpec> tracks;
   tracks.push_back(content_track);
   tracks.push_back(inspector_track);
   (*shell).set_track_specs(tracks);

   std::shared_ptr<gf::Panel> content =
       gf::make_control<gf::Panel>(gf::StableId("shell.content"));
   std::shared_ptr<gf::Panel> inspector =
       gf::make_control<gf::Panel>(gf::StableId("shell.inspector"));
   (*content).dock = gf::DockStyle::fill;
   (*inspector).dock = gf::DockStyle::fill;
   (*shell).add_child(content);
   (*shell).add_child(inspector);
   (*shell).set_child_track(*content, 0U);
   (*shell).set_child_track(*inspector, 1U);

   gf::Window window(shell, {240.0, 100.0});
   window.perform_layout();      // both tracks fit side by side
   window.resize({100.0, 100.0});
   window.perform_layout();      // the inspector track collapses
   gf::ResponsiveLayoutSnapshot snapshot = (*shell).layout_snapshot();
   // snapshot.resolution.collapse_order holds 1, (*inspector).visible() is
   // still true and (*inspector).layout_collapsed() is true.
