Underlined text that the user clicks like a hyperlink.

A link label shows its caption in the theme's link colour with an underline.
Clicking it, pressing Space or Enter while it has focus, or pressing its
mnemonic marks it visited, which changes it to the visited-link colour, and
then raises `clicked`; the application does the navigation, such as showing a
help page or opening details. The whole caption is one link. Use a `Label`
for text that does nothing, and a `Button` for an action that should look
like a command.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Logs, automation and
    semantic actions address the control by this ID.
text
    Caption. `&` marks the next character as the mnemonic, so `"&Details"`
    responds to Alt+D; write `&&` for a literal ampersand.

Properties
----------
visited : default ``false``
    Whether the link draws in the visited colour. Activation sets it to true;
    set it back to false yourself if the target can become new again. It
    raises no event.

See Also
--------
Label : Static text without interaction.
Button : A push button for commands.
ButtonBase : Input, mnemonics and command binding shared with buttons.

Notes
-----
When the user activates the link, `visited` becomes true first and `clicked`
is raised afterwards, so a handler already sees the visited state.

The link is drawn as one line of text at the left, vertically centred, with
an underline as wide as the text, in the text colour of the theme's `link`
recipe family, or of `visited_link` once visited, for the control's state:
the built-in theme gives every enabled state its compatibility style's
`link` or `visited_link` colour (hover does not change it) and the disabled
state its `disabled_text`. A style set with `set_style` keeps its own
`link`, `visited_link` and `disabled_text` outside high contrast. The
``underline`` part is drawn by the ``glyph`` role with the ``underline``
mark, in the text's colour. A link draws no plate. It does not draw the inherited
`image`, and ignores `text_alignment` and multi-line captions. A focus
rectangle is drawn around it when it has keyboard focus.

Input is that of `ButtonBase`: the link takes focus when clicked, and is
activated by a click, by Space or Enter on release, by Alt with its mnemonic,
and by `perform_click`.

The control publishes the accessibility role `link`, with the value
`visited` or `unvisited`. Its name is the accessible name, or else the caption
without the mnemonic marker.

Examples
--------
Show details when the link is clicked:

.. code-block:: cpp

   struct Report final : gf::Component {
       std::shared_ptr<gf::LinkLabel> details =
           gf::make_control<gf::LinkLabel>(gf::StableId("report.details"), "Show &details");
       bool showing = false;

       Report() { gf::on((*details).clicked(), *this, &Report::on_details); }
       void on_details() { showing = true; }
   };

   void example() {
       Report report;
       (*report.details).perform_click();   // report.showing and visited.read() are now true
   }
