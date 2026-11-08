A brief notice shown for a while above the bottom of a window, such as
"Message archived." with an Undo button after a deletion.

A toast is the stock content of `Window::show_toast`, which places it in
the window's toast overlay, centred 16 units above the bottom of the
client area, and closes it when its duration runs out (five seconds by
default; zero keeps it until it is dismissed). The control draws the
notice face with the overlay shadow, the severity mark, the message on one
line, the action button and a close mark. Its content is fixed when it is
made: show another toast to say something else.

Parameters
----------
stable_id
    Identity of the control, unique within its window.
content
    A `ToastContent`: the `message` (not empty), the `action_text` (empty
    for no action button), the `severity` (a `NoticeSeverity`, default
    information) and `closable` (default true). An empty message, or a
    message or action text that is not valid UTF-8, throws
    `std::invalid_argument`.

Properties
----------
content
    The `ToastContent` the toast was made with.
action
    The action button, owned as a child, or null without action text.

Events
------
action_invoked
    Raised when the action button is pressed, before the window dismisses
    the toast with `ToastDismissReason::action`. The window raises
    `toast_dismissed` last, once the toast has left it.

Methods
-------
close
    Asks the window to dismiss the toast with `ToastDismissReason::closed`,
    as the close mark and the close accessibility action do. Returns false,
    changing nothing, when the toast is not its window's showing toast.

See Also
--------
InfoBanner : A message strip across a form or a page, which stays.

Notes
-----
A window shows one toast at a time; a new one replaces the one that shows
(dismissed `replaced`), and the toast closes when its owner leaves the
window. When it closes, the toast leaves the overlay detached, so the same
control may be shown again. The presenter follows the tooltip presenter's
pattern and moves with it to lightweight Splash windows in Part S. The
parts take the theme's ``notice`` family; the severity is the toast's
custom state, exactly one of information, warning, success and error. The
toast publishes the accessibility role `live_region`, named by its message,
with the close action while closable; it takes no focus, and the action
button is reachable inside it.

Examples
--------
A success toast with an Undo action, shown by the button that archived the
message:

.. code-block:: cpp

   std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("mail"));
   std::shared_ptr<gf::Button> archive = gf::make_control<gf::Button>(gf::StableId("mail.archive"), "Archive");
   (*root).add_child(archive);
   gf::Window window(root, gf::Size{640.0, 480.0});
   gf::ToastContent content{};
   content.message = "Message archived.";
   content.action_text = "Undo";
   content.severity = gf::NoticeSeverity::success;
   std::shared_ptr<gf::Toast> toast = gf::make_control<gf::Toast>(gf::StableId("mail.archived"), std::move(content));
   std::uint64_t id = window.show_toast(archive, toast);
   // id is 1; the toast closes after five seconds unless dismissed sooner
