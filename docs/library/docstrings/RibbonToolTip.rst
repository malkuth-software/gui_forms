A provider of enhanced tooltips: a title, a description, a shortcut, an image, why a command is unavailable, and an F1 help topic.

A ribbon tool tip belongs to one window. It holds content for a whole
control (`set_tool_tip`) or for one item of a `ToolStrip` or
`QuickAccessToolbar` (`set_item_tool_tip`). When the pointer rests on a
mapped control or item, the tooltip appears after `initial_delay`, below it;
when keyboard focus reaches a mapped control, or moves to a mapped item of a
focused strip, it appears the same way, keyboard-initiated. It disappears
after `auto_pop_delay`, or when the pointer leaves, presses or moves to
another item, or focus moves. The overlay is drawn over the window but never
intercepts the pointer. While a tooltip with a help topic shows, F1 raises
`help_requested` with the topic; GUI.Forms opens no help viewer.

Content may name a `Command`: an empty title, description, shortcut or
disabled reason is then taken from the command's text, description,
shortcut and availability reason when the tooltip opens, and the command's
enabled state says whether the target is disabled. A strip item with a
command supplies it when the content names none. The disabled reason shows
only while the target is disabled.

Content without a title takes the thing's plain tooltip as its title: the
control's `tool_tip_text`, or the strip item's `tool_tip_text`. While a
mapping exists, the window's plain tooltip presenter shows nothing for what
it describes (a control mapping covers every part of the control), so the
two never show at once; removing the mapping hands the thing back.

Parameters
----------
window
    The window whose controls the provider serves. The provider cannot be
    changed or shown once that window has shut down.

Properties
----------
initial_delay : default ``500 ms``
    How long the pointer must rest, or focus stay, before the tooltip
    appears. A negative delay throws `std::invalid_argument`.
auto_pop_delay : default ``10000 ms``
    How long a tooltip stays up; zero keeps it until the pointer leaves or
    focus moves. A negative delay throws `std::invalid_argument`.
help_line_text : default ``"Press F1 for more help."``
    The line a tooltip with a help topic shows under a rule. Invalid UTF-8
    throws `std::invalid_argument`.
visible : default ``false``
    Whether a tooltip is showing.
active_control
    The control whose tooltip is showing, or null.
active_item_id
    The strip item whose tooltip is showing, or empty for a whole control or
    no tooltip.
shown_content
    What the showing tooltip shows, resolved from its command; empty when
    none shows.
overlay_bounds
    The showing tooltip's bounds in window coordinates; empty when none
    shows.

Events
------
visibility_changed
    Raised when a tooltip appears and when it disappears, with the target's
    stable ID, the item ID, the resolved content, whether it is now shown,
    and whether keyboard focus showed it.
help_requested
    Raised when F1 is pressed while a tooltip with a help topic shows, with
    the target's stable ID, the item ID and the topic. The tooltip closes
    first, so the help surface the application opens replaces it.

Methods
-------
set_tool_tip
    `set_tool_tip(control, content)` maps a whole control. The control must
    be live and attached to the provider's window, and the content valid
    UTF-8 with a title, a command or a plain tooltip on the control, or
    `std::invalid_argument` is thrown.
set_item_tool_tip
    `set_item_tool_tip(strip, item_id, content)` maps one item of a strip;
    the item's command, plain tooltip or text may supply the title. Throws as
    `set_tool_tip` does, and `std::out_of_range` for an ID that names no
    item of the strip.
has_tool_tip
    `has_tool_tip(control, item_id)` says whether a control, or one of its
    items, is mapped.
remove_tool_tip
    `remove_tool_tip(control, item_id)` removes a mapping, hiding its
    tooltip; returns false when there was none.
clear
    Hides any tooltip and removes every mapping.
show
    `show(control, item_id)` shows a mapping at once, below its target, as
    keyboard focus would. An unmapped target or item throws
    `std::invalid_argument`.
hide
    Hides the showing tooltip and cancels a waiting one.

See Also
--------
ToolTip : Plain tooltip text and the window's tooltip timing.
HelpProvider : Help the user asks for with F1 on a focused control.
QuickAccessToolbar : A ribbon toolbar whose buttons take enhanced tooltips.

Notes
-----
The overlay is published to accessibility as a `tool_tip` named by the
title, with the description and disabled reason as its description. It is
painted by role: a ``card`` face, the title and shortcut as labels, the
image as an icon, the description and reason as content (the reason
disabled), and a divider over the help line. F1 is registered ahead of the
focused control's route, so a showing tooltip's topic wins over a
`HelpProvider`; with no tooltip showing, the key passes on.

Examples
--------
Enhanced tooltips for two items of a strip; the commands supply the text:

.. code-block:: cpp

   std::shared_ptr<gf::Command> bold = std::make_shared<gf::Command>("format.bold", "Bold");
   (*bold).set_description("Make the selected text bold.");
   (*bold).set_shortcut("Ctrl+B");
   std::shared_ptr<gf::Command> paste = std::make_shared<gf::Command>("edit.paste", "Paste");
   (*paste).set_availability_reason("The Clipboard is empty.");
   (*paste).set_enabled(false);
   std::shared_ptr<gf::ToolStrip> strip = gf::make_control<gf::ToolStrip>(gf::StableId("format"));
   (*strip).set_items({{.stable_id = "bold", .command = bold}, {.stable_id = "paste", .command = paste}});
   gf::Window window(strip, {320.0, 25.0});

   gf::RibbonToolTip tips(window);
   tips.set_item_tool_tip(strip, "bold", {.help_topic = "format.bold"});
   tips.set_item_tool_tip(strip, "paste", {});
   tips.show(strip, "paste");
   // tips.shown_content() has the title "Paste" and, because the command is
   // disabled, the reason "The Clipboard is empty.". F1 now does nothing:
   // paste has no help topic. Over "bold" it would raise help_requested.
   tips.hide();
