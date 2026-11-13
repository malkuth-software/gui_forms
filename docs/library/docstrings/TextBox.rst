An editable text field: one line by default, or a plain-text document with `multiline`.

The user clicks to place the caret, drags or uses Shift with the arrow keys
to select, types to replace the selection, and uses the usual editing and
clipboard shortcuts. With `accepts_enter` set, Enter in a single-line box
raises `committed`; with `accepts_escape` set, Escape raises `cancelled`.
Both suit search fields and inline editors; without them the keys go on to
the window's accept and cancel buttons. Set a
`password_character`, or `use_system_password_character`, to mask a secret.
Use a `Label` for text the user only reads, a `ComboBox` to choose from a
list, and a `NumericUpDown` for numbers. The selection, `select`, undo and
the clipboard commands are `TextBoxBase`'s, shared with `RichTextBox` and
`MaskedTextBox`; undo keeps at most 128 steps and 8 MiB of earlier text.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Logs, automation and
    semantic actions address the control by this ID.
initial_text
    Initial text. It must be valid UTF-8, or the constructor throws
    `std::invalid_argument`. The caret starts at its end.

Properties
----------
text : default ``""``
    The UTF-8 content. Setting a different value replaces it, puts the caret
    at the end, clears the undo history and raises `text_changed` and then
    `selection_changed`. It is not limited by `maximum_length`. Invalid
    UTF-8 throws `std::invalid_argument`, and more than 16 MiB throws
    `std::length_error`.
placeholder_text : default ``""``
    Hint drawn in a muted colour while the box is empty, and used as the
    accessible description when none is set. It must be valid UTF-8.
read_only : default ``false``
    Whether the user may only select and copy. Typing, deleting, pasting,
    cutting, undo, redo, `replace_selection` and assistive-technology
    set-value are refused.
multiline : default ``false``
    Whether the box edits several lines. Enter then inserts
    `newline_sequence` instead of raising `committed`; Up and Down move by
    visual line and Page Up and Page Down by a screenful, Home and End go to
    the start and end of the visual line, and Ctrl or Cmd with Home or End to
    the start and end of the text. A multi-line document is limited to 1 MiB
    and 4096 bytes per line, and edits that would exceed the limits are
    refused. Turning it on throws `std::invalid_argument` while the box is
    masked or the text exceeds the limits.
word_wrap : default ``false``
    Whether a multi-line box wraps long lines at word boundaries to its
    width. Wrapped text scrolls only vertically.
newline_sequence : default ``"\n"``
    What Enter inserts in a multi-line box: `"\n"`, `"\r"` or `"\r\n"`; any
    other value throws `std::invalid_argument`. Line endings already in the
    text are kept as they are.
accepts_tab : default ``false``
    Whether Tab inserts a tab character in a multi-line box instead of
    moving focus.
accepts_enter : default ``false``
    Whether plain Enter (no modifiers) in a single-line box is the box's own
    when it has nothing else to accept: it raises `committed` and goes no
    further. Set it on a box that is an edit session, such as a search field
    or an in-place editor. Otherwise such an Enter goes on to the containers
    and the window's accept button. A multi-line box always takes Enter.
accepts_escape : default ``false``
    Whether plain Escape (no modifiers) is the box's own when it has nothing
    else to cancel: it raises `cancelled` and goes no further. Set it on a
    box that is an edit session, such as an in-place editor. Otherwise such
    an Escape goes on to the containers and the window's cancel button.
maximum_length : default ``0``
    Largest number of Unicode scalar values the user may produce; zero means
    no limit. An edit, paste or assistive-technology set-value whose result
    would exceed it is refused entirely rather than truncated. Because the
    limit applies to the result, a longer text assigned to `text`
    refuses every edit, deletions included, until one edit brings it within
    the limit. Values above 16 Mi throw `std::out_of_range`.
password_character : default ``U'\0'``
    Character drawn once per grapheme in place of the text; `U'\0'` shows the
    text. It must be a single printable Unicode scalar, or the write throws
    `std::invalid_argument`. A multi-line box cannot be masked.
use_system_password_character : default ``false``
    Whether to mask the text with bullets (U+2022). It takes precedence over
    `password_character`.
password_protected
    Whether the text is masked by either property.
font : default ``FontSpec{FontRole::content, 12.0, 400, false}``
    Font of the text, scaled by the window's text scale. An invalid
    specification throws `std::invalid_argument`.
can_redo
    Whether `redo` would reapply an undone edit.
auto_complete_mode : default ``AutoCompleteMode::none``
    How the box completes what the user types: `suggest` opens a list of
    matching candidates under the box, `append` adds the rest of the first
    match after the typed text and selects it, and `suggest_append` does
    both. Completion acts only in a single-line, unmasked, writable box with
    no edit filter; otherwise the setting is kept and does nothing.
auto_complete_source : default ``AutoCompleteSource::none``
    Where the candidates come from. `custom_source` uses
    `auto_complete_custom_source`. The host sources (`file_system` to
    `all_system_sources`) ask the window's host services, and no host
    supplies them yet: they offer nothing. `list_items` throws
    `std::invalid_argument`, since a box has no list; an editable `ComboBox`
    offers its items through its own setting.
auto_complete_custom_source
    The `AutoCompleteStringCollection` the `custom_source` source reads.
    A new box owns an empty one; assigning another shares it, so one
    collection can serve several boxes and lives while any of them holds
    it. A null collection throws `std::invalid_argument`.
auto_complete_case_sensitive : default ``false``
    Whether matching compares the case of ASCII letters. Other characters
    always compare exactly.
auto_complete_open
    Whether the suggestion list is open.
auto_complete_suggestions
    The suggestions the open list shows: the candidates that start with the
    typed text, in source order, each text once, at most 256. Empty while
    the list is closed.
auto_complete_highlighted
    The highlighted suggestion's index, or none.

Events
------
text_changed
    Raised after the text changes, by typing, deleting, cutting, pasting,
    undo, redo, a setter or an assistive technology. It carries the new text,
    and is followed by `selection_changed`.
selection_changed
    Raised after the selection or caret moves, and after every text change.
    It carries the new selection.
committed
    Raised when the user presses plain Enter in a single-line box that
    `accepts_enter`, even a read-only one. It carries the current text. It
    is not raised for an Enter that accepts a suggestion or an appended
    completion, or that meets a composition.
cancelled
    Raised when the user presses plain Escape in a box that
    `accepts_escape`, in either mode. The text is left unchanged; restore it
    in the handler if the edit should be discarded. It is not raised for an
    Escape that closes the suggestion list, removes an appended completion,
    or meets a composition or a selection drag.
auto_complete_open_changed
    Raised with `true` after the suggestion list opens and with `false`
    after it closes, whichever way it closed.
auto_complete_accepted
    Raised when the user accepts a completion: a suggestion chosen with
    Enter, Tab or the pointer, or an appended completion taken with Enter or
    Tab. It is raised after the text holds the completion and the list has
    closed, and carries the accepted text.

Methods
-------
replace_selection
    Replaces the selection with UTF-8 text as one undoable edit, as typing
    does, and leaves the caret after it. It returns false, changing nothing,
    when the box is read-only, the text is not valid UTF-8, or a limit would
    be exceeded.
delete_selection
    Removes the selection as one undoable edit. It returns false when nothing
    is selected or the box is read-only.
redo
    Reapplies the last undone edit, and returns false when there is none or
    the box is read-only. A new edit clears the redo steps.
set_edit_filter
    Installs a filter that sees every user edit (typing, deletion, paste and
    assistive set-value, as a `TextEdit` with its `TextEditKind`) before it
    is applied, and accepts it, refuses it, or rewrites the whole text and
    places the caret. Assigning `text`, undo and redo do not pass through it. A
    filter may set the text itself only when it refuses the edit, to show a
    value it looked up (a read-only `DomainUpDown` does); an edit during whose
    filter call the text changed is abandoned whatever the verdict. The delegate does not own its target; clear
    it with an empty `Delegate` before the target goes away. `MaskedTextBox`
    uses it to put input through its mask.
auto_complete_source_status
    Whether `auto_complete_source` can offer candidates. It is accepted for
    `none` and `custom_source`. For a host source it is the answer of the
    window's host services, which is `HostServiceError::unsupported` from
    every host today, and also without a host.
set_auto_complete_anchor
    Sets the control the suggestion list opens under and is as wide as: by
    default the box itself. It must contain the box; the box observes it
    without owning it, and an anchor that is gone or does not contain the
    box is ignored. An editable `ComboBox` sets itself, so the list spans
    the whole field.
close_auto_complete
    Closes the suggestion list and ends an appended completion, keeping the
    text as it is.
visual_line_count
    The number of displayed lines after wrapping; 1 for a single-line box.
validate_multiline_text
    Checks text against the multi-line limits without changing anything,
    returning the first problem found.

See Also
--------
Label : Read-only text that names a field.
ComboBox : A text field with a drop-down list.
NumericUpDown : A field for a number with step buttons.

Notes
-----
Its `tool_tip_text` shows as grey hint text in the empty box while it has
focus, in place of a tooltip bubble; a placeholder takes precedence.
Resting the pointer on the box still shows the bubble.

Keyboard, while the box has focus: Left and Right move by grapheme, with Ctrl
or Alt by word, and with Cmd (Meta) to the start or end of the text; Home and
End go to the start and end of a single-line box; Shift with any of these
extends the selection. Backspace and Delete remove the selection, or else one
grapheme, a word with Ctrl or Alt, or everything before or after the caret
with Cmd. Ctrl or Cmd with A selects all; with C, X and V they copy, cut and
paste; with Z they undo, and with Shift+Z or Y they redo. Ctrl and Cmd are
both accepted on every platform. Text input replaces the selection.

During an input-method composition the box keeps Enter and Escape from the
window and leaves them to the input method. Otherwise, in a multi-line box
Enter inserts `newline_sequence`. In a single-line box Enter is the box's
only while an open suggestion list or appended completion is there to
accept. With `accepts_enter` set, a plain Enter with nothing else to accept
raises `committed` and stops there. Every other Enter goes on unhandled to
the containers and the window's accept button, as in Windows Forms, where
the form's AcceptButton receives it.

Escape is the box's only while it has something to cancel: an open
suggestion list or appended completion (Escape closes or removes it), a
composition, or a selection drag (Escape ends it, keeping what is
selected). With `accepts_escape` set, a plain Escape with nothing else to
cancel raises `cancelled` and stops there. Every other Escape, with or without Shift, goes
on unhandled to the containers, menus and the window's cancel button, as in
Windows Forms, where the form's CancelButton receives it.

Auto-complete runs after each edit the user makes. Typing at the end of the
text in `append` or `suggest_append` mode appends the rest of the first
candidate that starts with the typed text, selected, so the next character
replaces it; the typed text keeps its own case. Deleting, pasting, typing
before the end and input-method composition never append, and nothing is
appended past `maximum_length`. In `suggest` and `suggest_append` mode a
list of the matching candidates opens under the box, each with the typed
prefix highlighted; it never takes the focus, so typing goes on in the box,
and it closes when nothing matches.
Matching is by prefix and ignores the case of ASCII letters unless
`auto_complete_case_sensitive` is set.

While the list is open, Down highlights the first suggestion and then the
next, and from the last returns to the typed text; Up goes the other way.
The highlighted suggestion is shown in the box. Enter or Tab accepts the
highlighted suggestion, or the appended completion when nothing is
highlighted, and is consumed: the next Enter or Tab does its usual work.
Enter or Tab with nothing to accept closes the list and does its usual
work. Escape closes the list and puts back what the user typed,
removing an appended completion. Pressing a row highlights it and releasing
on it accepts it. A press outside the list closes it, keeping the text; so
do moving the caret, losing the focus, assigning `text`, undo and redo. A
completion belongs to the edit that produced it: one undo removes both.
Accepting raises `text_changed` (when the text changes), then
`auto_complete_open_changed` with `false` if the list was open, then
`auto_complete_accepted`.

Composition text is the input method's: while a composition is in progress
nothing is appended and the list closes, and the text is left exactly as
the input method wrote it. Completion starts again with the first committed
input.

Pointer: pressing the primary button focuses the box and places the caret,
with Shift extending the selection, and dragging selects. In a multi-line box
the wheel scrolls three lines per notch.

The selection is drawn only while the box has focus. The caret blinks every
530 ms while focused; each blink is one scheduled repaint, and nothing is
scheduled when the box does not have focus. Clipboard commands use the
clipboard of the host attached to the window, and return false when there is
none.

The control publishes the accessibility role `text_box`. Its name is the
accessible name only, so give every box one by writing `accessible_name`; the
placeholder becomes the description, not the name. The value is the text, or
empty when masked, together with the protected-content state. The box also
publishes whether it is multiline and, unless masked, its selection as an
anchor and a caret. It reports
read-only when `read_only` is true, and otherwise offers a set-value action,
which replaces the whole text as one undoable edit within `maximum_length`.
A box that suggests also publishes that it opens an overlay, and the expanded
state while its list is open; the open list publishes the role `list` with
`list_item` rows.

Examples
--------
A search field that acts when the user presses Enter:

.. code-block:: cpp

   struct Search final : gf::Component {
       std::shared_ptr<gf::TextBox> field =
           gf::make_control<gf::TextBox>(gf::StableId("search.query"));
       std::string query;

       Search() {
           (*field).placeholder_text = "Search";
           (*field).accessible_name = "Search";
           (*field).accepts_enter = true;
           gf::on((*field).committed(), *this, &Search::on_commit);
       }
       void on_commit(const std::string& text) { query = text; }
   };

   void example() {
       Search search;
       gf::Window window(search.field, gf::Size{200.0, 28.0});
       window.request_focus(search.field);
       window.dispatch_text(gf::TextInputEvent{"retained"});
       window.dispatch_key(gf::KeyEvent{gf::KeyAction::down, gf::PhysicalKey::enter});
       // search.query is now "retained"
   }

Edit a multi-line note. Each replacement is one undo step:

.. code-block:: cpp

   std::shared_ptr<gf::TextBox> note =
       gf::make_control<gf::TextBox>(gf::StableId("note"), "Draft");
   (*note).multiline = true;
   (*note).word_wrap = true;
   (*note).select_all();
   (*note).replace_selection("First line\nSecond line");
   (*note).undo();   // the text is "Draft" again, and can_redo() is true

A city field that suggests from its own list and appends the first match:

.. code-block:: cpp

   std::shared_ptr<gf::TextBox> city =
       gf::make_control<gf::TextBox>(gf::StableId("address.city"));
   (*(*city).auto_complete_custom_source.read()).add_range({"Calgary", "Cambridge", "Edmonton"});
   (*city).auto_complete_source = gf::AutoCompleteSource::custom_source;
   (*city).auto_complete_mode = gf::AutoCompleteMode::suggest_append;
   gf::Window window(city, gf::Size{240.0, 200.0});
   window.request_focus(city);
   window.dispatch_text(gf::TextInputEvent{"ca"});
   // The box shows "calgary" with "lgary" selected, and the list offers
   // Calgary and Cambridge.
   window.dispatch_key(gf::KeyEvent{gf::KeyAction::down, gf::PhysicalKey::tab});
   // Tab accepted the completion: the text is "calgary", the caret at its end.
