A drop-down field whose popup is a control the application gives it.

The field looks like a drop-down-list ComboBox: the text the application
writes, and a drop-down button. Its popup shows `popup_content`, any control
(a calendar, a tree of folders, a small form), in an anchored overlay below
the field, or above it when there is no room below. Windows Forms has no
counterpart.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
text : default empty
    The text the field shows, which the application writes, usually from
    the content when the popup closes. Invalid UTF-8 throws
    `std::invalid_argument`.
popup_content
    The control the popup shows, or none; `set_popup_content` sets it and
    throws `std::logic_error` while the popup is open and
    `std::invalid_argument` for a control with a parent or in a window.
popup_size
    The popup's size; zero on an axis takes the content's measured size
    there, and the popup is at least as wide as the field.
    `set_popup_size` throws `std::invalid_argument` for a negative or
    non-finite size, or one past `maximum_popup_extent`.
popup_shown
    Whether the popup is open.

Events
------
text_changed
    Raised after the text changes, with the new text.
popup_opened
    Raised after the popup opens.
popup_closed
    Raised after the popup closes, with how (`PopupClosed`, whose reason is
    a `PopupCloseReason`): accepted or cancelled by `close_popup`, or
    cancelled by a click away or Escape.

Methods
-------
show_popup
    Opens the popup; the content takes the keyboard focus when it can.
    Returns false when the popup is open, the field is not in a window or
    not enabled, or there is no content.
close_popup
    Closes the open popup and raises `popup_closed` with the reason given.
    Returns false when the popup is not open.

See Also
--------
ComboBox : A drop-down of items.
LookupComboBox : A drop-down of rows in columns.
AnchoredOverlayLayer : The overlay the popup is shown in.

Notes
-----
A click on the field, F4, Alt+Down or Alt+Up, or the semantic expand opens
the popup; the same with the popup open cancels it. Focus inside the popup
returns to the field when it closes. The content is held while it is set
and lent to the overlay while the popup is open, so it keeps its state
between openings; the window closing the overlay itself cancels the popup.
The well takes the theme's ``editor`` family and the drop-down button the
``choice`` family; the field is ``open`` while the popup is. To assistive
technology the field is a read-only ``combo_box`` node with a pop-up, its
value the text, expanded while open, controlling the content.

Examples
--------
A field whose popup is a calendar:

.. code-block:: cpp

   std::shared_ptr<gf::PopupContainerBox> due = gf::make_control<gf::PopupContainerBox>(gf::StableId("task.due"));
   std::shared_ptr<gf::MonthCalendar> calendar = gf::make_control<gf::MonthCalendar>(gf::StableId("task.due.calendar"));
   (*due).set_popup_content(calendar);
   (*due).text = std::string{"Choose a date"};
   const bool opened = (*due).show_popup();
   // opened is false outside a window
