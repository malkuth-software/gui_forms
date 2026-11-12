Many small images of one size kept in the slots of one backing image.

An `ImageAtlas` holds generated images that share a size, such as avatars,
thumbnails or icons made at run time, in fixed-size slots of one image in the
application's image registry. A program asks for a key's slot with `acquire`,
writes its pixels with `fill`, and draws it with `draw` during a control's
paint. When a slot changes, only the controls
that drew it repaint. Use an `ImageList` for images loaded from files under
names, and a single registry image for one large picture.

Parameters
----------
resources
    The `ApplicationResources` whose registry holds the backing image; the
    image is valid in every window sharing them.
layout
    Slot size, slot count, gutter, users remembered per slot and the
    replacement policy (`ImageAtlasLayout`).

Properties
----------
image
    The backing image. Its `ImageId` stays the same for the atlas's whole
    life, because a fill writes the image in place.
layout
    The layout the atlas was made with.
statistics
    Counts of hits, assignments, replacements, fills, draws, refusals and
    damaged users, the slots in use and the bytes of the image and tables.

Methods
-------
acquire
    Returns the key's slot: found (`hit`), a free one (`assigned`), the least
    recently used one (`replaced`, under `least_recently_used`) or none
    (`full`, under `caller`). A replaced slot's previous key loses it, its
    handles go stale and the controls that drew it repaint whole.
find
    The key's current slot handle, or an empty handle. It does not count as a
    use.
fill
    Writes premultiplied BGRA pixels into a slot and repeats its edge pixels
    into the gutter. The controls that drew the slot repaint where they drew
    it. During a paint it returns `ImageAtlasError::deferred` and the pixels
    are written when the paint ends.
release
    Frees a slot for another key. Its handles go stale and the controls that
    drew it repaint whole.
draw
    Draws a filled slot through the painter and remembers the control and
    where it drew, so that later changes repaint it. Returns false and draws
    nothing for a stale or unfilled slot, or for a control past the layout's
    `users_per_slot`; the control then draws its own fallback.
draw_unrecorded
    Draws a slot without remembering a control, for a painter that is not a
    control's paint; a change to the slot repaints nothing drawn this way.

See Also
--------
ImageList : Named images loaded from files, with state and density variants.
ImageRegistry : The registry that holds the backing image.
AllocationScope : Reads what GUI.Forms' counted paths allocated around a workload.

Notes
-----
Slot handles carry a generation. Releasing or replacing a slot advances it,
so an older handle is rejected by every call (`ImageAtlasError::stale_slot`,
or false).

Replacement: under `least_recently_used` the atlas keeps its own record of
use (acquire hits and draws) and replaces the least recently used key when
every slot is taken. Under `caller` it never replaces; `acquire` answers
`full` and the program releases a slot by its own policy.

Deferred fills: a fill made while a window sharing the resources is painting
(a row measured inside the paint's settle) is copied into a pending cell and
written when that paint ends, or earlier at the atlas's next `acquire`,
`fill`, `release` or `clear` outside a paint. Pending fills are written in
the order their slots were first deferred. A slot has at most one: a later
fill replaces the pending pixels and keeps its place. Releasing or replacing
the slot drops it. The layout's `pending_fills` sets how many cells exist
(one per slot by default); with none free, the fill is refused with
`ImageAtlasError::painting`.

Damage: a fill asks each control that drew the slot since it last changed to
repaint the union of the rectangles where it drew it, then forgets them until
they draw it again. A release or replacement asks them to repaint whole, so
they look their key up again. No other control repaints.

Memory: every table is allocated when the atlas is made; acquiring, filling,
drawing and releasing allocate nothing of the atlas's own. The tables are
counted under `AllocationSubsystem::image_atlas` and the backing image under
`AllocationSubsystem::image_resources`.

Threads and lifetime: use the atlas on the resources' UI thread. It removes
its image when destroyed. If the resources go first, mutations throw
`std::logic_error` and draws draw nothing. It never keeps a control alive.
