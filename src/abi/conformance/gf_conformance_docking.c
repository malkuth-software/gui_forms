/* Kit part: a dock workspace's panels and layout (GF_TABLE_DOCKING, since
 * 1.1): registering panels with content controls, the program's edits and
 * their refusals, where a panel is (with the side of the document area its
 * stack stands on), saving and restoring the layout, the stacks' panels,
 * and the workspace's layout_changed interaction. No window runs, so
 * attach_windows is refused and floating needs no window. Since 1.6, the
 * frames presentation (its edits, the frame memory, the area's documents,
 * a panel's menu strip) and a window's MDI slots (windows table), on
 * windows made with windows.create that never run. */

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

static gf_dock_frame_memory frame_of(gf_kit* kit, gf_object workspace, const char* document) {
    gf_dock_frame_memory frame;
    memset(&frame, 0, sizeof(frame));
    frame.struct_size = (uint32_t)sizeof(frame);
    gf_kit_status(kit, (*kit).docking.frame_memory(workspace, gf_kit_text(document), &frame), GF_STATUS_OK,
                  "read a document's frame");
    return frame;
}

/* The frames presentation (since 1.6): the presentation edit and its read,
 * a document's frame bounds, state and icon slot, the area's documents and
 * its active one, the arrangements, and a panel's menu strip. The site is
 * never laid out, so arranging changes nothing. */
static void frames(gf_kit* kit, gf_object workspace) {
    gf_dock_document_presentation presentation = 99u;
    gf_kit_status(kit, (*kit).docking.document_presentation(workspace, &presentation), GF_STATUS_OK,
                  "read the presentation");
    gf_kit_check(kit, presentation == GF_DOCK_DOCUMENT_PRESENTATION_TABS, "documents show as tabs by default");
    gf_kit_status(kit, (*kit).docking.document_presentation(workspace, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "document_presentation needs an output");

    gf_dock_edit framed = edit_record(GF_DOCK_EDIT_SET_DOCUMENT_PRESENTATION, "");
    framed.presentation = GF_DOCK_DOCUMENT_PRESENTATION_FRAMES;
    gf_kit_status(kit, apply(kit, workspace, &framed, NULL), GF_STATUS_OK, "show the documents as frames");
    gf_kit_status(kit, (*kit).docking.document_presentation(workspace, &presentation), GF_STATUS_OK,
                  "read the presentation again");
    gf_kit_check(kit, presentation == GF_DOCK_DOCUMENT_PRESENTATION_FRAMES, "the area shows frames");
    gf_dock_edit short_record = framed;
    short_record.struct_size = 104u;
    gf_kit_status(kit, apply(kit, workspace, &short_record, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "a 1.1-sized record cannot carry a presentation");
    gf_dock_edit unknown = framed;
    unknown.presentation = 7u;
    gf_kit_status(kit, apply(kit, workspace, &unknown, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown presentation is refused");

    uint64_t count = 0u;
    gf_kit_status(kit, (*kit).docking.area_document_count(workspace, &count), GF_STATUS_OK,
                  "count the area's documents");
    gf_kit_check(kit, count == 1u, "the one document stands in the area");
    char text[32];
    uint64_t required = 0u;
    gf_kit_status(kit, (*kit).docking.area_document_at(workspace, 0u, text, sizeof(text), &required), GF_STATUS_OK,
                  "read the area's first document");
    gf_kit_check(kit, required == 8u && memcmp(text, "spectrum", 8u) == 0, "it is the spectrum document");
    gf_kit_status(kit, (*kit).docking.area_document_at(workspace, 1u, text, sizeof(text), &required),
                  GF_STATUS_NOT_FOUND, "no document past the area's end");
    gf_kit_status(kit, (*kit).docking.area_active_document(workspace, text, sizeof(text), &required), GF_STATUS_OK,
                  "read the area's active document");
    gf_kit_check(kit, required == 8u && memcmp(text, "spectrum", 8u) == 0, "the only document is the active one");

    gf_dock_edit bounds = edit_record(GF_DOCK_EDIT_SET_FRAME_BOUNDS, "spectrum");
    bounds.frame.x = 12.0;
    bounds.frame.y = 16.0;
    bounds.frame.width = 300.0;
    bounds.frame.height = 200.0;
    gf_kit_status(kit, apply(kit, workspace, &bounds, NULL), GF_STATUS_OK, "place the document's frame");
    gf_dock_frame_memory frame = frame_of(kit, workspace, "spectrum");
    gf_kit_check(kit, frame.present == 1u && frame.bounds.x == 12.0 && frame.bounds.width == 300.0 &&
                          frame.state == GF_DOCK_FRAME_STATE_NORMAL,
                 "the frame keeps its normal bounds");
    gf_dock_edit empty_bounds = bounds;
    empty_bounds.frame.width = 0.0;
    gf_kit_status(kit, apply(kit, workspace, &empty_bounds, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "an empty frame is refused");

    gf_dock_edit minimized = edit_record(GF_DOCK_EDIT_SET_FRAME_STATE, "spectrum");
    minimized.frame_state = GF_DOCK_FRAME_STATE_MINIMIZED;
    gf_kit_status(kit, apply(kit, workspace, &minimized, NULL), GF_STATUS_OK, "minimize the frame");
    frame = frame_of(kit, workspace, "spectrum");
    gf_kit_check(kit, frame.state == GF_DOCK_FRAME_STATE_MINIMIZED && frame.icon_slot == 0u &&
                          frame.bounds.width == 300.0,
                 "minimized to the first icon slot, its bounds kept");
    gf_dock_edit slot = edit_record(GF_DOCK_EDIT_SET_FRAME_ICON_SLOT, "spectrum");
    slot.index = 3u;
    gf_kit_status(kit, apply(kit, workspace, &slot, NULL), GF_STATUS_OK, "move the icon to the fourth slot");
    frame = frame_of(kit, workspace, "spectrum");
    gf_kit_check(kit, frame.icon_slot == 3u, "the icon stands in the fourth slot");
    gf_dock_edit maximized = minimized;
    maximized.frame_state = GF_DOCK_FRAME_STATE_MAXIMIZED;
    gf_kit_status(kit, apply(kit, workspace, &maximized, NULL), GF_STATUS_OK, "maximize the frame");
    frame = frame_of(kit, workspace, "spectrum");
    gf_kit_check(kit, frame.state == GF_DOCK_FRAME_STATE_MAXIMIZED, "the frame is maximized");
    gf_dock_edit bad_state = minimized;
    bad_state.frame_state = 9u;
    gf_kit_status(kit, apply(kit, workspace, &bad_state, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown frame state is refused");
    gf_dock_edit tool_frame = edit_record(GF_DOCK_EDIT_SET_FRAME_STATE, "audio");
    tool_frame.frame_state = GF_DOCK_FRAME_STATE_MINIMIZED;
    gf_dock_refusal refusal = GF_DOCK_REFUSAL_NONE;
    gf_kit_status(kit, apply(kit, workspace, &tool_frame, &refusal), GF_STATUS_INVALID_STATE,
                  "a panel outside the document area has no frame state");
    gf_kit_check(kit, refusal != GF_DOCK_REFUSAL_NONE, "the refusal names why");
    frame = frame_of(kit, workspace, "audio");
    gf_kit_check(kit, frame.present == 0u && frame.bounds.width == 0.0, "a panel never framed has no frame");
    gf_kit_status(kit, (*kit).docking.frame_memory(workspace, gf_kit_text("nobody"), &frame), GF_STATUS_NOT_FOUND,
                  "an unregistered panel has no frame");
    frame.struct_size = 8u;
    gf_kit_status(kit, (*kit).docking.frame_memory(workspace, gf_kit_text("spectrum"), &frame),
                  GF_STATUS_INVALID_ARGUMENT, "a frame record below the 1.6 size is refused");

    refusal = 99u;
    gf_kit_status(kit, (*kit).docking.arrange_frames(workspace, GF_DOCK_FRAME_ARRANGEMENT_CASCADE, &refusal),
                  GF_STATUS_OK, "cascade the frames of an area never laid out");
    gf_kit_check(kit, refusal == GF_DOCK_REFUSAL_NONE, "an applied arrangement writes no refusal");
    gf_kit_status(kit, (*kit).docking.arrange_frames(workspace, GF_DOCK_FRAME_ARRANGEMENT_ARRANGE_ICONS, NULL),
                  GF_STATUS_OK, "arrange the icons");
    gf_kit_status(kit, (*kit).docking.arrange_frames(workspace, 4u, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown arrangement is refused");
    gf_kit_status(kit, (*kit).docking.set_frame_icons_at_bottom(workspace, 0u), GF_STATUS_OK,
                  "line the icons along the top");
    gf_kit_status(kit, (*kit).docking.set_frame_icons_at_bottom(workspace, 1u), GF_STATUS_OK,
                  "and along the bottom again");
    gf_kit_status(kit, (*kit).docking.set_frame_icons_at_bottom(workspace, 2u), GF_STATUS_INVALID_ARGUMENT,
                  "the icon edge is 0 or 1");

    const gf_object strip = gf_kit_create(kit, "menu_strip", "kit.dock.menu");
    const gf_object label = gf_kit_create(kit, "label", "kit.dock.not-a-strip");
    gf_kit_status(kit, (*kit).docking.set_panel_menu_strip(workspace, gf_kit_text("spectrum"), strip), GF_STATUS_OK,
                  "give the document a menu strip");
    gf_kit_status(kit, (*kit).docking.set_panel_menu_strip(workspace, gf_kit_text("spectrum"), label),
                  GF_STATUS_WRONG_HANDLE_KIND, "a label is not a menu strip");
    gf_kit_status(kit, (*kit).docking.set_panel_menu_strip(workspace, gf_kit_text("nobody"), strip),
                  GF_STATUS_NOT_FOUND, "an unregistered panel takes no strip");
    required = 99u;
    gf_kit_status(kit, (*kit).docking.merged_document(workspace, text, sizeof(text), &required), GF_STATUS_OK,
                  "read the merged document");
    gf_kit_check(kit, required == 0u, "no window's main menu strip, so nothing is merged");
    gf_kit_status(kit, (*kit).docking.set_panel_menu_strip(workspace, gf_kit_text("spectrum"), 0u), GF_STATUS_OK,
                  "take the strip away");

    gf_dock_edit tabs = framed;
    tabs.presentation = GF_DOCK_DOCUMENT_PRESENTATION_TABS;
    gf_kit_status(kit, apply(kit, workspace, &tabs, NULL), GF_STATUS_OK, "show the documents as tabs again");
    frame = frame_of(kit, workspace, "spectrum");
    gf_kit_check(kit, frame.present == 1u, "the frame waits while tabs show");
    gf_kit_status(kit, (*kit).objects.release(label), GF_STATUS_OK, "release the label");
    gf_kit_status(kit, (*kit).objects.release(strip), GF_STATUS_OK, "release the menu strip");
}

/* Tool strip and ribbon merging (since 1.7): the merge targets are held
 * and read back as new references, a panel takes a strip or a ribbon and
 * gives it back with zero, another object is the wrong kind, and the
 * merged document is the area's active document or none (empty strips and
 * ribbons may merge nothing). */
static void merges(gf_kit* kit, gf_object workspace) {
    char active[64];
    uint64_t active_size = 0u;
    gf_kit_status(kit, (*kit).docking.area_active_document(workspace, active, sizeof(active), &active_size),
                  GF_STATUS_OK, "read the area's active document");
    char text[64];
    uint64_t required = 0u;

    const gf_object target_strip = gf_kit_create(kit, "tool_strip", "kit.dock.merge.tools");
    const gf_object panel_strip = gf_kit_create(kit, "tool_strip", "kit.dock.panel.tools");
    const gf_object label = gf_kit_create(kit, "label", "kit.dock.merge.not-a-strip");
    gf_object held = 99u;
    gf_kit_status(kit, (*kit).docking.merge_tool_strip(workspace, &held), GF_STATUS_OK,
                  "read the merge tool strip before one is set");
    gf_kit_check(kit, held == 0u, "no merge tool strip is set");
    gf_kit_status(kit, (*kit).docking.set_merge_tool_strip(workspace, target_strip), GF_STATUS_OK,
                  "set the merge tool strip");
    gf_kit_status(kit, (*kit).docking.merge_tool_strip(workspace, &held), GF_STATUS_OK, "read it back");
    gf_kit_check(kit, held != 0u, "the workspace gives a reference to its merge tool strip");
    gf_kit_status(kit, (*kit).objects.release(held), GF_STATUS_OK, "release that reference");
    gf_kit_status(kit, (*kit).docking.merge_tool_strip(workspace, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "merge_tool_strip needs an output");
    gf_kit_status(kit, (*kit).docking.set_merge_tool_strip(workspace, label), GF_STATUS_WRONG_HANDLE_KIND,
                  "a label is not a tool strip");
    gf_kit_status(kit, (*kit).docking.set_panel_tool_strip(workspace, gf_kit_text("spectrum"), panel_strip),
                  GF_STATUS_OK, "give the document a tool strip");
    gf_kit_status(kit, (*kit).docking.set_panel_tool_strip(workspace, gf_kit_text("spectrum"), label),
                  GF_STATUS_WRONG_HANDLE_KIND, "a label is not a panel's tool strip");
    gf_kit_status(kit, (*kit).docking.set_panel_tool_strip(workspace, gf_kit_text("nobody"), panel_strip),
                  GF_STATUS_NOT_FOUND, "an unregistered panel takes no tool strip");
    required = 99u;
    gf_kit_status(kit, (*kit).docking.merged_tool_strip_document(workspace, text, sizeof(text), &required),
                  GF_STATUS_OK, "read the tool strip's merged document");
    gf_kit_check(kit, required == 0u || (required == active_size && memcmp(text, active, (size_t)required) == 0),
                 "a merged tool strip is the active document's");
    gf_kit_status(kit, (*kit).docking.set_panel_tool_strip(workspace, gf_kit_text("spectrum"), 0u), GF_STATUS_OK,
                  "take the panel's tool strip away");
    required = 99u;
    gf_kit_status(kit, (*kit).docking.merged_tool_strip_document(workspace, text, sizeof(text), &required),
                  GF_STATUS_OK, "read it again");
    gf_kit_check(kit, required == 0u, "no tool strip is merged once the panel has none");
    gf_kit_status(kit, (*kit).docking.set_merge_tool_strip(workspace, 0u), GF_STATUS_OK,
                  "clear the merge tool strip");
    held = 99u;
    gf_kit_status(kit, (*kit).docking.merge_tool_strip(workspace, &held), GF_STATUS_OK, "read it once cleared");
    gf_kit_check(kit, held == 0u, "the merge tool strip is cleared");

    const gf_object target_ribbon = gf_kit_create(kit, "ribbon", "kit.dock.merge.ribbon");
    const gf_object panel_ribbon = gf_kit_create(kit, "ribbon", "kit.dock.panel.ribbon");
    gf_kit_status(kit, (*kit).docking.set_merge_ribbon(workspace, target_ribbon), GF_STATUS_OK,
                  "set the merge ribbon");
    held = 0u;
    gf_kit_status(kit, (*kit).docking.merge_ribbon(workspace, &held), GF_STATUS_OK, "read the merge ribbon");
    gf_kit_check(kit, held != 0u, "the workspace gives a reference to its merge ribbon");
    gf_kit_status(kit, (*kit).objects.release(held), GF_STATUS_OK, "release that reference");
    gf_kit_status(kit, (*kit).docking.merge_ribbon(workspace, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "merge_ribbon needs an output");
    gf_kit_status(kit, (*kit).docking.set_merge_ribbon(workspace, target_strip), GF_STATUS_WRONG_HANDLE_KIND,
                  "a tool strip is not a ribbon");
    gf_kit_status(kit, (*kit).docking.set_panel_ribbon(workspace, gf_kit_text("spectrum"), panel_ribbon),
                  GF_STATUS_OK, "give the document a ribbon");
    gf_kit_status(kit, (*kit).docking.set_panel_ribbon(workspace, gf_kit_text("spectrum"), label),
                  GF_STATUS_WRONG_HANDLE_KIND, "a label is not a panel's ribbon");
    gf_kit_status(kit, (*kit).docking.set_panel_ribbon(workspace, gf_kit_text("nobody"), panel_ribbon),
                  GF_STATUS_NOT_FOUND, "an unregistered panel takes no ribbon");
    required = 99u;
    gf_kit_status(kit, (*kit).docking.merged_ribbon_document(workspace, text, sizeof(text), &required),
                  GF_STATUS_OK, "read the ribbon's merged document");
    gf_kit_check(kit, required == 0u, "a ribbon without tabs merges nothing");
    gf_kit_status(kit, (*kit).docking.set_panel_ribbon(workspace, gf_kit_text("spectrum"), 0u), GF_STATUS_OK,
                  "take the panel's ribbon away");
    gf_kit_status(kit, (*kit).docking.set_merge_ribbon(workspace, 0u), GF_STATUS_OK, "clear the merge ribbon");
    held = 99u;
    gf_kit_status(kit, (*kit).docking.merge_ribbon(workspace, &held), GF_STATUS_OK, "read it once cleared");
    gf_kit_check(kit, held == 0u, "the merge ribbon is cleared");

    gf_kit_status(kit, (*kit).objects.release(panel_ribbon), GF_STATUS_OK, "release the panel's ribbon");
    gf_kit_status(kit, (*kit).objects.release(target_ribbon), GF_STATUS_OK, "release the merge ribbon");
    gf_kit_status(kit, (*kit).objects.release(label), GF_STATUS_OK, "release the label");
    gf_kit_status(kit, (*kit).objects.release(panel_strip), GF_STATUS_OK, "release the panel's tool strip");
    gf_kit_status(kit, (*kit).objects.release(target_strip), GF_STATUS_OK, "release the merge tool strip");
}

/* A window's MDI slots (since 1.6): over a plain root no document area is
 * bound, so the window keeps its settings, has no children and arranges
 * nothing; its main menu strip is held weakly and refuses another object.
 * Over the workspace's site the area may bind before the run (when the
 * window attaches its root); either way the window lists what it holds. */
static void mdi_window(gf_kit* kit, gf_object workspace, gf_object site) {
    const gf_object root = gf_kit_create(kit, "Panel", "kit.mdi");
    gf_object window = 0u;
    gf_kit_status(kit, (*kit).windows.create(root, &window), GF_STATUS_OK, "create a window");
    uint32_t value = 99u;
    gf_kit_status(kit, (*kit).windows.mdi_container(window, &value), GF_STATUS_OK, "read the container switch");
    gf_kit_check(kit, value == 0u, "a window is not an MDI container by default");
    gf_kit_status(kit, (*kit).windows.set_mdi_container(window, 1u), GF_STATUS_OK, "make it an MDI container");
    gf_kit_status(kit, (*kit).windows.mdi_container(root, &value), GF_STATUS_OK,
                  "read the switch through the root");
    gf_kit_check(kit, value == 1u, "the switch is kept with no area bound");
    gf_kit_status(kit, (*kit).windows.set_mdi_container(window, 2u), GF_STATUS_INVALID_ARGUMENT,
                  "the switch is 0 or 1");
    gf_kit_status(kit, (*kit).windows.mdi_children_minimized_anchor_bottom(window, &value), GF_STATUS_OK,
                  "read the icon anchor");
    gf_kit_check(kit, value == 1u, "minimized children line the bottom by default");
    gf_kit_status(kit, (*kit).windows.set_mdi_children_minimized_anchor_bottom(window, 0u), GF_STATUS_OK,
                  "anchor them at the top");
    gf_kit_status(kit, (*kit).windows.mdi_children_minimized_anchor_bottom(window, &value), GF_STATUS_OK,
                  "read the anchor again");
    gf_kit_check(kit, value == 0u, "the anchor is kept");

    uint64_t count = 99u;
    gf_kit_status(kit, (*kit).windows.mdi_child_count(window, &count), GF_STATUS_OK, "count the MDI children");
    gf_kit_check(kit, count == 0u, "no area, no children");
    char text[32];
    uint64_t required = 99u;
    gf_kit_status(kit, (*kit).windows.mdi_child_at(window, 0u, text, sizeof(text), &required), GF_STATUS_NOT_FOUND,
                  "no child at index zero");
    gf_kit_status(kit, (*kit).windows.active_mdi_child(window, text, sizeof(text), &required), GF_STATUS_OK,
                  "read the active child");
    gf_kit_check(kit, required == 0u, "there is no active child");
    uint32_t done = 99u;
    gf_kit_status(kit, (*kit).windows.activate_mdi_child(window, gf_kit_text("spectrum"), &done), GF_STATUS_OK,
                  "activate a child the window does not hold");
    gf_kit_check(kit, done == 0u, "nothing is activated");
    done = 99u;
    gf_kit_status(kit, (*kit).windows.layout_mdi(window, GF_MDI_LAYOUT_TILE_VERTICAL, &done), GF_STATUS_OK,
                  "tile the children");
    gf_kit_check(kit, done == 0u, "nothing is arranged");
    gf_kit_status(kit, (*kit).windows.layout_mdi(window, 4u, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown layout is refused");

    const gf_object strip = gf_kit_create(kit, "menu_strip", "kit.mdi.menu");
    gf_kit_status(kit, (*kit).windows.set_main_menu_strip(window, strip), GF_STATUS_OK, "set the main menu strip");
    gf_object held = 0u;
    gf_kit_status(kit, (*kit).windows.main_menu_strip(window, &held), GF_STATUS_OK, "read the main menu strip");
    gf_kit_check(kit, held != 0u, "the window gives a reference to its strip");
    gf_kit_status(kit, (*kit).objects.release(held), GF_STATUS_OK, "release that reference");
    gf_kit_status(kit, (*kit).windows.set_main_menu_strip(window, root), GF_STATUS_WRONG_HANDLE_KIND,
                  "a panel is not a menu strip");
    gf_kit_status(kit, (*kit).windows.set_main_menu_strip(window, 0u), GF_STATUS_OK, "clear the main menu strip");
    held = 99u;
    gf_kit_status(kit, (*kit).windows.main_menu_strip(window, &held), GF_STATUS_OK, "read it again");
    gf_kit_check(kit, held == 0u, "no strip is set");
    gf_kit_status(kit, (*kit).windows.main_menu_strip(window, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "main_menu_strip needs an output");
    gf_kit_status(kit, (*kit).windows.mdi_container(workspace, &value), GF_STATUS_WRONG_HANDLE_KIND,
                  "a workspace is neither a window nor a control in one");
    gf_kit_status(kit, (*kit).windows.mdi_child_count(strip, &count), GF_STATUS_INVALID_STATE,
                  "a control outside a window has no MDI children");

    gf_object docked = 0u;
    gf_kit_status(kit, (*kit).windows.create(site, &docked), GF_STATUS_OK, "create a window over the dock site");
    uint64_t area_count = 0u;
    gf_kit_status(kit, (*kit).docking.area_document_count(workspace, &area_count), GF_STATUS_OK,
                  "count the workspace's area documents");
    gf_kit_status(kit, (*kit).windows.mdi_child_count(docked, &count), GF_STATUS_OK,
                  "count the docked window's MDI children");
    gf_kit_check(kit, count == area_count || count == 0u,
                 "the window lists the area's documents once the area is bound, none before");

    gf_kit_status(kit, (*kit).objects.release(docked), GF_STATUS_OK, "release the docked window");
    gf_kit_status(kit, (*kit).objects.release(strip), GF_STATUS_OK, "release the strip");
    gf_kit_status(kit, (*kit).objects.release(window), GF_STATUS_OK, "release the window");
    gf_kit_status(kit, (*kit).objects.release(root), GF_STATUS_OK, "release its root");
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
    frames(kit, workspace);
    merges(kit, workspace);
    mdi_window(kit, workspace, site);
    gf_kit_check(kit, changes.invoked >= 10u, "every applied edit and the restore raised layout_changed");
    gf_kit_status(kit, (*kit).docking.attach_windows(workspace), GF_STATUS_INVALID_STATE,
                  "no window runs to attach");

    gf_kit_status(kit, (*kit).events.disconnect(subscription), GF_STATUS_OK, "disconnect");
    gf_kit_status(kit, (*kit).objects.release(site), GF_STATUS_OK, "release the site");
    gf_kit_status(kit, (*kit).objects.release(second), GF_STATUS_OK, "release a content control");
    gf_kit_status(kit, (*kit).objects.release(first), GF_STATUS_OK, "release the other");
    gf_kit_status(kit, (*kit).objects.release(workspace), GF_STATUS_OK, "release the workspace");
}
