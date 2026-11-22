A teaching callout: a card pointing out a target control, with an optional
bold title, wrapped text, a close mark and an optional action button.

It opens below its target (above with `prefers_above`), aligned to the
target's leading edge and moved to the other side when the preferred one has
no room, in an overlay of the target's window, and it follows the target
when the window resizes. A callout is not modal. Use a `ToolTip` for a short
hint on hover, and a `ModalLayer` or a `TaskDialog` when the user must
answer before going on.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
title : default empty
    The bold first line; empty for none. It is localizable. Invalid UTF-8
    throws `std::invalid_argument`.
text : default empty
    The explanation under the title, wrapped to the callout's width. It is
    localizable. Invalid UTF-8 throws `std::invalid_argument`.
action_text : default empty
    The action button's text; empty for no action button. It is
    localizable. Invalid UTF-8 throws `std::invalid_argument`.
light_dismiss : default ``false``
    Whether a press anywhere outside the callout closes it. Without it,
    presses outside reach the window and only the close mark, Escape or the
    action close it.
prefers_above : default ``false``
    Whether the callout opens above its target rather than below; it moves
    to the other side when the preferred one has no room.
is_shown
    Whether the callout is open.
target
    The target while shown, else null.
action_button
    The action `Button` while `action_text` is set, else null.

Events
------
closed
    Raised once per closing, after the callout has left the overlay, with
    the `CalloutCloseReason`: the close mark, Escape, a press outside, the
    action, `close`, or the target leaving its window. The callout may be
    shown again from the handler.
action_clicked
    Raised when the action button is clicked, before the callout closes.

Methods
-------
show
    Opens the callout at a target, which must be attached to a window;
    false, changing nothing, when it is not, or when the callout is already
    shown or has a parent. Call it on the UI thread.
close
    Closes a shown callout, with the reason ``programmatic``; false when it
    is not shown.

See Also
--------
ToolTip : A short hint on hover.
ModalLayer : Content the user must deal with first.

Notes
-----
Without `light_dismiss`, presses outside the callout reach the window as
usual. With it, a press outside closes the callout and is not passed on.
Focus moves into the callout when it opens (to the action button, else the
callout) without being held there, and returns to where it was when it
closes. Escape closes it while focus is inside it.

To assistive technology the callout is a non-modal dialog named by its
title, described by its text, with a close action. A theme draws the card as
the ``face`` part (tool-tip family) with the overlay ``shadow`` beneath and
the ``beak`` on the side towards the target, pointing at the target's
centre; ``glyph`` is the close mark. The callout's bounds hold the card and
the beak, and presses beside the beak pass through.
