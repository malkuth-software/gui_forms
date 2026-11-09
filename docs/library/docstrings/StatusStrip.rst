A status bar laid out as a strip of labels along the bottom of a window.

A status strip is a `ToolStrip` laid out for status: its items never overflow,
labels marked `spring` share the width the other items leave, and a sizing
grip shows in its corner (ToolStrip's `sizing_grip_visible`, on for a status
strip). Use spring labels for the message that should take
the free space, and plain labels for fixed fields such as a cursor position or
an encoding.

Parameters
----------
stable_id
    Identity of the strip, unique within its window.

See Also
--------
ToolStrip : The strip a status strip is laid out from.

Notes
-----
Everything else (items, clicks, menus, keyboard, the ``item`` item system)
is `ToolStrip`'s. A status strip has no grip at its start and no overflow
button; items that do not fit are cut off. Spring labels share the free
width equally; with none, the items keep their own widths from the start.

The status strip publishes ``region``, ``face``, ``sizing_grip`` and
``focus``. The sizing grip is drawn and published but does not resize the
window yet, because a control has no native window request; an application
can call `ApplicationWindowHandle::request_client_size`.

The strip is published to accessibility with the role `status_bar` (named
"Status" unless it has an accessible name), with a child per item.

Examples
--------
A message that takes the free width, then two fixed fields:

.. code-block:: cpp

   std::shared_ptr<gf::StatusStrip> status =
       gf::make_control<gf::StatusStrip>(gf::StableId("editor.status"));
   (*status).set_items({
       {.stable_id = "status.message", .kind = gf::ToolStripItemKind::label, .text = "Ready", .spring = true},
       {.stable_id = "status.position", .kind = gf::ToolStripItemKind::label, .text = "Ln 1, Col 1"},
       {.stable_id = "status.encoding", .kind = gf::ToolStripItemKind::label, .text = "UTF-8"},
   });
