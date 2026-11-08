A text field for a query, such as the search box above a list of messages.

The control draws a well holding the search mark, an inner TextBox (the
`editor`, which takes the focus), a clear mark while there is text, and an
activity strip along its bottom while `busy`.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
text : default empty
    The query, a view onto the editor's text. A change raises
    `text_changed` and, once typing has paused for `search_delay`,
    `search_requested`.
placeholder_text : default empty
    The hint the empty field shows, a view onto the editor's placeholder;
    it describes the field to assistive technology.
search_delay : default 300 ms
    How long typing must pause before `search_requested` is raised; zero
    raises it at each change. Negative or over 10 s throws
    `std::invalid_argument`.
busy : default false
    Whether a search is running; the application sets and clears it.
editor
    The inner TextBox, owned as the field's only child.

Events
------
text_changed
    Raised after the text changes, with the new text.
search_requested
    Raised when a search is due, with the query: after the pause, at once
    outside a window or with a zero delay, on Enter, and with the empty
    query after the field is cleared.
cleared
    Raised after `clear`, Escape or the clear mark empties the field.

Methods
-------
search_now
    Raises `search_requested` with the query now, dropping a pending one.
clear
    Empties the field: raises `cleared`, then `search_requested` with the
    empty query. Returns false, raising nothing, when the field is empty.

See Also
--------
TextBox : The editor the field holds.
TokenBox : Entries kept as tokens.

Notes
-----
The search delay runs on the window's frame schedule. Escape in a field
with text clears it; in an empty field Escape goes on to the containers
and the window's cancel button. The well and the marks take the theme's
``editor`` family and the activity strip the ``progress`` family. While the
field is busy and drawn, a segment runs along the strip once every 1.2 s on
the window's motion clock; under reduced motion, with ambient motion off or
in an occluded window the strip is still and as wide as the field. To
assistive technology the field is one ``search_box`` node (a host without a
mapping publishes a text box) whose value is the query, which carries the
editor's selection and text and is busy while `busy`; the editor is not
published separately.

Examples
--------
A field that reports its queries:

.. code-block:: cpp

   std::shared_ptr<gf::SearchField> search = gf::make_control<gf::SearchField>(gf::StableId("mail.search"));
   (*search).placeholder_text = std::string{"Search mail"};
   (*search).search_delay = std::chrono::milliseconds(0);
   (*search).text = std::string{"invoice"};
   // outside a window, search_requested has been raised with "invoice"
