/* Kit part: a dock workspace's panels and layout (GF_TABLE_DOCKING, since
 * 1.1): registering panels with content controls, the program's edits and
 * their refusals, where a panel is (with the side of the document area its
 * stack stands on), saving and restoring the layout, the stacks' panels,
 * and the workspace's layout_changed interaction. No window runs, so
 * attach_windows is refused and floating needs no window. */

#include "gf_conformance_internal.h"

#include <string.h>

static gf_dock_panel panel_record(const char* id, const char* title, gf_dock_panel_kind kind, gf_object content) {
    gf_dock_panel made;
    memset(&made, 0, sizeof(made));
    made.struct_size = (uint32_t)sizeof(made);
    made.kind = kind;
    made.allowed = GF_DOCK_ALLOWED_ALL;
    made.default_placement = GF_DOCK_PLACEMENT_DOCKED;
    made.default_side = GF_DOCK_SIDE_LEFT;
    made.default_ratio = 0.25;
    made.default_extent = 200.0;
    made.default_frame.x = 100.0;
    made.default_frame.y = 100.0;
    made.default_frame.width = 320.0;
    made.default_frame.height = 240.0;
    made.id = gf_kit_text(id);
    made.title = gf_kit_text(title);
    made.content = content;
    return made;
}

static gf_dock_edit edit_record(gf_dock_edit_kind kind, const char* panel) {
    gf_dock_edit made;
    memset(&made, 0, sizeof(made));
    made.struct_size = (uint32_t)sizeof(made);
    made.kind = kind;
    made.panel = gf_kit_text(panel);
    made.ratio = 0.3;
    made.extent = 180.0;
    made.index = 0xffffffffu;
    return made;
}

static gf_dock_location located(gf_kit* kit, gf_object workspace, const char* panel) {
    gf_dock_location location;
    memset(&location, 0, sizeof(location));
    location.struct_size = (uint32_t)sizeof(location);
    gf_kit_status(kit, (*kit).docking.locate(workspace, gf_kit_text(panel), &location), GF_STATUS_OK,
                  "locate a panel");
    return location;
}

static gf_status apply(gf_kit* kit, gf_object workspace, const gf_dock_edit* edit, gf_dock_refusal* refusal) {
    return (*kit).docking.apply_edit(workspace, edit, refusal);
}

static void registering(gf_kit* kit, gf_object workspace, gf_object first, gf_object second) {
    gf_dock_panel radio = panel_record("radio", "Radio", GF_DOCK_PANEL_KIND_TOOL, first);
    gf_dock_panel audio = panel_record("audio", "Audio", GF_DOCK_PANEL_KIND_TOOL, second);
    gf_dock_panel source = panel_record("source", "Source", GF_DOCK_PANEL_KIND_TOOL, 0u);
    source.default_side = GF_DOCK_SIDE_RIGHT;
    gf_dock_panel spectrum = panel_record("spectrum", "Spectrum", GF_DOCK_PANEL_KIND_DOCUMENT, 0u);
    gf_kit_status(kit, (*kit).docking.register_panel(workspace, &radio), GF_STATUS_OK, "register a tool panel");
    gf_kit_status(kit, (*kit).docking.register_panel(workspace, &audio), GF_STATUS_OK, "register another");
    gf_kit_status(kit, (*kit).docking.register_panel(workspace, &source), GF_STATUS_OK,
                  "register a panel without content");
    gf_kit_status(kit, (*kit).docking.register_panel(workspace, &spectrum), GF_STATUS_OK, "register a document");
    gf_kit_status(kit, (*kit).docking.register_panel(workspace, &radio), GF_STATUS_INVALID_ARGUMENT,
                  "a repeated id is refused");
    gf_dock_panel empty = panel_record("", "Empty", GF_DOCK_PANEL_KIND_TOOL, 0u);
    gf_kit_status(kit, (*kit).docking.register_panel(workspace, &empty), GF_STATUS_INVALID_ARGUMENT,
                  "an empty id is refused");
    gf_dock_panel spelled = panel_record("radioToolWindow", "Radio", GF_DOCK_PANEL_KIND_TOOL, 0u);
    gf_kit_status(kit, (*kit).docking.register_panel(workspace, &spelled), GF_STATUS_INVALID_ARGUMENT,
                  "an id that is not a stable id is refused");
    gf_dock_panel wrong = panel_record("wrong", "Wrong", GF_DOCK_PANEL_KIND_TOOL, 0u);
    wrong.default_ratio = 1.5;
    gf_kit_status(kit, (*kit).docking.register_panel(workspace, &wrong), GF_STATUS_INVALID_ARGUMENT,
                  "a ratio outside (0, 1) is refused");
    wrong.default_ratio = 0.25;
    wrong.allowed = 0x1000u;
    gf_kit_status(kit, (*kit).docking.register_panel(workspace, &wrong), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown allowed flag is refused");
    gf_kit_status(kit, (*kit).docking.register_panel(workspace, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "a missing record is refused");
    gf_kit_status(kit, (*kit).docking.set_panel_title(workspace, gf_kit_text("radio"), gf_kit_text("Radio!")),
                  GF_STATUS_OK, "retitle a panel");
    gf_kit_status(kit, (*kit).docking.set_panel_title(workspace, gf_kit_text("nobody"), gf_kit_text("X")),
                  GF_STATUS_NOT_FOUND, "an unregistered panel has no title");
    gf_kit_status(kit, (*kit).docking.set_panel_content(workspace, gf_kit_text("source"), second),
                  GF_STATUS_OK, "give a panel content before it shows");
    gf_kit_status(kit, (*kit).docking.set_panel_content(workspace, gf_kit_text("source"), 0u), GF_STATUS_OK,
                  "take it back");
}

static void editing(gf_kit* kit, gf_object workspace) {
    gf_dock_refusal refusal = 99u;
    gf_dock_edit show = edit_record(GF_DOCK_EDIT_SHOW, "radio");
    gf_kit_status(kit, apply(kit, workspace, &show, &refusal), GF_STATUS_OK, "show a tool panel");
    gf_kit_check(kit, refusal == GF_DOCK_REFUSAL_NONE, "an applied edit writes no refusal");
    gf_dock_location radio = located(kit, workspace, "radio");
    gf_kit_check(kit, radio.placement == GF_DOCK_PLACEMENT_DOCKED, "the panel is docked");
    gf_kit_check(kit, radio.side == GF_DOCK_SIDE_LEFT, "at its default side of the document area");
    gf_kit_check(kit, radio.stack_kind == GF_DOCK_NODE_KIND_TOOL_STACK && radio.selected == 1u,
                 "selected in a tool stack");

    refusal = 99u;
    gf_kit_status(kit, apply(kit, workspace, &show, &refusal), GF_STATUS_OK,
                  "showing a shown panel activates it");
    gf_kit_check(kit, refusal == GF_DOCK_REFUSAL_NONE, "and writes no refusal");

    gf_dock_edit into = edit_record(GF_DOCK_EDIT_DOCK_INTO, "audio");
    into.target_node = radio.stack;
    gf_kit_status(kit, apply(kit, workspace, &into, NULL), GF_STATUS_OK, "tab a panel into a stack");
    gf_dock_location audio = located(kit, workspace, "audio");
    gf_kit_check(kit, audio.stack == radio.stack && audio.stack_panel_count == 2u, "two panels share the stack");

    char text[32];
    uint64_t required = 0u;
    gf_kit_status(kit, (*kit).docking.panel_at(workspace, radio.stack, 1u, text, sizeof(text), &required),
                  GF_STATUS_OK, "read a stack's second panel");
    gf_kit_check(kit, required == 5u && memcmp(text, "audio", 5u) == 0, "the tabbed panel is second");
    gf_kit_status(kit, (*kit).docking.panel_at(workspace, radio.stack, 2u, text, sizeof(text), &required),
                  GF_STATUS_NOT_FOUND, "no panel past the stack's end");

    gf_dock_edit move = edit_record(GF_DOCK_EDIT_MOVE, "audio");
    move.index = 0u;
    gf_kit_status(kit, apply(kit, workspace, &move, NULL), GF_STATUS_OK, "move a tab to the front");
    gf_dock_edit select = edit_record(GF_DOCK_EDIT_SELECT, "radio");
    gf_kit_status(kit, apply(kit, workspace, &select, NULL), GF_STATUS_OK, "select a tab");

    gf_dock_edit edge = edit_record(GF_DOCK_EDIT_SHOW, "source");
    gf_kit_status(kit, apply(kit, workspace, &edge, NULL), GF_STATUS_OK, "show a panel at its default edge");
    gf_dock_location source = located(kit, workspace, "source");
    gf_kit_check(kit, source.side == GF_DOCK_SIDE_RIGHT, "on the right of the document area");

    gf_dock_edit document = edit_record(GF_DOCK_EDIT_SHOW, "spectrum");
    gf_kit_status(kit, apply(kit, workspace, &document, NULL), GF_STATUS_OK, "show a document");
    gf_dock_location spectrum = located(kit, workspace, "spectrum");
    gf_kit_check(kit, spectrum.stack_kind == GF_DOCK_NODE_KIND_DOCUMENT_STACK && spectrum.side == GF_DOCK_SIDE_NONE,
                 "a document stands in the document area");
    uint32_t area = 0u;
    gf_kit_status(kit, (*kit).docking.document_area(workspace, &area), GF_STATUS_OK, "read the document area");
    gf_kit_check(kit, area != 0u, "the main site has a document area");

    gf_dock_edit pin = edit_record(GF_DOCK_EDIT_AUTO_HIDE, "source");
    pin.side = GF_DOCK_SIDE_RIGHT;
    gf_kit_status(kit, apply(kit, workspace, &pin, NULL), GF_STATUS_OK, "pin a panel out to an edge");
    source = located(kit, workspace, "source");
    gf_kit_check(kit, source.placement == GF_DOCK_PLACEMENT_AUTO_HIDDEN && source.side == GF_DOCK_SIDE_RIGHT,
                 "auto-hidden on the right edge");
    gf_dock_edit unpin = edit_record(GF_DOCK_EDIT_UNPIN, "");
    unpin.subject_stack = source.stack;
    gf_kit_status(kit, apply(kit, workspace, &unpin, NULL), GF_STATUS_OK, "unpin it");
    source = located(kit, workspace, "source");
    gf_kit_check(kit, source.placement == GF_DOCK_PLACEMENT_DOCKED, "docked again");

    gf_dock_edit beside = edit_record(GF_DOCK_EDIT_DOCK_BESIDE, "source");
    beside.target_node = radio.stack;
    beside.side = GF_DOCK_SIDE_BOTTOM;
    beside.ratio = 0.4;
    gf_kit_status(kit, apply(kit, workspace, &beside, NULL), GF_STATUS_OK, "dock beside a stack");
    source = located(kit, workspace, "source");
    gf_kit_check(kit, source.side == GF_DOCK_SIDE_LEFT, "it now stands on the left with the stack");

    gf_dock_edit at_edge = edit_record(GF_DOCK_EDIT_DOCK_AT_EDGE, "source");
    at_edge.side = GF_DOCK_SIDE_TOP;
    gf_kit_status(kit, apply(kit, workspace, &at_edge, NULL), GF_STATUS_OK, "dock at the top edge");
    source = located(kit, workspace, "source");
    gf_kit_check(kit, source.side == GF_DOCK_SIDE_TOP, "above the document area");

    gf_dock_edit ratio = edit_record(GF_DOCK_EDIT_SET_RATIO, "");
    ratio.target_node = radio.stack;
    gf_kit_status(kit, apply(kit, workspace, &ratio, &refusal), GF_STATUS_INVALID_STATE,
                  "a stack has no ratio to set");
    gf_kit_check(kit, refusal == GF_DOCK_REFUSAL_NOT_A_SPLIT, "a stack is not a split");

    gf_dock_edit floating = edit_record(GF_DOCK_EDIT_FLOAT, "audio");
    floating.frame.x = 40.0;
    floating.frame.y = 40.0;
    floating.frame.width = 300.0;
    floating.frame.height = 200.0;
    gf_kit_status(kit, apply(kit, workspace, &floating, NULL), GF_STATUS_OK, "float a panel");
    gf_dock_location audio_floating = located(kit, workspace, "audio");
    gf_kit_check(kit, audio_floating.placement == GF_DOCK_PLACEMENT_FLOATING && audio_floating.site != 0u,
                 "it floats in a floating site");
    uint32_t sites[4] = {0u, 0u, 0u, 0u};
    uint64_t site_count = 0u;
    gf_kit_status(kit, (*kit).docking.floating_sites(workspace, sites, 4u, &site_count), GF_STATUS_OK,
                  "list the floating sites");
    gf_kit_check(kit, site_count == 1u && sites[0] == audio_floating.site, "one floating site");
    gf_kit_status(kit, (*kit).docking.floating_sites(workspace, NULL, 0u, &site_count),
                  GF_STATUS_BUFFER_TOO_SMALL, "an empty array is too small");
    gf_logical_rect frame;
    memset(&frame, 0, sizeof(frame));
    uint32_t first_stack = 0u;
    gf_kit_status(kit, (*kit).docking.floating_site(workspace, sites[0], &frame, &first_stack), GF_STATUS_OK,
                  "read the floating site");
    gf_kit_check(kit, frame.width == 300.0 && first_stack == audio_floating.stack, "its frame and stack");
    gf_kit_status(kit, (*kit).docking.floating_site(workspace, 999999u, &frame, &first_stack),
                  GF_STATUS_NOT_FOUND, "an unknown floating site");
    gf_dock_edit moved = edit_record(GF_DOCK_EDIT_SET_FLOATING_FRAME, "");
    moved.site = sites[0];
    moved.frame = frame;
    moved.frame.x = 60.0;
    gf_kit_status(kit, apply(kit, workspace, &moved, NULL), GF_STATUS_OK, "move the floating site");

    gf_dock_edit hide = edit_record(GF_DOCK_EDIT_HIDE, "radio");
    gf_kit_status(kit, apply(kit, workspace, &hide, NULL), GF_STATUS_OK, "hide a panel");
    radio = located(kit, workspace, "radio");
    gf_kit_check(kit, radio.placement == GF_DOCK_PLACEMENT_HIDDEN, "the panel is hidden");
    gf_kit_status(kit, apply(kit, workspace, &show, NULL), GF_STATUS_OK, "show it again");
    const gf_status activated = (*kit).docking.activate_panel(workspace, gf_kit_text("radio"));
    gf_kit_check(kit, activated == GF_STATUS_OK || activated == GF_STATUS_INVALID_STATE,
                 "activate a placed panel (refused only while no site realizes it)");
    gf_kit_status(kit, (*kit).docking.activate_panel(workspace, gf_kit_text("nobody")), GF_STATUS_NOT_FOUND,
                  "an unregistered panel does not activate");

    gf_dock_edit unknown = edit_record(GF_DOCK_EDIT_SHOW, "nobody");
    gf_kit_status(kit, apply(kit, workspace, &unknown, NULL), GF_STATUS_NOT_FOUND, "an unregistered panel");
    gf_dock_edit bad = edit_record(31u, "radio");
    gf_kit_status(kit, apply(kit, workspace, &bad, NULL), GF_STATUS_INVALID_ARGUMENT, "an unknown edit kind");
}

static void persistence(gf_kit* kit, gf_object workspace) {
    uint64_t required = 0u;
    gf_kit_status(kit, (*kit).docking.save_layout(workspace, NULL, 0u, &required), GF_STATUS_BUFFER_TOO_SMALL,
                  "measure the saved layout");
    gf_kit_check(kit, required > 0u && required < 65536u, "the layout has a size");
    char saved[65536];
    gf_kit_status(kit, (*kit).docking.save_layout(workspace, saved, sizeof(saved), &required), GF_STATUS_OK,
                  "save the layout");
    gf_dock_edit close = edit_record(GF_DOCK_EDIT_CLOSE, "radio");
    gf_kit_status(kit, apply(kit, workspace, &close, NULL), GF_STATUS_OK, "close a panel");
    gf_dock_location radio = located(kit, workspace, "radio");
    gf_kit_check(kit, radio.placement != GF_DOCK_PLACEMENT_DOCKED, "the closed panel is not docked");
    gf_utf8_view xml;
    xml.data = saved;
    xml.size = required;
    uint64_t diagnostics = 99u;
    gf_kit_status(kit, (*kit).docking.load_layout(workspace, xml, &diagnostics), GF_STATUS_OK, "restore the layout");
    radio = located(kit, workspace, "radio");
    gf_kit_check(kit, radio.placement == GF_DOCK_PLACEMENT_DOCKED, "the panel is docked again");
    gf_kit_status(kit, (*kit).docking.load_layout(workspace, gf_kit_text("<not a layout"), NULL),
                  GF_STATUS_INVALID_ARGUMENT, "malformed XML is refused");
    radio = located(kit, workspace, "radio");
    gf_kit_check(kit, radio.placement == GF_DOCK_PLACEMENT_DOCKED, "and leaves the layout");
}

// Side restrictions, the close button and the workspace's options.
static void restricting(gf_kit* kit, gf_object workspace) {
    gf_dock_panel left_only = panel_record("left-only", "Left only", GF_DOCK_PANEL_KIND_TOOL, 0u);
    left_only.allowed = GF_DOCK_ALLOWED_ALL & ~(GF_DOCK_ALLOWED_SIDE_TOP | GF_DOCK_ALLOWED_SIDE_RIGHT |
                                                GF_DOCK_ALLOWED_SIDE_BOTTOM);
    left_only.allowed |= GF_DOCK_ALLOWED_CLOSE_BUTTON_HIDDEN;
    gf_kit_status(kit, (*kit).docking.register_panel(workspace, &left_only), GF_STATUS_OK,
                  "register a panel restricted to the left with no close button");
    gf_dock_edit right = edit_record(GF_DOCK_EDIT_DOCK_AT_EDGE, "left-only");
    right.side = GF_DOCK_SIDE_RIGHT;
    gf_dock_refusal refusal = GF_DOCK_REFUSAL_NONE;
    gf_kit_status(kit, apply(kit, workspace, &right, &refusal), GF_STATUS_INVALID_STATE,
                  "docking it on a side it does not allow is refused");
    gf_kit_check(kit, refusal == GF_DOCK_REFUSAL_PLACEMENT_NOT_ALLOWED, "as a placement not allowed");
    gf_dock_edit left = edit_record(GF_DOCK_EDIT_DOCK_AT_EDGE, "left-only");
    left.side = GF_DOCK_SIDE_LEFT;
    gf_kit_status(kit, apply(kit, workspace, &left, NULL), GF_STATUS_OK, "docking it on the left is applied");
    gf_dock_location placed = located(kit, workspace, "left-only");
    gf_kit_check(kit, placed.side == GF_DOCK_SIDE_LEFT, "it stands left of the document area");
    gf_dock_edit pin = edit_record(GF_DOCK_EDIT_AUTO_HIDE, "left-only");
    pin.side = GF_DOCK_SIDE_BOTTOM;
    gf_kit_status(kit, apply(kit, workspace, &pin, NULL), GF_STATUS_INVALID_STATE,
                  "auto-hiding it to an edge it does not allow is refused");

    left_only.allowed = GF_DOCK_ALLOWED_ALL;
    gf_kit_status(kit, (*kit).docking.update_panel(workspace, &left_only), GF_STATUS_OK,
                  "update the panel to allow every side");
    gf_kit_status(kit, apply(kit, workspace, &right, NULL), GF_STATUS_OK, "now it docks on the right");
    placed = located(kit, workspace, "left-only");
    gf_kit_check(kit, placed.side == GF_DOCK_SIDE_RIGHT, "and stands right of the document area");
    gf_dock_panel stranger = panel_record("stranger", "Stranger", GF_DOCK_PANEL_KIND_TOOL, 0u);
    gf_kit_status(kit, (*kit).docking.update_panel(workspace, &stranger), GF_STATUS_NOT_FOUND,
                  "updating an unregistered panel is refused");
    left_only.kind = GF_DOCK_PANEL_KIND_DOCUMENT;
    gf_kit_status(kit, (*kit).docking.update_panel(workspace, &left_only), GF_STATUS_INVALID_ARGUMENT,
                  "an update keeps the panel's kind");
    gf_dock_edit close = edit_record(GF_DOCK_EDIT_CLOSE, "left-only");
    gf_kit_status(kit, apply(kit, workspace, &close, NULL), GF_STATUS_OK, "close it again");

    gf_dock_options options;
    memset(&options, 0, sizeof(options));
    options.struct_size = (uint32_t)sizeof(options);
    options.flags = GF_DOCK_OPTION_FLOATS_SHOWN_WHILE_INACTIVE | GF_DOCK_OPTION_DOCUMENT_TABS_HIDDEN |
                    GF_DOCK_OPTION_AUTO_HIDE_NO_HOVER | GF_DOCK_OPTION_AREA_BACKGROUND;
    options.area_background = 0x202020ffu;
    gf_kit_status(kit, (*kit).docking.set_options(workspace, &options), GF_STATUS_OK, "set every option");
    options.flags = 0u;
    gf_kit_status(kit, (*kit).docking.set_options(workspace, &options), GF_STATUS_OK, "clear them");
    options.flags = 0x100u;
    gf_kit_status(kit, (*kit).docking.set_options(workspace, &options), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown option is refused");
    gf_kit_status(kit, (*kit).docking.set_options(workspace, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "a missing options record is refused");
    gf_kit_status(kit, (*kit).docking.set_floating_window_visibility(workspace, 4096u, GF_DOCK_WINDOW_VISIBILITY_SHOWN),
                  GF_STATUS_NOT_FOUND, "an unknown floating site has no window option");
    gf_kit_status(kit, (*kit).docking.set_floating_window_visibility(workspace, 1u, 7u), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown visibility is refused");
}

void gf_kit_docking(gf_kit* kit) {
    const gf_object workspace = gf_kit_create(kit, "dock_workspace", "kit.dock");
    gf_create_arguments arguments;
    memset(&arguments, 0, sizeof(arguments));
    arguments.struct_size = (uint32_t)sizeof(arguments);
    arguments.owner = workspace;
    gf_object site = 0u;
    gf_kit_status(kit, (*kit).objects.create_with(gf_kit_text("dock_site"), gf_kit_text("kit.dock.site"), &arguments,
                                                  &site),
                  GF_STATUS_OK, "make the workspace's site");
    const gf_object first = gf_kit_create(kit, "label", "kit.dock.first");
    const gf_object second = gf_kit_create(kit, "label", "kit.dock.second");
    uint32_t area = 0u;
    gf_kit_status(kit, (*kit).docking.document_area(first, &area), GF_STATUS_WRONG_HANDLE_KIND,
                  "a label is not a workspace");

    gf_kit_recorder changes;
    memset(&changes, 0, sizeof(changes));
    changes.kit = kit;
    gf_callback callback = gf_kit_recording_callback(&changes);
    gf_subscription subscription = 0u;
    gf_kit_status(kit, (*kit).events.subscribe(workspace, gf_kit_text("layout_changed"), &callback, &subscription),
                  GF_STATUS_OK, "subscribe to the layout's changes");

    registering(kit, workspace, first, second);
    editing(kit, workspace);
    persistence(kit, workspace);
    restricting(kit, workspace);
    gf_kit_check(kit, changes.invoked >= 10u, "every applied edit and the restore raised layout_changed");
    gf_kit_status(kit, (*kit).docking.attach_windows(workspace), GF_STATUS_INVALID_STATE,
                  "no window runs to attach");

    gf_kit_status(kit, (*kit).events.disconnect(subscription), GF_STATUS_OK, "disconnect");
    gf_kit_status(kit, (*kit).objects.release(site), GF_STATUS_OK, "release the site");
    gf_kit_status(kit, (*kit).objects.release(second), GF_STATUS_OK, "release a content control");
    gf_kit_status(kit, (*kit).objects.release(first), GF_STATUS_OK, "release the other");
    gf_kit_status(kit, (*kit).objects.release(workspace), GF_STATUS_OK, "release the workspace");
}
