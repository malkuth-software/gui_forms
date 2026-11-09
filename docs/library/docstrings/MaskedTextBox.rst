A text box that takes input through a mask.

A masked text box shows a fixed pattern, such as ``(000) 000-0000`` for a
telephone number, in its `editor` text box: each position the user fills
shows a prompt character until it is filled, and the pattern's literals
(the brackets, the space and the hyphen) stand in place. Typing, deletion,
paste and assistive set-value all go through the mask: input that does not
fit is refused, and `mask_input_rejected` reports where and why. Use it for
values with a fixed shape, such as codes, telephone numbers and dates typed
as digits; use a `TextBox` for free text.

Parameters
----------
stable_id
    Identity of the control, unique within its window. The editor is created
    as ``<stable_id>.editor``.
initial_mask : default empty
    The mask, in the language of `MaskedTextProvider`. An empty mask takes
    any text, as a plain `TextBox`.

Properties
----------
mask
    The mask. Assigning a new one replaces it and places the assigned characters again
    from the first position (without prompts or literals); when the new mask
    refuses them, the box starts empty. An invalid mask throws
    `std::invalid_argument`.
culture
    The symbols the mask's ``.``, ``,``, ``:``, ``/`` and ``$`` stand for.
    GUI.Forms has no culture service; the default is the invariant culture
    (``.``, ``,``, ``:``, ``/`` and the generic currency sign U+00A4).
    Assigning a culture replaces them as assigning `mask` replaces the mask.
provider
    The `MaskedTextProvider` that holds the mask and the assigned characters,
    for reading. Edits go through the box, so that the field and the events
    follow.
text
    The text in `text_mask_format`. `set_text` clears the box and places its
    argument from the first position, passing over literals it repeats; text
    the mask refuses leaves the box unchanged, raises `mask_input_rejected`,
    and is reported in the returned `MaskEditResult`. `clear` unassigns every
    position.
text_mask_format : default ``MaskTextFormat::include_literals``
    Which characters `text` keeps: prompts, literals, both or neither. Without
    prompts an unassigned position is a space and the text ends at the last
    assigned position. An unknown value throws `std::invalid_argument`.
mask_completed
    Whether every required position is assigned.
mask_full
    Whether every position, required or optional, is assigned.
prompt_character : default ``'_'``
    The character an unassigned position shows: any non-control character.
    Anything else throws `std::invalid_argument`.
allow_prompt_as_input : default ``true``
    Whether the prompt character may be typed; with `reset_on_prompt` it
    clears its position.
reset_on_prompt : default ``true``
    Whether a typed prompt character clears its position.
reset_on_space : default ``true``
    Whether a typed space clears its position instead of being tested.
ascii_only : default ``false``
    Whether characters past the ASCII range are refused everywhere.
skip_literals : default ``true``
    Whether a typed character equal to the literal at the caret passes over
    it.
reject_input_on_first_failure : default ``false``
    Whether one refused character refuses a whole paste. Off, a paste keeps
    the characters the mask takes and reports each one it drops.
overwrite_mode : default ``false``
    Whether typing replaces the character at the caret instead of inserting
    before it. The Insert key toggles it.
read_only : default ``false``
    Whether the editor refuses typing.
editor
    The `TextBox` that shows the prompts, literals and assigned characters.

Events
------
mask_input_rejected
    Raised for each refused input, before the edit returns, with the position
    the character was tested at, the `MaskResultHint` reason and the refused
    text.
text_changed
    Raised after `text` changes, with the new text.
mask_changed
    Raised after an assignment changes `mask`, with the new mask.
overwrite_mode_changed
    Raised after `overwrite_mode` changes, with the new mode.
type_validation_completed
    Raised after the validation hook runs, with its `MaskValidation` record.

Methods
-------
set_validator
    Installs the validation hook, a `Delegate` that reads the
    `MaskValidation` record's text and writes whether it is valid, a message,
    and whether to keep the focus. The hook runs when the focus leaves the
    box and from `validate_text`. The delegate does not own its target; clear
    it before the target goes away. Without a hook, validation reports valid
    and raises nothing.
validate_text
    Runs the validation hook, raises `type_validation_completed`, and returns
    whether the text is valid.
initialize_control_tree
    Creates the editor. `make_control` calls it; do not call it yourself.

See Also
--------
MaskedTextProvider : The mask and its text, without a control.
TextBox : A text box for free text.
DateTimePicker : A date chosen from a calendar.

Notes
-----
Its `tool_tip_text` shows as grey hint text in the edit field when the
field is empty and has focus, in place of a tooltip bubble.

The caret passes over literals: after a character is typed it moves to the
next position the user can fill. Typing inserts before the caret (later
characters move on) unless `overwrite_mode` is on. Backspace and Delete
remove a character and move the later ones back; over a literal they only
move the caret, since a literal cannot be removed. Undo and redo restore the
field and the provider follows it.

The editor's edit filter (`TextBox::set_edit_filter`) carries every user edit
to the provider, so the box composes a stock `TextBox` rather than copying
it.

Letters (``L``, ``?``) and alphanumerics (``A``, ``a``) are ASCII only, and
case shifts convert ASCII letters only: GUI.Forms has no Unicode
character-category data. The prompt is not hidden when the box loses focus,
and there is no password character yet.

The control publishes the accessibility role `text_box` with the field as
shown (prompts and literals in place) as its value, the read-only state, and
the editor's selection while the editor shows that value.
`SemanticAction::set_value` goes through the mask as `set_text` does and
fails for text the mask refuses.

Examples
--------
A telephone field that reports refused characters:

.. code-block:: cpp

   std::shared_ptr<gf::MaskedTextBox> phone =
       gf::make_control<gf::MaskedTextBox>(gf::StableId("contact.phone"), "(000) 000-0000");
   (*phone).set_text("5551234567");   // the field shows "(555) 123-4567"
   const bool complete = (*phone).mask_completed();   // true
   const gf::MaskEditResult refused = (*phone).set_text("55x");
   // refused.hint == gf::MaskResultHint::digit_expected; the text is unchanged
