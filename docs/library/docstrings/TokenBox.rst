A text field that turns each accepted entry into a removable token.

A token box shows its tokens first to last, wrapping onto further lines,
with an `editor` text box after the last. The user types an entry and ends
it with Enter or a delimiter character, or accepts a completion of the
editor's auto-complete; the entry becomes a token and the editor empties.
With no entry typed (nothing, or only spaces), Enter is not the box's: it
goes on to the window's accept button.
Each token has a close glyph that removes it, and the keyboard can select
and remove tokens without leaving the editor. Use it for lists of short
values such as recipients, tags or keywords. It is a GUI.Forms-only control;
Windows Forms has none.

Parameters
----------
stable_id
    Identity of the control, unique within its window. The editor is
    ``<stable_id>.editor``; a token's semantic node and realized item are
    ``<stable_id>.token.<identity>``, and its remove button
    ``<stable_id>.token.<identity>.remove``.

Properties
----------
tokens
    The tokens, first to last, each a `Token` with its `TokenId` and text.
selected_token : default none
    The token the keyboard or the pointer selected, or none. `select_token`
    selects one, or clears the selection; an identity not in the box throws
    `std::invalid_argument`.
delimiters : default ``",;"``
    The characters that end an entry when typed or pasted. Only ASCII
    punctuation and the space are admitted; anything else throws
    `std::invalid_argument`. With none, only Enter ends an entry.
font : default ``FontSpec{FontRole::content, 12.0, 400, false}``
    The font of the tokens and the editor. An invalid specification throws
    `std::invalid_argument`.
editor
    The `TextBox` the user types into. It keeps the keyboard focus for the
    box.
auto_complete_mode : default ``AutoCompleteMode::none``
    The editor's auto-complete mode (see `TextBox`).
auto_complete_source : default ``AutoCompleteSource::none``
    The editor's auto-complete source. `list_items` throws
    `std::invalid_argument`: the box has no list.
auto_complete_custom_source
    The editor's custom source collection, shared as `TextBox`'s is.
parts
    The published parts: ``region``, ``well``, ``field`` (where the editor
    sits) and ``focus``.
states
    The published states; the box is focused while its editor is.
realized_items
    The tokens laid out inside the box, with their parts and states.

Events
------
token_added
    Raised after a token is added, with the token.
token_removed
    Raised after a token is removed, with the token as it was. Removing the
    selected token first raises `selected_token_changed`.
selected_token_changed
    Raised after `selected_token` changes, with the new selection.

Methods
-------
add_token
    Adds a token after the last and returns its identity. The text is
    trimmed of ASCII spaces; text that is not valid UTF-8, or empty once
    trimmed, throws `std::invalid_argument` and adds nothing.
remove_token
    Removes the token with an identity, returning false, and changing
    nothing, when no token in the box has it.
clear_tokens
    Removes every token, last first.
token_index
    The position of the token with an identity, or none.
initialize_control_tree
    Creates the editor. `make_control` calls it; do not call it yourself.

See Also
--------
TextBox : The editor, and auto-complete.
AutoCompleteStringCollection : Candidates for the editor's suggestions.

Notes
-----
A token's `TokenId` is assigned when it is added and never reused, so it
stays valid while other tokens come and go; positions are not identities.
Text with several delimiters, typed or pasted, makes one token per
delimited piece, skipping empty pieces, and the text after the last
delimiter stays in the editor. A delimiter ends the entry as typed: an
appended completion is taken by Enter or Tab instead.

Keyboard, with the caret at the start of an empty selection in the editor:
Backspace or Left selects the last token. With a token selected, Backspace
or Delete removes it, Left and Right select the previous and next token
(Right past the last returns to the editor), Home selects the first token,
and End or Escape returns to the editor. Typing returns to the editor, and
so does losing the focus. While the editor's suggestion list is open its
keys are the list's.

Pointer: pressing a token selects it; the close glyph removes its token
when released over it, as a button does. Both keep the focus in the
editor.

Tokens wrap onto further lines at the box's width. The box does not
scroll: lines below its height are not laid out, painted or published,
and `measure` asks for the height every line needs.

Each token is an item of the ``token`` item system with the parts
``token_face`` (a `chip`), ``token_label``, ``token_close`` (a `glyph`
with the `close` mark, hot and pressed of its own) and ``token_focus``,
and the states `selected`, `hot` and `focus_visible`, all painted through
the by-role paint path. The control publishes the accessibility role
`list`, with each token a `list_item` that carries its position in the set
and can be selected, holding a `button` that removes it.

Examples
--------
A recipients field that suggests names and starts with one recipient:

.. code-block:: cpp

   std::shared_ptr<gf::TokenBox> recipients =
       gf::make_control<gf::TokenBox>(gf::StableId("mail.to"));
   (*(*recipients).auto_complete_custom_source.read()).add_range({"Ada Lovelace", "Alan Turing"});
   (*recipients).auto_complete_source = gf::AutoCompleteSource::custom_source;
   (*recipients).auto_complete_mode = gf::AutoCompleteMode::suggest;
   const gf::TokenId grace = (*recipients).add_token("Grace Hopper");
   gf::Window window(recipients, gf::Size{400.0, 60.0});
   window.request_focus((*recipients).editor());
   window.dispatch_text(gf::TextInputEvent{"Barbara Liskov,"});
   // The box holds "Grace Hopper" and "Barbara Liskov"; the editor is empty.
   (*recipients).remove_token(grace);   // now only "Barbara Liskov"
