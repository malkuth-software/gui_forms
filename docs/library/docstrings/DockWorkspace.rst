A docking workspace: the layout, the panels and their factories, and which panel and document are active.

A workspace owns a `DockLayout` and, for every panel the program registers,
its title and a factory that builds its content the first time the panel is
shown. It keeps two activation tracks: the active panel (the panel that has,
or last had, keyboard focus, tool or document) and the active document (the
document last active), each with a most-recently-active history. A
`DockSite` shows the main site in a window; once `attach_windows` binds the
main window, the workspace opens one owned floating window per floating
site. Use it for an application with tool panels around a document area.

Parameters
----------
stable_id
    The workspace's identity, used in diagnostics.

Properties
----------
layout
    The layout. Restrictions, dock groups, the workspace lock, saving and
    restoring go through it directly; register panels through
    `register_panel` so that their titles and factories are known.
created_panel_count
    The number of factory runs that produced content.
active_panel : default none
    The active panel.
active_document : default none
    The active document. Activating a tool panel leaves it unchanged.
tool_history
    The placed tool panels that have been active, most recent first.
document_history
    The placed documents that have been active, most recent first.
site : default none
    The `DockSite` showing the main site, or none.
windows_attached : default ``false``
    Whether a main window is attached, so floating sites have windows.
main_window : default inactive
    The attached main window's handle.
floating_window_count : default ``0``
    The live floating windows.
tree_generation : default ``0``
    The generation of the last layout change that touched a site's tree; a
    change that only moved floating windows does not count.
flyout_opening : default ``DockFlyoutOpening::hover_or_click``
    What opens an auto-hidden panel's flyout from its side tab (owner
    decision DK-27, E1-1): resting the pointer on the tab or clicking it, or
    (``click``) only clicking it.
close_prompt : default ``DockClosePrompt::each``
    How the program is asked before documents close at the user's request
    (owner decision DK-31): one ``document_closing`` event per document, in
    order, or (``combined``) one event for all of them.
closable_documents
    The placed documents that may close, the main site's in tree and tab
    order, then each floating site's: the documents Close All Documents
    asks about.
named_layouts : default empty
    The layouts saved by name (`DockNamedLayout`: a name and the layout's
    XML), in the order they were first saved. The program keeps the list
    and saves it with its settings; `set_named_layouts` puts it back,
    throwing `std::invalid_argument` and changing nothing for an empty,
    invalid or repeated name (the XML is read only when a layout is
    applied).
save_layout
    The layout as XML (`DockLayout::write_xml`), on the program's request;
    the workspace never saves on its own (owner decision DK-33).
close_pending : default ``false``
    Whether a close request waits for `answer_close`.
pending_close : default empty
    The documents the waiting prompt asked about; valid until the next
    request, answer or layout change.

Events
------
activation_changed
    Raised after a track changed, with the track, the old and new panel and
    the cause; when one activation changes both, the panel track is raised
    first.
document_closing
    Raised before documents close at the user's request, with the documents
    asked about and the cause (one document, Close All, Close Others, or a
    floating window's close button). A handler sets ``cancel`` to keep
    them, which ends the request with nothing closed, or ``defer`` to answer
    later with `answer_close`. A handler may change the layout itself; a
    request made from inside it is ``busy``.

Methods
-------
load_layout
    Replaces the layout with a saved one on the live windows, as one
    layout change: the floating windows the saved layout holds open and
    those it drops close in one window transaction, and only the panels
    then shown selected are created. A saved panel not registered yet is
    kept as a placeholder, and appears there once it registers and is
    shown. Without monitors in the options, the attached run's are used.
    A close request waiting for `answer_close` ends, closing nothing. An
    unreadable or rejected layout changes nothing (``kept_current``).
find_named_layout
    The named layout, or null.
save_named_layout
    Saves the current layout under a name, replacing a layout of that name
    in its place; false when the layout cannot be written. An empty name,
    or one that is not valid UTF-8, throws `std::invalid_argument`.
apply_named_layout
    Loads a named layout as `load_layout` does; ``kept_current`` with a
    ``not_a_layout`` diagnostic for a name not saved.
remove_named_layout
    Removes a named layout; false when there is none.
register_panel
    Registers a panel's descriptor with the layout and keeps its title and
    factory; registering places nothing. False, changing nothing, for an
    empty or repeated identity.
find_registration
    The registration of a panel, or null.
set_panel_title
    Retitles a panel's caption, tab and navigator row; false for an
    unregistered panel.
panel_title
    The registered title, or the identity when there is none.
apply
    Applies a transaction to the layout, as `DockLayout::apply` does.
show_panel
    Shows a hidden or new panel where it was last and activates it; a
    placed panel is only activated.
hide_panel
    Hides a panel, keeping its placeholder and its content.
close_panel
    Closes a panel and releases its content. A closed tool panel keeps its
    placeholder and reopens where it was; a closed document leaves. It
    asks nobody: the program's own close.
request_close
    The user's close of one panel: a document is asked about first
    (``document_closing``) and closes once allowed; a tool panel closes at
    once. Returns a `DockCloseOutcome`: ``closed``, ``cancelled``,
    ``pending`` (a handler deferred), ``busy`` (another request waits) or
    ``refused`` (not placed, may not close, or the layout refused).
close_all_documents
    Close All Documents: every placed document that may close, the main
    site's in tree and tab order, then each floating site's.
close_other_documents
    Close Other Documents: as `close_all_documents`, keeping the given one.
answer_close
    Answers the waiting prompt: ``true`` goes on with the request (the next
    document is asked about, or the documents close), ``false`` cancels it.
    Refused when no request waits.
panel_content
    The panel's content, or null before its factory ran.
create_panel_content
    Runs the panel's factory if it has no content yet and returns the
    content.
activate_panel
    Makes a placed panel active: selects it in its stack, creates its
    content, moves focus into it through the site showing it (main or
    floating) and updates the tracks. A panel behind a side tab
    (auto-hidden, or in a stack collapsed by shrinking) opens in its flyout
    with focus inside; an auto-hidden stack's selection is not edited.
    False, changing nothing, for a panel not placed, or floating in a site
    no `DockSite` shows.
attach_windows
    Binds the run's main window (its handle and its
    `ApplicationWindow::stable_id`) and opens a floating window for every
    floating site. The main `DockSite` must be attached to that window.
    False for an inactive handle, an empty identity, a host without runtime
    windows, or windows already attached.
detach_windows
    Destroys the floating windows (their sites stay in the layout) and
    unbinds the main window.
floating_window
    The window showing a floating site, or an inactive handle.
floating_site
    The `DockSite` showing a floating site, or null.
can_float
    Whether a subject may float now: windows attached, room for another
    window, and the layout accepts it (the restrictions, the lock and the
    limit of 32 floating sites).
float_subject
    Floats a subject into a new floating window with the given client area
    in desktop units, recording the monitor it is on.
dock_back_edit
    The edit that docks a subject back: into the stack its panel was docked
    in last, else beside the neighbour remembered there, else a document
    into the document area or a tool panel along its default edge; the
    first the layout accepts. None when no edit is accepted.
dock_back
    Applies `dock_back_edit` and activates the panel.

See Also
--------
DockSite : Shows the workspace in a window.
DockLayout : The layout model the workspace owns.
DockNavigator : The Ctrl+Tab switcher over the two histories.

Notes
-----
Lazy creation (owner decision DK-34): a factory runs the first time its
panel is shown selected in a stack the site shows or in a flyout. A panel
behind another tab, hidden, behind a side tab whose flyout has not opened,
or in a stack hidden by the maximized document area is not created. Hiding
keeps the content; closing releases it,
and a closed panel shown again is built again by its factory.

Command routing (owner decision DK-29): the workspace names, on every
site's window, the command target that the active panel's content control
carries (`Control::set_command_target`) for the active-panel stage, and the
active document's for the active-document stage; floating windows also take
the main window's main-window target. A document command such as Save bound
as a shortcut therefore reaches the active document while focus is in a
tool panel, floating or not: keys a floating window does not use go to the
main window's accelerators (`Window::offer_accelerators`).

Floating windows (phase D4; decisions DK-1, DK-6 to DK-12, DK-44): each
layout change is carried out as one `WorkspaceTransaction` across the
windows. The sites realize it with every window's layout held; a new
floating site gets a window opened hidden, owned by the main window, whose
root is a `DockSite` for the site and which shares the main window's
resources and key bindings; at the commit every window lays out once, the
new windows appear, and only then are the windows of sites that left
destroyed, so a panel moving between windows is never invisible. A window
holding a document is a floating document window, which stays visible while
the application is inactive; any other is a floating tool window, which
hides then. Its client area is the site's frame, placed on a monitor
(`place_on_monitor`), wholly inside the work area when the fallback moved
it and it fits. A move or resize is written back as ``set_floating_frame``
with the monitor it is on (a change that realizes nothing); a frame the
program sets moves the window. The close button asks about the window's
documents (cause ``floating_window``); once they are allowed it hides the
window's tool panels and closes its documents, which closes it. A
cancelled or deferred prompt keeps the window open.

Closing documents (phase D6; owner decision DK-31): a close at the user's
request (a document tab's close, the position menu's Close, a floating
window's close button, or the program's `request_close`,
`close_all_documents` and `close_other_documents`) asks before anything
closes. The prompts run in the request's order; a document that left the
layout before its turn is not asked about. A cancel at any prompt, or a
``false`` answer, ends the request and closes nothing, including the
documents already allowed: the documents close together, in one user
transaction, only after the last prompt allowed them.

After every layout change the workspace drops panels that left the layout
from its histories, releases the content of a document that left, and
replaces an active document that left with the next one in its history
(cause ``fallback``) and an active panel that left with the active
document. The sites realize the change after that, the main site first. A
factory is a named delegate; its object must outlive the workspace.

Examples
--------
Two documents and a tool panel whose controls are made only when shown:

.. code-block:: cpp

   struct Views final {
       int made = 0;
       void make(gf::DockPanelCreation& creation) {
           ++made;
           creation.content = gf::make_control<gf::TextBox>(gf::StableId("view." + creation.panel.value));
       }
   };

   void example() {
       Views views;
       gf::DockWorkspace workspace("editor");
       const gf::Delegate<gf::DockPanelCreation&> factory =
           gf::Delegate<gf::DockPanelCreation&>::bind<Views, &Views::make>(views);
       gf::DockPanelRegistration outline{};
       outline.descriptor.id = gf::DockPanelId("outline");
       outline.descriptor.default_placement.side = gf::DockSide::left;
       outline.title = "Outline";
       outline.factory = factory;
       static_cast<void>(workspace.register_panel(outline));
       const std::array<std::string_view, 2> files{"a.txt", "b.txt"};
       for (const std::string_view file : files) {
           gf::DockPanelRegistration document{};
           document.descriptor.id = gf::DockPanelId(std::string(file));
           document.descriptor.kind = gf::DockPanelKind::document;
           document.title = std::string(file);
           document.factory = factory;
           static_cast<void>(workspace.register_panel(document));
           static_cast<void>(workspace.show_panel(document.descriptor.id));
       }
       static_cast<void>(workspace.show_panel(gf::DockPanelId("outline")));
       // views.made is 0: nothing is shown until a site lays the workspace out.
       // The active panel is the outline and the active document b.txt.
   }
