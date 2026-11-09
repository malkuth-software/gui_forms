A themed surface with a header, a body and a footer.

A card arranges up to three section controls that you supply: a header strip
at the top, a footer strip at the bottom and a body that fills the space
between them. It paints the theme's card surface behind them. A card can
also be made interactive, so the user can focus, click and select it like an
item in a gallery of choices. Use a `Panel` or `MaterialPanel` for a surface
without sections, and a `ListBox` or `ChoiceGroup` when the choices are
simple text.

Parameters
----------
stable_id
    Identity of the card, unique within its window. Logs, automation and
    accessibility actions address the card by this ID.

Properties
----------
header : default ``nullptr``
    The control in the header strip. `set_header` makes the new control a
    child of the card and returns the control it replaces, now detached, or
    null. Pass null to remove the header; passing the current header does
    nothing and returns null. A control that already has a parent, or that
    fills another section of this card, throws `std::logic_error` and the
    card is left unchanged.
body : default ``nullptr``
    The control between the header and the footer; `set_body` follows the
    same rules as `set_header`.
footer : default ``nullptr``
    The control in the footer strip; `set_footer` follows the same rules as
    `set_header`.
card_layout : default ``CardLayout{}``
    Your own padding, section gap and header and footer heights. Setting it
    makes the card use it instead of the theme's values; a value that is not
    finite or lies outside 0 to 4096 throws `std::invalid_argument`. Until
    you set it, the card follows the theme and this value is not used.
uses_theme_layout : default ``true``
    Whether the card takes its layout from the theme. It becomes false when
    you call `set_card_layout`, and true again after
    `reset_card_layout_to_theme`.
effective_card_layout
    The layout in use: `card_layout`, or the theme's values, with the
    theme's large spacing as left and right padding, its medium spacing as
    top and bottom padding and as the section gap, and its control height
    for the header and footer.
interactive : default ``false``
    Whether the user can hover, focus, press and activate the card.
    Turning it on also makes the card focusable; turning it off clears its
    hover and pressed state. A card that is not interactive is never itself
    the target of a click; the click goes to the control behind it.
selected : default ``false``
    Whether the card is selected. It changes the card's appearance and, on
    an interactive card, its accessibility state. The application sets it,
    or activation does through `selection_behavior`.
selection_behavior : default ``CardSelectionBehavior::manual``
    What activation does to `selected` before `activated` is raised:
    nothing, select the card, or toggle it. An unknown value throws
    `std::invalid_argument`.
hovered_visual : default ``false``
    Whether the pointer is over an interactive card.
pressed_visual : default ``false``
    Whether an interactive card is being pressed with the pointer, Space or
    Enter.
focused_visual : default ``false``
    Whether the card has focus.

Events
------
activated
    Raised when the user activates an interactive card: a primary-button
    press and release over it, the release of Space or Enter while it has
    focus, or an accessibility press action. It carries the card. When
    `selection_behavior` changes the selection, `selected_changed` is raised
    first.
selected_changed
    Raised after `selected` changes, whether by `set_selected`, by
    activation or by an accessibility select action. It carries the new
    value. Setting the current value again raises nothing.

Methods
-------
reset_card_layout_to_theme
    Returns to the theme's layout after `set_card_layout`.
measure
    Reports the padding plus the height of each section (at least
    `header_extent` or `footer_extent` for those two) and the gaps between
    them, and the widest section plus the padding. A requested width or
    height replaces the measured one.
arrange
    Gives the header the top strip and the footer the bottom strip, each the
    full inner width and its layout height, and gives the body what remains
    between them.

See Also
--------
ReviewCard : A card that presents a review record.
Panel : A plain container for children placed by bounds, dock or anchor.
ChoiceGroup : A row of exclusive choices bound to a `Value`.

Notes
-----
Sections are ordinary children of the card, owned by it while installed. The
header and footer are always exactly `header_extent` and `footer_extent`
tall (less if the card is too small), the body takes the rest, and each
section spans the width inside the padding. A section's own requested
bounds, `dock` and `anchor` are not used, and a hidden section still keeps
its strip: remove it with null to give its space back. Children added with
`add_child` instead of a section setter are not arranged.

When the user clicks an interactive card, the press focuses it and the
release over it activates it. A child that takes pointer input, such as a
button in the footer or a `Panel` used as the body, takes the click instead,
so the card is activated only where no such child covers it; a `Label` lets
the click through to the card. With focus, Space or Enter
presses the card on key down and activates it on key up; a held key does
not repeat. The order of an activation is: `selected` changes as
`selection_behavior` says, `selected_changed` is raised, then `activated`.

An interactive card is published to accessibility as a `list_item` with
focus, press and select actions, the selected state, and a busy or invalid
state when its `visual_status` is pending or invalid. The press action
activates the card; the select action only sets `selected`. A card that is
not interactive is published as a `group` when it has an accessible name or
description, and is otherwise left out with its sections published in its
place.

With no background or style override the card paints the theme's card
surface for its hover, pressed, selected, focused and status state, and a
focus ring when focused; setting either override paints it as a `Panel`.
The card does not scroll; leave `auto_scroll` off.

Examples
--------
Fill the three sections of a card:

.. code-block:: cpp

   std::shared_ptr<gf::Card> card = gf::make_control<gf::Card>(gf::StableId("plan.pro"));
   (*card).set_requested_bounds({0.0, 0.0, 240.0, 160.0});
   std::shared_ptr<gf::Label> title =
       gf::make_control<gf::Label>(gf::StableId("plan.pro.title"), "Pro");
   std::shared_ptr<gf::Label> details =
       gf::make_control<gf::Label>(gf::StableId("plan.pro.details"), "Unlimited projects");
   std::shared_ptr<gf::Button> choose =
       gf::make_control<gf::Button>(gf::StableId("plan.pro.choose"), "Choose");
   static_cast<void>((*card).set_header(title));    // nothing was replaced
   static_cast<void>((*card).set_body(details));
   static_cast<void>((*card).set_footer(choose));

Make a card selectable. Each activation toggles the selection before
`activated` is raised:

.. code-block:: cpp

   struct Plans final : gf::Component {
       std::shared_ptr<gf::Card> pro = gf::make_control<gf::Card>(gf::StableId("plans.pro"));
       bool chosen = false;

       Plans() {
           (*pro).interactive = true;
           (*pro).selection_behavior = gf::CardSelectionBehavior::toggle_on_activation;
           gf::on((*pro).activated(), *this, &Plans::on_activated);
       }
       void on_activated(gf::Card& card) { chosen = card.selected.read(); }
   };

   void example() {
       Plans plans;
       (*plans.pro).set_requested_bounds({0.0, 0.0, 200.0, 120.0});
       gf::Window window(plans.pro, {200.0, 120.0});
       window.perform_semantic_action("plans.pro", gf::SemanticAction::press);
       // the card is selected, then activated: plans.chosen is true
   }
