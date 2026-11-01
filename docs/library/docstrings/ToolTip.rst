The Windows Forms way to give controls short help text and to set when it shows.

Tooltips belong to the things they describe: every control has a
`tool_tip_text`, and each window has one presenter that shows it when the
pointer rests on the control, or when the control receives keyboard focus
while the focus cue is showing. A tool tip shows nothing itself.
`set_tool_tip` writes the control's text, and the timing and policy
properties read and change the window's `ToolTipSettings`, which every tool
tip of the window shares: the last value set holds. Text controls (a text
box, a combo box's edit field) show their text as light grey hint text in the
empty field instead of a bubble when they receive focus. Data items (list
rows, tree nodes) have no tooltips; a list reveals cut-off text in place. Use
a `HelpProvider` for help that the user asks for, and an `ErrorProvider` to
flag invalid input.

Parameters
----------
window
    The window whose controls the tool tip serves. The tool tip cannot be
    changed or shown once that window has shut down.

Properties
----------
initial_delay : default ``500 ms``
    How long the pointer must rest on a control, or the control must have
    keyboard focus, before its tooltip appears. Moving the pointer starts
    the rest again. A negative delay throws `std::invalid_argument`.
reshow_delay : default ``100 ms``
    The shorter delay used instead when a tooltip closed no more than
    `initial_delay` ago, so that moving from one control to the next shows
    each tooltip quickly. A negative delay throws `std::invalid_argument`.
auto_pop_delay : default ``5000 ms``
    How long a tooltip stays up. Zero keeps it up until the pointer leaves,
    focus moves or `hide` is called. A negative delay throws
    `std::invalid_argument`.
show_on_hover : default ``true``
    Whether the pointer resting on a control shows its tooltip.
show_on_focus : default ``true``
    Whether keyboard focus reaching a control shows its tooltip, placed
    below the control.
show_always : default ``false``
    Whether tooltips also appear for disabled controls, for example to
    explain why a command is unavailable. Hidden controls never show them.
maximum_width : default ``336.0``
    The widest a tooltip's text may be before it wraps onto further lines.
    It must be finite and between 80 and 2048, or `std::invalid_argument` is
    thrown. Changing it reshapes a tooltip that is showing.
visible : default ``false``
    Whether a tooltip of a control this tool tip set text on is showing.
active_control
    That control, or null.

Events
------
visibility_changed
    Raised when the tooltip of a control this tool tip set text on appears
    and when it disappears, with the control's stable ID, the text, whether
    it is now shown, and whether keyboard focus showed it.

Methods
-------
set_tool_tip
    `set_tool_tip(control, text)` sets a control's `tool_tip_text`; empty
    text removes it. The control must be live and attached to the tool
    tip's window, and the text valid UTF-8, or `std::invalid_argument` is
    thrown. Changing the text of a tooltip that is showing updates it at
    once.
tool_tip
    A control's tooltip text, whoever set it, or an empty string.
remove_tool_tip
    Clears the text this tool tip set on a control, closing its tooltip and
    cancelling a pending one. Returns false, changing nothing, when this
    tool tip set none there.
clear
    Clears the text this tool tip set on every control.
show
    `show(control)` shows the control's tooltip at once, placed below the
    control, for `auto_pop_delay`; `show(control, duration)` shows it for
    `duration`, where zero means until it is hidden. It shows even when the
    window's or the program's tooltips are switched off. A control this
    tool tip set no text on throws `std::invalid_argument`, as does a
    negative duration. A hidden control shows nothing, and so does a
    disabled one unless `show_always` is true.
hide
    Closes the window's tooltip when it shows one of this tool tip's
    controls.

See Also
--------
Window : The presenter, its `ToolTipSettings`, and the window switch `set_tool_tips_shown`.
Application : The program switch `set_tool_tips_shown`.
RibbonToolTip : Enhanced tooltips with a title, description and shortcut.
HelpProvider : Help text that the user requests.
ErrorProvider : An error icon and message beside a control.

Notes
-----
A tooltip shown by the pointer appears just below and to the right of the
pointer; one shown by focus or by `show` appears just below the control's
left edge. Either way it moves above when there is no room below and stays
at least 4 pixels inside the window. A tooltip that is showing follows its
control when the control is laid out somewhere else. Pressing a pointer
button closes the tooltip, and nothing shows again until the pointer leaves
the control.

The tooltip ignores the pointer, so the control underneath keeps receiving
clicks and hover. Disposing or hiding the control closes its tooltip.
Disposing the tool tip clears the text it set.

Changing, showing or hiding a tool tip must happen on the window's UI
thread. Doing so after the tool tip is disposed, or after its window has
shut down, throws `std::logic_error`.

The tooltip publishes the accessibility role `tool_tip`, named by its text,
while it is showing, and every control publishes its text as the semantic
field `tool_tip`.

Examples
--------
Register help for a toolbar button, then show it immediately:

.. code-block:: cpp

   std::shared_ptr<gf::Panel> toolbar =
       gf::make_control<gf::Panel>(gf::StableId("toolbar"));
   std::shared_ptr<gf::Button> save =
       gf::make_control<gf::Button>(gf::StableId("toolbar.save"), "Save");
   (*save).set_requested_bounds({8.0, 8.0, 80.0, 28.0});
   (*toolbar).add_child(save);
   gf::Window window(toolbar, {320.0, 200.0});

   gf::ToolTip tips(window);
   tips.set_tool_tip(save, "Save the document (Ctrl+S)");
   // (*save).tool_tip_text() is now that text; resting the pointer on the
   // button, or tabbing to it, shows it after 500 ms.

   tips.show(save, std::chrono::milliseconds(0));   // until hide()
   // tips.visible() is true and tips.active_control() is save.
   tips.hide();

Mirror each tip in a status bar text as it appears and disappears:

.. code-block:: cpp

   struct StatusHelp final : gf::Component {
       std::string status;

       void on_tip(const gf::ToolTipEvent& event) {
           status = event.shown ? event.text : std::string();
       }
   };

   void example() {
       std::shared_ptr<gf::Button> open =
           gf::make_control<gf::Button>(gf::StableId("open"), "Open");
       gf::Window window(open, {120.0, 40.0});
       gf::ToolTip tips(window);
       StatusHelp help;
       gf::on(tips.visibility_changed(), help, &StatusHelp::on_tip);

       tips.set_tool_tip(open, "Open a file");
       tips.show(open, std::chrono::milliseconds(0));  // help.status is "Open a file"
       tips.hide();                                    // and now empty
   }
