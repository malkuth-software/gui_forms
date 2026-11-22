An in-window modal layer: any control shown as a modal panel inside its
owner's window, without a native window.

The layer fills the client area over everything else, dims it with a scrim
and centres the content at its measured size within a 16-unit margin. It
takes every press, release and wheel turn outside the content, so the rest
of the window cannot be used until it closes, and it holds keyboard focus
inside the content (Tab cycles within it). Layers opened over one another
stack: the latest is on top and holds focus, and closing it returns to the
one below. `TaskDialog`'s overlay surface works this way for its own
content; `Form::show_dialog` remains the native modal path.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
close_on_escape : default ``true``
    Whether Escape inside the content asks to close with
    ``DialogResult::cancel``.
is_shown
    Whether the layer is shown.
scrim_opacity
    The scrim's opacity now, as a factor on the theme's: it fades in from 0
    over `modal_scrim_fade` (150 ms) as the layer opens, and is 1 at once
    under reduced motion or in an occluded window. The content shows at
    once, and closing removes the layer at once.
content
    The content while shown, else null.
result
    The result of the last closing, or none before the first.

Events
------
closing
    Raised when `close` asks to close, with the result; a handler refuses
    the closing by setting the event's ``cancel``.
closed
    Raised after the content has left the layer, with the result.
outside_pressed
    Raised for a primary or secondary press beside the content while shown.
    The layer takes the press either way; an owner that wants light dismiss
    closes on it.

Methods
-------
show
    Shows a content control over an owner's window; false, changing
    nothing, when the owner is not attached to a window, the content is
    null or has a parent, or the layer is already shown or has a parent.
    Call it on the UI thread.
close
    Asks to close with a `DialogResult`: `closing` is raised first and may
    refuse; the content then leaves the layer (it is not disposed, and may
    be shown again) and `closed` is raised. False when the layer is not
    shown or a handler refused.

See Also
--------
TaskDialog : A standard dialog, natively or over the window.
Callout : A non-modal card pointing at a control.

Notes
-----
Focus goes to the content's first Tab stop when the layer opens and returns
to where it was when it closes. When the owner leaves its window the layer
closes as ``cancel`` without raising `closing`.

To assistive technology the layer is a modal dialog named by its accessible
name. A theme draws the ``scrim`` over the whole client (window family; the
theme sets its colour and opacity with ``scrim-color``, and high contrast
covers it opaque), and the content casts the overlay ``shadow`` (tool-tip
family) over the scrim.
