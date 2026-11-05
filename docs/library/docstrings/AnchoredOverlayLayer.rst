A transparent full-window overlay that shows one overlay control next to an anchor control.

An anchored overlay layer holds one content control, such as a panel with a
few fields, and keeps it positioned against an anchor, usually the button
that opened it: below or above the anchor, aligned to its near edge, center
or far edge, and always inside the window. You open the layer as a window
overlay with `Window::open_overlay` and contain focus in it with
`Window::begin_focus_scope`. When the user clicks outside the content or
presses Escape, the layer raises `dismiss_requested`; it does not close
itself, so your application decides what dismissal means. Use a
`ContextMenu` instead for a menu of commands.

Parameters
----------
stable_id
    Identity of the layer, unique within its window.
anchor
    The control the content is placed against. It must be live, or the
    constructor throws `std::invalid_argument`.
placement
    The size and placement rules for the content, by default an
    `AnchoredOverlayPlacement{}`. Invalid geometry throws
    `std::invalid_argument`.

Properties
----------
anchor
    The control the content is placed against, or null once it has been
    destroyed. The new anchor must be live, or `std::invalid_argument` is
    thrown. Changing it moves an open overlay at the next layout.
content
    The control shown in the overlay, or null. `set_content` replaces the
    previous content, which is removed from the layer. The new content must
    be live, without a parent and not attached to a window, or
    `std::invalid_argument` is thrown.
placement : default ``AnchoredOverlayPlacement{}``
    The content's preferred size (0 by 0 by default, so set it), its
    horizontal alignment to the anchor
    (`OverlayHorizontalAlignment::near` by default), the side it prefers
    (`OverlayVerticalPreference::below` by default), the gap between anchor and
    content (0), the margin kept from the window's edges (4), and whether it
    may flip to the other side (true). Sizes, gap and margin must be finite
    and nonnegative, or `std::invalid_argument` is thrown.
resolved_placement
    Where the last layout put the content, in window client coordinates,
    whether it went above the anchor, and whether its width or height had
    to be reduced to fit. Empty while the anchor is hidden or detached.
dismiss_on_click_away : default ``true``
    Whether a primary-button press on the layer outside the content and the
    anchor raises `dismiss_requested`. Either way, such a press does not
    reach the controls underneath.
dismiss_on_escape : default ``true``
    Whether Escape raises `dismiss_requested` while focus is inside the
    layer. When false, Escape goes on to the focused control.

Events
------
dismiss_requested
    Raised when the user asks to close the overlay, with
    `OverlayDismissReason::click_away` or `OverlayDismissReason::escape_key`.
    The overlay stays open until the application closes it, typically by
    ending the focus scope and disconnecting its `OverlayToken`. Escape is
    reported before the focused control sees the key.

See Also
--------
ContextMenu : An overlay menu of commands with its own dismissal.
OverlayToken : Keeps a window overlay open and closes it.
ToolTip : An input-transparent help overlay.
AnchoredOverlayPlacement : The placement rules for the content.

Notes
-----
The layer always covers the whole client area of its window and is
transparent, so only the content is visible. The content is measured at the
preferred size and placed against the anchor's bounds in the window: it takes
the preferred side when it fits there, flips to the other side when it fits
only there and flipping is allowed, and is then moved and, if needed, reduced
to stay inside the window margin. The placement is resolved again each time
the layer is laid out, which includes every window resize and every change of
anchor or placement. The anchor's own area is left out of the layer's hit
testing, so a click on the anchor reaches the anchor, for example to toggle
the overlay.

The application owns the overlay's lifetime: `Window::open_overlay` with the
anchor and the layer returns the `OverlayToken` that keeps it open, and
disconnecting the token closes it. The window may also close it, for example
when the anchor is hidden or destroyed; the token's `closed_event` tells you
when. Ending the focus scope returns focus to where it was before the overlay
opened.

The layer publishes the accessibility role `group` only when it has an
accessible name.

Examples
--------
A colour picker that opens below its button and closes on Escape or a click
outside:

.. code-block:: cpp

   struct Picker final : gf::Component {
       std::shared_ptr<gf::Panel> root =
           gf::make_control<gf::Panel>(gf::StableId("picker"));
       std::shared_ptr<gf::Button> button =
           gf::make_control<gf::Button>(gf::StableId("picker.open"), "Colour");
       std::shared_ptr<gf::Panel> content =
           gf::make_control<gf::Panel>(gf::StableId("picker.content"));
       std::shared_ptr<gf::TextBox> value =
           gf::make_control<gf::TextBox>(gf::StableId("picker.value"), "#3366cc");
       std::shared_ptr<gf::AnchoredOverlayLayer> layer;
       gf::OverlayToken token;
       gf::FocusScopeId scope{};
       gf::OverlayDismissReason last_reason = gf::OverlayDismissReason::click_away;

       Picker() {
           (*button).set_requested_bounds({20.0, 20.0, 100.0, 28.0});
           (*root).add_child(button);
           (*value).set_requested_bounds({8.0, 8.0, 160.0, 24.0});
           (*content).add_child(value);
           gf::AnchoredOverlayPlacement placement;
           placement.preferred_size = {180.0, 120.0};
           placement.gap = 4.0;
           layer = gf::make_control<gf::AnchoredOverlayLayer>(
               gf::StableId("picker.layer"), button, placement);
           (*layer).set_content(content);
           gf::on((*layer).dismiss_requested(), *this, &Picker::on_dismiss);
       }

       void open(gf::Window& window) {
           token = window.open_overlay(button, layer);
           scope = window.begin_focus_scope(layer, value);
       }

       void on_dismiss(gf::OverlayDismissReason reason) {
           last_reason = reason;
           gf::Window* window = (*layer).attached_window();
           if (window != nullptr && scope) {
               static_cast<void>((*window).end_focus_scope(scope));
           }
           scope = {};
           token.disconnect();
       }
   };

   void example() {
       Picker picker;
       gf::Window window(picker.root, {320.0, 240.0});
       picker.open(window);
       window.perform_layout();
       // The content sits 4 pixels below the button, and the field has focus.
       gf::KeyEvent escape;
       escape.physical_key = gf::PhysicalKey::escape;
       static_cast<void>(window.dispatch_key(escape));
       // on_dismiss closed the overlay; picker.last_reason is escape_key.
   }
