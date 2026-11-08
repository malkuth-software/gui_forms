A component that attaches help text and help topics to controls and routes F1.

A help provider stores, for each control, a help string, a keyword for a help
topic and a `HelpNavigator` saying how to look it up. When the user presses
F1, it finds the help for the focused control or its nearest ancestor that has
some, and raises `help_requested`; it never opens a browser, a help file or a
network resource itself, so the application decides what help means. The help
string also becomes part of the control's accessible description. Use an
`ErrorProvider` for validation messages and a `ToolTip` for hints shown on
hover.

Parameters
----------
window
    The window whose controls the provider serves. Construct the provider on
    that window's UI thread. It registers F1, without modifiers, as an
    accelerator of the window.

Properties
----------
help_namespace : default ``""``
    The help collection to search, such as a URL or a help-file name. It is
    passed to handlers in `HelpRequestEvent::help_namespace` and never
    interpreted. It must be valid UTF-8.
tag
    Application data, kept as given and never interpreted.
snapshot
    A `HelpProviderSnapshot` with the number of controls that have entries,
    how many of them currently offer help, and how many requests were raised
    and handled.

Events
------
help_requested
    Raised when help is requested for a control that offers it and the
    control's own `help_requested` handlers did not handle the request. The
    `HelpRequestEvent` carries the control's stable ID, the position, the
    `help_namespace`, the control's help string, keyword and navigator, and
    whether the request came from the keyboard. Set `handled` to report that
    help was shown. It is also raised for the window's own help request (a
    message box's Help button or F1, `Window::request_help`) that the
    window's `help_requested` handlers did not handle: the window's
    providers are offered it in the order they were created, until one
    handles it, each with its own namespace and the root control's help
    string, keyword and navigator where it maps the root. A disposed
    provider is not offered it.

Methods
-------
can_extend
    Whether the provider can serve a control: the control must be alive,
    attached to the provider's window and registered there under its stable
    ID.
set_help_string
    `set_help_string(control, text)` sets the help text for a control, shown
    to assistive technology and passed to handlers. The text must be valid
    UTF-8. Throws `std::invalid_argument` when `can_extend` is false for the
    control.
help_string
    The control's help text, or an empty string.
set_help_keyword
    `set_help_keyword(control, keyword)` sets the topic keyword passed to
    handlers. It is published as the accessible description when the
    control has no help string.
help_keyword
    The control's keyword, or an empty string.
set_help_navigator
    `set_help_navigator(control, navigator)` sets how a handler should look
    the keyword up: by topic, table of contents, index, search, associated
    index or keyword index. The default is `HelpNavigator::topic`.
help_navigator
    The control's navigator, `HelpNavigator::topic` unless set.
set_show_help
    `set_show_help(control, show)` overrides whether the control offers help.
    False suppresses F1 help and the accessible description even when a help
    string or keyword is set.
show_help
    Whether the control offers help: the value given to `set_show_help`, or,
    when none was given, whether it has a help string or a keyword. False for
    a control without an entry.
reset_show_help
    Removes the `set_show_help` override, so that `show_help` again follows
    the help string and keyword.
clear
    Removes the help of every control.
request_help
    `request_help(control, position, keyboard_initiated = false)` requests
    help for a control as F1 does, and returns whether a handler handled it.
    It returns false without raising anything when the control does not offer
    help. For a keyboard request, the position becomes the centre of the
    control in window coordinates.

See Also
--------
ErrorProvider : Marks controls with validation errors.
ToolTip : Shows a hint when the pointer rests on a control.

Notes
-----
When F1 reaches the window, which happens only when the focused control does
not handle the key itself, the provider starts at the focused control, or at
the root when nothing has focus, and walks up through the parents to the
first control that offers help. It then calls `request_help` for it: first
the control's own `help_requested` event is raised, and only if no handler
set `handled` is the provider's `help_requested` raised. F1 counts as handled
only when a handler set `handled`.

A control that offers help gets its help string, or its keyword when it has
no string, appended to its accessible description. With several providers,
their texts appear in the order in which the providers were created.

Use the provider only on the window's UI thread. Its setters and its queries
that take a control throw `std::logic_error` from another thread. After
disposal, or after the window has shut down, the members that change the
provider throw `std::logic_error`. Disposal, and destruction, remove the
provider's help from every control and unregister F1.

Examples
--------
Show help for the focused control when the user presses F1:

.. code-block:: cpp

   struct HelpDesk final : gf::Component {
       std::string last_keyword;
       void on_help(gf::HelpRequestEvent& request) {
           last_keyword = request.keyword;   // open the topic here
           request.handled = true;
       }
   };

   void example() {
       std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("form"));
       std::shared_ptr<gf::Button> connect =
           gf::make_control<gf::Button>(gf::StableId("form.connect"), "Connect");
       (*root).set_requested_bounds({0.0, 0.0, 320.0, 120.0});
       (*connect).set_requested_bounds({20.0, 20.0, 120.0, 32.0});
       (*root).add_child(connect);
       gf::Window window(root, gf::Size{320.0, 120.0});
       window.perform_layout();

       gf::HelpProvider help(window);
       HelpDesk desk;
       gf::on(help.help_requested(), desk, &HelpDesk::on_help);
       help.help_namespace = "app-help://network";
       help.set_help_string(connect, "Connects to the server named above.");
       help.set_help_keyword(connect, "network.connect");

       static_cast<void>(window.request_focus(connect));
       static_cast<void>(window.dispatch_key(
           {gf::KeyAction::down, gf::PhysicalKey::f1, gf::Modifier::none}));
       // desk.last_keyword is now "network.connect"
   }
