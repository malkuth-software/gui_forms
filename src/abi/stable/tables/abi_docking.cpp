// GF_TABLE_DOCKING: a dock workspace's panels and layout, over the C++
// DockWorkspace. See gf_abi.h.

#include "abi_tables.hpp"

#include "../failure/abi_failure.hpp"
#include "../registry/abi_registry.hpp"

#include "gui_forms/application/application.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/dock_workspace.hpp"
#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"
#include "gui_forms/docking.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity register_panel_slot{GF_TABLE_DOCKING, 0U};
constexpr SlotIdentity set_panel_content_slot{GF_TABLE_DOCKING, 1U};
constexpr SlotIdentity set_panel_title_slot{GF_TABLE_DOCKING, 2U};
constexpr SlotIdentity apply_edit_slot{GF_TABLE_DOCKING, 3U};
constexpr SlotIdentity locate_slot{GF_TABLE_DOCKING, 4U};
constexpr SlotIdentity save_layout_slot{GF_TABLE_DOCKING, 5U};
constexpr SlotIdentity load_layout_slot{GF_TABLE_DOCKING, 6U};
constexpr SlotIdentity activate_panel_slot{GF_TABLE_DOCKING, 7U};
constexpr SlotIdentity attach_windows_slot{GF_TABLE_DOCKING, 8U};
constexpr SlotIdentity document_area_slot{GF_TABLE_DOCKING, 9U};
constexpr SlotIdentity panel_at_slot{GF_TABLE_DOCKING, 10U};
constexpr SlotIdentity floating_sites_slot{GF_TABLE_DOCKING, 11U};
constexpr SlotIdentity floating_site_slot{GF_TABLE_DOCKING, 12U};
constexpr SlotIdentity update_panel_slot{GF_TABLE_DOCKING, 13U};
constexpr SlotIdentity set_options_slot{GF_TABLE_DOCKING, 14U};
constexpr SlotIdentity set_floating_window_visibility_slot{GF_TABLE_DOCKING, 15U};

constexpr const char* not_workspace = "the object is not a dock workspace";

// The 1.1 sizes of the records this table reads and writes.
constexpr std::uint32_t panel_size_1_1 = 136U;
constexpr std::uint32_t edit_size_1_1 = 104U;
constexpr std::uint32_t location_size_1_1 = 40U;
constexpr std::uint32_t options_size_1_1 = 16U;
static_assert(sizeof(gf_dock_options) == options_size_1_1);
static_assert(sizeof(gf_dock_panel) == panel_size_1_1);
static_assert(sizeof(gf_dock_edit) == edit_size_1_1);
static_assert(sizeof(gf_dock_location) == location_size_1_1);

// gf_dock_side and DockSide, gf_dock_placement and DockPlacementKind, and
// gf_dock_refusal and DockRefusalReason number their values alike.
static_assert(static_cast<std::uint32_t>(DockSide::bottom) == GF_DOCK_SIDE_BOTTOM);
static_assert(static_cast<std::uint32_t>(DockPlacementKind::absent) == GF_DOCK_PLACEMENT_ABSENT);
static_assert(static_cast<std::uint32_t>(DockRefusalReason::user_docking_locked) ==
              GF_DOCK_REFUSAL_USER_DOCKING_LOCKED);

using WorkspacePtr = std::shared_ptr<DockWorkspace>;

// -- panel contents --------------------------------------------------------------

// The controls a C program gave its panels, and the factory the workspace
// asks for a panel's content (it hands back the given control). One per
// workspace; the factory delegate is bound to this object, which is never
// destroyed while its workspace lives (the holder below prunes it only
// after the workspace has gone, when no factory call can come).
class WorkspaceContents final {
public:
    explicit WorkspaceContents(const WorkspacePtr& workspace) : workspace_(workspace) {}
    WorkspaceContents(const WorkspaceContents&) = delete;
    WorkspaceContents& operator=(const WorkspaceContents&) = delete;

    [[nodiscard]] bool belongs_to(const DockWorkspace& workspace) const noexcept {
        const WorkspacePtr held = workspace_.lock();
        const bool same = held && held.get() == &workspace;
        return same;
    }
    [[nodiscard]] bool expired() const noexcept { return workspace_.expired(); }

    // The given control of panel, replacing any before; null forgets it.
    void set(const DockPanelId& panel, Control::Ptr content) {
        for (PanelContent& entry : panels_) {
            if (!(entry.panel == panel)) continue;
            entry.content = std::move(content);
            return;
        }
        PanelContent added{};
        added.panel = panel;
        added.content = std::move(content);
        panels_.push_back(std::move(added));
    }

    // The factory (DockPanelRegistration::factory): the panel's given
    // control, or none (an empty area).
    void create(DockPanelCreation& creation) {
        for (const PanelContent& entry : panels_) {
            if (entry.panel == creation.panel) creation.content = entry.content;
        }
    }

private:
    struct PanelContent final {
        DockPanelId panel{};
        Control::Ptr content{};
    };
    std::weak_ptr<DockWorkspace> workspace_;
    std::vector<PanelContent> panels_{};
};

// Every workspace's contents. Never destroyed, like the registry, so no
// control is released by a static destructor at exit. UI thread only.
std::vector<std::unique_ptr<WorkspaceContents>>& all_contents() noexcept {
    static std::vector<std::unique_ptr<WorkspaceContents>>* const instance =
        new std::vector<std::unique_ptr<WorkspaceContents>>();
    return *instance;
}

// Whether held belongs to a workspace that has gone (the prune predicate).
bool is_expired(const std::unique_ptr<WorkspaceContents>& held) noexcept {
    return (*held).expired();
}

// The contents of workspace, made on first use; the contents of workspaces
// that have gone are released first.
WorkspaceContents& contents_of(const WorkspacePtr& workspace) {
    std::vector<std::unique_ptr<WorkspaceContents>>& all = all_contents();
    const std::vector<std::unique_ptr<WorkspaceContents>>::iterator gone =
        std::remove_if(all.begin(), all.end(), &is_expired);
    all.erase(gone, all.end());
    for (const std::unique_ptr<WorkspaceContents>& held : all) {
        if ((*held).belongs_to(*workspace)) return *held;
    }
    all.push_back(std::make_unique<WorkspaceContents>(workspace));
    return *all.back();
}

// -- entry -----------------------------------------------------------------------

// Enters the UI thread and finds the live workspace behind object.
gf_status enter_workspace(gf_object object, SlotIdentity slot, WorkspacePtr& workspace) {
    const gf_status entered = registry().enter_ui(slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    Component::Ptr component;
    const gf_status found = registry().find_component(object, slot, component);
    if (found != GF_STATUS_OK) {
        return found;
    }
    WorkspacePtr typed = std::dynamic_pointer_cast<DockWorkspace>(component);
    if (!typed) {
        return fail(GF_STATUS_WRONG_HANDLE_KIND, slot, not_workspace);
    }
    if (!(*typed).is_alive()) {
        return fail(GF_STATUS_DISPOSED, slot, "the workspace has been disposed");
    }
    workspace = std::move(typed);
    return GF_STATUS_OK;
}

// A registered panel's id from the caller's text.
gf_status read_registered_panel(const DockWorkspace& workspace, gf_utf8_view text, SlotIdentity slot,
                                DockPanelId& panel) {
    std::string_view id;
    const gf_status read = read_text(text, slot, id);
    if (read != GF_STATUS_OK) {
        return read;
    }
    DockPanelId made{std::string(id)};
    if (workspace.find_registration(made) == nullptr) {
        return fail(GF_STATUS_NOT_FOUND, slot, "no panel of that id is registered");
    }
    panel = std::move(made);
    return GF_STATUS_OK;
}

// The live control behind content, or none for zero.
gf_status read_content(gf_object content, SlotIdentity slot, Control::Ptr& control) {
    if (content == 0U) {
        control.reset();
        return GF_STATUS_OK;
    }
    Control::Ptr found;
    const gf_status status = registry().find_object(content, slot, found);
    if (status != GF_STATUS_OK) {
        return status;
    }
    if (!(*found).is_alive()) {
        return fail(GF_STATUS_DISPOSED, slot, "the content control has been disposed");
    }
    control = std::move(found);
    return GF_STATUS_OK;
}

bool valid_side(gf_dock_side side) noexcept {
    return side <= GF_DOCK_SIDE_BOTTOM;
}

bool valid_frame(const gf_logical_rect& frame) noexcept {
    const bool finite = std::isfinite(frame.x) && std::isfinite(frame.y) && std::isfinite(frame.width) &&
                        std::isfinite(frame.height);
    return finite && frame.width > 0.0 && frame.height > 0.0;
}

Rect rect_of(const gf_logical_rect& frame) noexcept {
    const Rect made{frame.x, frame.y, frame.width, frame.height};
    return made;
}

// The descriptor a caller's record describes, every field checked before
// the workspace is touched.
gf_status read_descriptor(const gf_dock_panel* record, SlotIdentity slot, DockPanelDescriptor& descriptor,
                          std::string& title) {
    if (record == nullptr || (*record).struct_size < panel_size_1_1 || (*record).reserved != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a panel record has at least the 1.1 size and reserved zero");
    }
    constexpr gf_dock_allowed known_allowed = GF_DOCK_ALLOWED_ALL | GF_DOCK_ALLOWED_CLOSE_BUTTON_HIDDEN;
    if ((*record).kind > GF_DOCK_PANEL_KIND_DOCUMENT || ((*record).allowed & ~known_allowed) != 0U ||
        (*record).dock_as_document > 1U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot,
                    "a panel has a known kind, known allowed flags and dock_as_document 0 or 1");
    }
    const gf_dock_placement placement = (*record).default_placement;
    if (placement != GF_DOCK_PLACEMENT_DOCKED && placement != GF_DOCK_PLACEMENT_FLOATING &&
        placement != GF_DOCK_PLACEMENT_AUTO_HIDDEN) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a default placement is DOCKED, FLOATING or AUTO_HIDDEN");
    }
    const double ratio = (*record).default_ratio;
    const double extent = (*record).default_extent;
    if (!valid_side((*record).default_side) || !std::isfinite(ratio) || ratio <= 0.0 || ratio >= 1.0 ||
        !std::isfinite(extent) || extent <= 0.0 || !valid_frame((*record).default_frame)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot,
                    "a default placement has a side, a ratio in (0, 1), a positive extent and a positive frame");
    }
    const gf_logical_size minimum = (*record).minimum_size;
    if (!std::isfinite(minimum.width) || !std::isfinite(minimum.height) || minimum.width < 0.0 ||
        minimum.height < 0.0) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a minimum size is finite and not negative");
    }
    std::string_view id;
    std::string_view title_text;
    gf_status read = read_text((*record).id, slot, id);
    if (read == GF_STATUS_OK) read = read_text((*record).title, slot, title_text);
    if (read != GF_STATUS_OK) {
        return read;
    }
    // A panel id names its page in a stack's page set, so it is spelled as
    // a stable id (lowercase segments joined by '.' or single '-').
    if (id.empty() || !valid_stable_id(id)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot,
                    "a panel id is a stable id: '.'-separated segments of lowercase ASCII letters and digits joined by "
                    "single '-'");
    }
    const gf_dock_allowed allowed = (*record).allowed;
    DockPanelDescriptor made{};
    made.id = DockPanelId(std::string(id));
    made.kind = (*record).kind == GF_DOCK_PANEL_KIND_DOCUMENT ? DockPanelKind::document : DockPanelKind::tool;
    made.dock_as_document = (*record).dock_as_document == 1U;
    made.allowed.docked = (allowed & GF_DOCK_ALLOWED_DOCKED) != 0U;
    made.allowed.document_area = (allowed & GF_DOCK_ALLOWED_DOCUMENT_AREA) != 0U;
    made.allowed.floating = (allowed & GF_DOCK_ALLOWED_FLOATING) != 0U;
    made.allowed.auto_hidden = (allowed & GF_DOCK_ALLOWED_AUTO_HIDDEN) != 0U;
    made.allowed.tabbed = (allowed & GF_DOCK_ALLOWED_TABBED) != 0U;
    made.allowed.hidden = (allowed & GF_DOCK_ALLOWED_HIDDEN) != 0U;
    made.allowed.closed = (allowed & GF_DOCK_ALLOWED_CLOSED) != 0U;
    // No side named permits every side.
    constexpr gf_dock_allowed sides = GF_DOCK_ALLOWED_SIDE_LEFT | GF_DOCK_ALLOWED_SIDE_TOP |
                                      GF_DOCK_ALLOWED_SIDE_RIGHT | GF_DOCK_ALLOWED_SIDE_BOTTOM;
    const bool every_side = (allowed & sides) == 0U;
    made.allowed.left = every_side || (allowed & GF_DOCK_ALLOWED_SIDE_LEFT) != 0U;
    made.allowed.top = every_side || (allowed & GF_DOCK_ALLOWED_SIDE_TOP) != 0U;
    made.allowed.right = every_side || (allowed & GF_DOCK_ALLOWED_SIDE_RIGHT) != 0U;
    made.allowed.bottom = every_side || (allowed & GF_DOCK_ALLOWED_SIDE_BOTTOM) != 0U;
    made.close_button_visible = (allowed & GF_DOCK_ALLOWED_CLOSE_BUTTON_HIDDEN) == 0U;
    made.minimum_size = Size{minimum.width, minimum.height};
    made.default_placement.kind = static_cast<DockPlacementKind>(placement);
    made.default_placement.side = static_cast<DockSide>((*record).default_side);
    made.default_placement.ratio = ratio;
    made.default_placement.extent = extent;
    made.default_placement.frame = rect_of((*record).default_frame);
    descriptor = std::move(made);
    title = std::string(title_text);
    return GF_STATUS_OK;
}

// The C++ edit a caller's record describes, checked before the layout sees
// it (the layout validates the rest: identities, ratios and permissions).
gf_status read_edit(const DockWorkspace& workspace, const gf_dock_edit* record, SlotIdentity slot, DockEdit& edit) {
    if (record == nullptr || (*record).struct_size < edit_size_1_1 || (*record).reserved != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "an edit record has at least the 1.1 size and reserved zero");
    }
    if ((*record).kind > GF_DOCK_EDIT_SET_FLOATING_FRAME || (*record).subject_is_stack > 1U ||
        !valid_side((*record).side) || !std::isfinite((*record).ratio) || !std::isfinite((*record).extent)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot,
                    "an edit has a known kind, subject_is_stack 0 or 1, a side and finite numbers");
    }
    const gf_dock_edit_kind kind = (*record).kind;
    const bool stack_subject = (*record).subject_is_stack == 1U;
    const bool reads_subject = kind <= GF_DOCK_EDIT_AUTO_HIDE;
    const bool reads_panel = (reads_subject && !stack_subject) || kind == GF_DOCK_EDIT_HIDE ||
                             kind == GF_DOCK_EDIT_SHOW || kind == GF_DOCK_EDIT_CLOSE || kind == GF_DOCK_EDIT_MOVE ||
                             kind == GF_DOCK_EDIT_SELECT;
    DockPanelId panel{};
    if (reads_panel) {
        const gf_status read = read_registered_panel(workspace, (*record).panel, slot, panel);
        if (read != GF_STATUS_OK) {
            return read;
        }
    }
    if ((kind == GF_DOCK_EDIT_FLOAT || kind == GF_DOCK_EDIT_SET_FLOATING_FRAME) && !valid_frame((*record).frame)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a floating frame has a positive size");
    }
    DockSubject subject = stack_subject ? DockSubject::of_stack(DockNodeId{(*record).subject_stack})
                                        : DockSubject::of_panel(panel);
    const DockNodeId target{(*record).target_node};
    const DockSide side = static_cast<DockSide>((*record).side);
    const std::uint32_t index = (*record).index;
    const double ratio = (*record).ratio;
    switch (kind) {
    case GF_DOCK_EDIT_DOCK_INTO:
        edit = DockEdit::dock_into(std::move(subject), target, index);
        break;
    case GF_DOCK_EDIT_DOCK_BESIDE:
        edit = DockEdit::dock_beside(std::move(subject), target, side, ratio);
        break;
    case GF_DOCK_EDIT_DOCK_AT_EDGE:
        edit = DockEdit::dock_at_edge(std::move(subject), side, ratio);
        break;
    case GF_DOCK_EDIT_FLOAT:
        edit = DockEdit::float_subject(std::move(subject), rect_of((*record).frame), std::string{});
        break;
    case GF_DOCK_EDIT_AUTO_HIDE:
        edit = DockEdit::auto_hide(std::move(subject), side, (*record).extent);
        break;
    case GF_DOCK_EDIT_UNPIN:
        edit = DockEdit::unpin(DockNodeId{(*record).subject_stack});
        break;
    case GF_DOCK_EDIT_HIDE:
        edit = DockEdit::hide(std::move(panel));
        break;
    case GF_DOCK_EDIT_SHOW:
        edit = DockEdit::show(std::move(panel));
        break;
    case GF_DOCK_EDIT_CLOSE:
        edit = DockEdit::close(std::move(panel));
        break;
    case GF_DOCK_EDIT_MOVE:
        edit = DockEdit::move(std::move(panel), index);
        break;
    case GF_DOCK_EDIT_SELECT:
        edit = DockEdit::select(std::move(panel));
        break;
    case GF_DOCK_EDIT_SET_RATIO:
        edit = DockEdit::set_ratio(target, ratio);
        break;
    default:
        edit = DockEdit::set_floating_frame(DockSiteId{(*record).site}, rect_of((*record).frame), std::string{});
        break;
    }
    return GF_STATUS_OK;
}

// -- the document area's side ------------------------------------------------------

// Whether target is node or under it (a split's children, the document
// area's content). Bounded by the tree's depth.
// Recurses once per dock node level: a committed tree is at most
// dock_maximum_depth (32) deep, refused beyond by the invariant check;
// a few hundred bytes of stack per level.
bool contains(const DockLayout& layout, DockNodeId node_id, DockNodeId target) {
    if (node_id == DockNodeId{}) return false;
    if (node_id == target) return true;
    const DockNode* const node = layout.find_node(node_id);
    if (node == nullptr) return false;
    if ((*node).kind == DockNodeKind::split) {
        const bool found = contains(layout, (*node).first, target) || contains(layout, (*node).second, target);
        return found;
    }
    if ((*node).kind == DockNodeKind::document_area) {
        const bool found = contains(layout, (*node).first, target);
        return found;
    }
    return false;
}

// The side of the main site's document area that stack stands on: walking
// up from the stack, the first split whose other child holds the document
// area names it. NONE inside the document area or outside the main site.
gf_dock_side document_side(const DockLayout& layout, DockNodeId stack) {
    const DockNodeId area = layout.document_area();
    DockNodeId child = stack;
    const DockNode* node = layout.find_node(stack);
    while (node != nullptr && !((*node).parent == DockNodeId{})) {
        const DockNodeId parent_id = (*node).parent;
        const DockNode* const parent = layout.find_node(parent_id);
        if (parent == nullptr) break;
        if ((*parent).kind == DockNodeKind::document_area) return GF_DOCK_SIDE_NONE;
        if ((*parent).kind == DockNodeKind::split) {
            const bool first = (*parent).first == child;
            const DockNodeId other = first ? (*parent).second : (*parent).first;
            if (contains(layout, other, area)) {
                const bool horizontal = (*parent).axis == DockAxis::horizontal;
                if (horizontal) return first ? GF_DOCK_SIDE_LEFT : GF_DOCK_SIDE_RIGHT;
                return first ? GF_DOCK_SIDE_TOP : GF_DOCK_SIDE_BOTTOM;
            }
        }
        child = parent_id;
        node = parent;
    }
    return GF_DOCK_SIDE_NONE;
}

gf_dock_node_kind node_kind_of(DockNodeKind kind) noexcept {
    switch (kind) {
    case DockNodeKind::split:
        return GF_DOCK_NODE_KIND_SPLIT;
    case DockNodeKind::tool_stack:
        return GF_DOCK_NODE_KIND_TOOL_STACK;
    case DockNodeKind::document_stack:
        return GF_DOCK_NODE_KIND_DOCUMENT_STACK;
    case DockNodeKind::document_area:
        return GF_DOCK_NODE_KIND_DOCUMENT_AREA;
    }
    return GF_DOCK_NODE_KIND_NONE;
}

// -- slots -------------------------------------------------------------------------

gf_status GF_CALL docking_register_panel(gf_object workspace, const gf_dock_panel* panel) noexcept {
    try {
        const ShadowFrameScope shadow_frame(register_panel_slot.table_id, register_panel_slot.slot_index);
        WorkspacePtr typed;
        const gf_status entered = enter_workspace(workspace, register_panel_slot, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        DockPanelDescriptor descriptor{};
        std::string title;
        const gf_status read = read_descriptor(panel, register_panel_slot, descriptor, title);
        if (read != GF_STATUS_OK) {
            return read;
        }
        Control::Ptr content;
        const gf_status found = read_content((*panel).content, register_panel_slot, content);
        if (found != GF_STATUS_OK) {
            return found;
        }
        if ((*typed).find_registration(descriptor.id) != nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, register_panel_slot, "a panel of that id is registered already");
        }
        WorkspaceContents& contents = contents_of(typed);
        const DockPanelId id = descriptor.id;
        DockPanelRegistration registration{};
        registration.descriptor = std::move(descriptor);
        registration.title = std::move(title);
        registration.factory =
            Delegate<DockPanelCreation&>::bind<WorkspaceContents, &WorkspaceContents::create>(contents);
        if (!(*typed).register_panel(std::move(registration))) {
            return fail(GF_STATUS_INVALID_ARGUMENT, register_panel_slot, "the layout refused the panel's descriptor");
        }
        contents.set(id, std::move(content));
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(register_panel_slot);
    }
}

gf_status GF_CALL docking_set_panel_content(gf_object workspace, gf_utf8_view panel, gf_object content) noexcept {
    try {
        const ShadowFrameScope shadow_frame(set_panel_content_slot.table_id, set_panel_content_slot.slot_index);
        WorkspacePtr typed;
        const gf_status entered = enter_workspace(workspace, set_panel_content_slot, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        DockPanelId id;
        const gf_status read = read_registered_panel(*typed, panel, set_panel_content_slot, id);
        if (read != GF_STATUS_OK) {
            return read;
        }
        Control::Ptr control;
        const gf_status found = read_content(content, set_panel_content_slot, control);
        if (found != GF_STATUS_OK) {
            return found;
        }
        const Control::Ptr held = (*typed).panel_content(id);
        if (held && held != control) {
            return fail(GF_STATUS_INVALID_STATE, set_panel_content_slot,
                        "the workspace holds the panel's content; close the panel first");
        }
        contents_of(typed).set(id, std::move(control));
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(set_panel_content_slot);
    }
}

gf_status GF_CALL docking_set_panel_title(gf_object workspace, gf_utf8_view panel, gf_utf8_view title) noexcept {
    try {
        const ShadowFrameScope shadow_frame(set_panel_title_slot.table_id, set_panel_title_slot.slot_index);
        WorkspacePtr typed;
        const gf_status entered = enter_workspace(workspace, set_panel_title_slot, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        DockPanelId id;
        const gf_status read = read_registered_panel(*typed, panel, set_panel_title_slot, id);
        if (read != GF_STATUS_OK) {
            return read;
        }
        std::string_view text;
        const gf_status title_read = read_text(title, set_panel_title_slot, text);
        if (title_read != GF_STATUS_OK) {
            return title_read;
        }
        static_cast<void>((*typed).set_panel_title(id, std::string(text)));
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(set_panel_title_slot);
    }
}

gf_status GF_CALL docking_apply_edit(gf_object workspace, const gf_dock_edit* edit, gf_dock_refusal* refusal) noexcept {
    try {
        const ShadowFrameScope shadow_frame(apply_edit_slot.table_id, apply_edit_slot.slot_index);
        WorkspacePtr typed;
        const gf_status entered = enter_workspace(workspace, apply_edit_slot, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        DockEdit made{};
        const gf_status read = read_edit(*typed, edit, apply_edit_slot, made);
        if (read != GF_STATUS_OK) {
            return read;
        }
        // Show, hide, close and float go through the workspace's own
        // operations, which also keep its contents and floating windows;
        // the rest is one program transaction.
        DockTransactionResult result{};
        switch (made.kind) {
        case DockEditKind::show:
            result = (*typed).show_panel(made.subject.panel, DockEditOrigin::program);
            break;
        case DockEditKind::hide:
            result = (*typed).hide_panel(made.subject.panel, DockEditOrigin::program);
            break;
        case DockEditKind::close:
            result = (*typed).close_panel(made.subject.panel, DockEditOrigin::program);
            break;
        case DockEditKind::float_subject:
            if ((*typed).windows_attached()) {
                result = (*typed).float_subject(made.subject, made.frame, DockEditOrigin::program);
                break;
            }
            [[fallthrough]];
        default: {
            // Every other edit (docking, moving, sizing) is one program transaction.
            DockTransaction transaction{};
            transaction.origin = DockEditOrigin::program;
            transaction.edits.push_back(std::move(made));
            result = (*typed).apply(transaction);
            break;
        }
        }
        const gf_dock_refusal reason = static_cast<gf_dock_refusal>(result.refusal.reason);
        if (refusal != nullptr) *refusal = result.applied ? GF_DOCK_REFUSAL_NONE : reason;
        if (!result.applied) {
            return fail(GF_STATUS_INVALID_STATE, apply_edit_slot, "the layout refused the edit");
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(apply_edit_slot);
    }
}

gf_status GF_CALL docking_locate(gf_object workspace, gf_utf8_view panel, gf_dock_location* location) noexcept {
    try {
        const ShadowFrameScope shadow_frame(locate_slot.table_id, locate_slot.slot_index);
        if (location == nullptr || (*location).struct_size < location_size_1_1) {
            return fail(GF_STATUS_INVALID_ARGUMENT, locate_slot, "locate needs a location of at least the 1.1 size");
        }
        WorkspacePtr typed;
        const gf_status entered = enter_workspace(workspace, locate_slot, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        DockPanelId id;
        const gf_status read = read_registered_panel(*typed, panel, locate_slot, id);
        if (read != GF_STATUS_OK) {
            return read;
        }
        const DockLayout& layout = (*typed).layout();
        const DockPanelLocation found = layout.locate(id);
        gf_dock_location made{};
        made.struct_size = location_size_1_1;
        made.placement = static_cast<gf_dock_placement>(found.placement);
        made.stack_kind = GF_DOCK_NODE_KIND_NONE;
        made.side = GF_DOCK_SIDE_NONE;
        const DockNode* const stack = layout.find_node(found.stack);
        if (stack != nullptr) {
            made.stack = found.stack.value;
            made.index = found.index;
            made.stack_kind = node_kind_of((*stack).kind);
            made.selected = (*stack).selected == found.index ? 1U : 0U;
            made.stack_panel_count = static_cast<std::uint32_t>((*stack).panels.size());
        }
        if (found.placement == DockPlacementKind::floating) made.site = found.site.value;
        if (found.placement == DockPlacementKind::docked) made.side = document_side(layout, found.stack);
        if (found.placement == DockPlacementKind::auto_hidden) {
            for (const DockAutoHideEntry& entry : layout.state().auto_hidden) {
                if (entry.stack == found.stack) made.side = static_cast<gf_dock_side>(entry.edge);
            }
        }
        // The caller's record may be larger; only the 1.1 fields are ours.
        const std::uint32_t caller_size = (*location).struct_size;
        *location = made;
        (*location).struct_size = caller_size;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(locate_slot);
    }
}

gf_status GF_CALL docking_save_layout(gf_object workspace, char* buffer, std::uint64_t capacity,
                                      std::uint64_t* required) noexcept {
    try {
        const ShadowFrameScope shadow_frame(save_layout_slot.table_id, save_layout_slot.slot_index);
        WorkspacePtr typed;
        const gf_status entered = enter_workspace(workspace, save_layout_slot, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const DockLayoutWriteResult written = (*typed).save_layout();
        if (!written) {
            return fail(GF_STATUS_INTERNAL, save_layout_slot, "the layout could not be written");
        }
        return write_text(written.text, buffer, capacity, required, save_layout_slot);
    } catch (...) {
        return fail_with_current_exception(save_layout_slot);
    }
}

gf_status GF_CALL docking_load_layout(gf_object workspace, gf_utf8_view xml, std::uint64_t* diagnostic_count) noexcept {
    try {
        const ShadowFrameScope shadow_frame(load_layout_slot.table_id, load_layout_slot.slot_index);
        WorkspacePtr typed;
        const gf_status entered = enter_workspace(workspace, load_layout_slot, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        std::string_view text;
        const gf_status read = read_text(xml, load_layout_slot, text);
        if (read != GF_STATUS_OK) {
            return read;
        }
        const DockRestoreResult result = (*typed).load_layout(text);
        if (diagnostic_count != nullptr) *diagnostic_count = result.diagnostics.size();
        if (result.status != DockRestoreStatus::restored) {
            return fail(GF_STATUS_INVALID_ARGUMENT, load_layout_slot, "the layout was unreadable or refused; kept");
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(load_layout_slot);
    }
}

gf_status GF_CALL docking_activate_panel(gf_object workspace, gf_utf8_view panel) noexcept {
    try {
        const ShadowFrameScope shadow_frame(activate_panel_slot.table_id, activate_panel_slot.slot_index);
        WorkspacePtr typed;
        const gf_status entered = enter_workspace(workspace, activate_panel_slot, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        DockPanelId id;
        const gf_status read = read_registered_panel(*typed, panel, activate_panel_slot, id);
        if (read != GF_STATUS_OK) {
            return read;
        }
        if (!(*typed).activate_panel(id, DockActivationCause::programmatic)) {
            return fail(GF_STATUS_INVALID_STATE, activate_panel_slot, "the panel is not placed where it can activate");
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(activate_panel_slot);
    }
}

gf_status GF_CALL docking_attach_windows(gf_object workspace) noexcept {
    try {
        const ShadowFrameScope shadow_frame(attach_windows_slot.table_id, attach_windows_slot.slot_index);
        WorkspacePtr typed;
        const gf_status entered = enter_workspace(workspace, attach_windows_slot, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        if ((*typed).windows_attached()) {
            return GF_STATUS_OK;
        }
        gf_object root = 0U;
        ApplicationWindowHandle window;
        const gf_status running = registry().current_run(attach_windows_slot, root, window);
        if (running != GF_STATUS_OK) {
            return running;
        }
        if (!window.active()) {
            return fail(GF_STATUS_INVALID_STATE, attach_windows_slot, "the window is not ready yet");
        }
        // The running window's stable id is "main" (Application::run's id),
        // which the floating windows name as their owner.
        const std::string main_id = "main";
        if (!(*typed).attach_windows(std::move(window), main_id)) {
            return fail(GF_STATUS_UNSUPPORTED_CAPABILITY, attach_windows_slot,
                        "the host has no runtime windows, or the workspace refused the window");
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(attach_windows_slot);
    }
}

gf_status GF_CALL docking_document_area(gf_object workspace, std::uint32_t* node) noexcept {
    try {
        const ShadowFrameScope shadow_frame(document_area_slot.table_id, document_area_slot.slot_index);
        if (node == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, document_area_slot, "document_area needs a node output");
        }
        WorkspacePtr typed;
        const gf_status entered = enter_workspace(workspace, document_area_slot, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        *node = (*typed).layout().document_area().value;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(document_area_slot);
    }
}

gf_status GF_CALL docking_panel_at(gf_object workspace, std::uint32_t stack, std::uint64_t index, char* buffer,
                                   std::uint64_t capacity, std::uint64_t* required) noexcept {
    try {
        const ShadowFrameScope shadow_frame(panel_at_slot.table_id, panel_at_slot.slot_index);
        WorkspacePtr typed;
        const gf_status entered = enter_workspace(workspace, panel_at_slot, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const DockNode* const node = (*typed).layout().find_node(DockNodeId{stack});
        if (node == nullptr ||
            ((*node).kind != DockNodeKind::tool_stack && (*node).kind != DockNodeKind::document_stack)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, panel_at_slot, "the node is not a stack");
        }
        if (index >= (*node).panels.size()) {
            return fail(GF_STATUS_NOT_FOUND, panel_at_slot, "the index is past the stack's panels");
        }
        // Copied before the text is written: the buffer is the caller's.
        const std::string id = (*node).panels[static_cast<std::size_t>(index)].value;
        return write_text(id, buffer, capacity, required, panel_at_slot);
    } catch (...) {
        return fail_with_current_exception(panel_at_slot);
    }
}

gf_status GF_CALL docking_floating_sites(gf_object workspace, std::uint32_t* sites, std::uint64_t capacity,
                                         std::uint64_t* count) noexcept {
    try {
        const ShadowFrameScope shadow_frame(floating_sites_slot.table_id, floating_sites_slot.slot_index);
        if (count == nullptr || (sites == nullptr && capacity != 0U)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, floating_sites_slot,
                        "floating_sites needs a count output, and sites when capacity is not zero");
        }
        WorkspacePtr typed;
        const gf_status entered = enter_workspace(workspace, floating_sites_slot, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const std::vector<DockFloatingSite>& floating = (*typed).layout().state().floating_sites;
        *count = floating.size();
        if (floating.size() > capacity) {
            return fail(GF_STATUS_BUFFER_TOO_SMALL, floating_sites_slot, "the sites array is too small");
        }
        for (std::size_t index = 0U; index < floating.size(); ++index) {
            sites[index] = floating[index].id.value;
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(floating_sites_slot);
    }
}

gf_status GF_CALL docking_floating_site(gf_object workspace, std::uint32_t site, gf_logical_rect* frame,
                                        std::uint32_t* first_stack) noexcept {
    try {
        const ShadowFrameScope shadow_frame(floating_site_slot.table_id, floating_site_slot.slot_index);
        if (frame == nullptr || first_stack == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, floating_site_slot, "floating_site needs its outputs");
        }
        WorkspacePtr typed;
        const gf_status entered = enter_workspace(workspace, floating_site_slot, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const DockLayout& layout = (*typed).layout();
        const DockFloatingSite* const found = layout.find_floating_site(DockSiteId{site});
        if (found == nullptr) {
            return fail(GF_STATUS_NOT_FOUND, floating_site_slot, "no floating site of that number");
        }
        // The first stack in tree order: down the first children.
        DockNodeId stack = (*found).root;
        const DockNode* node = layout.find_node(stack);
        while (node != nullptr && (*node).kind == DockNodeKind::split) {
            stack = (*node).first;
            node = layout.find_node(stack);
        }
        const bool is_stack = node != nullptr && ((*node).kind == DockNodeKind::tool_stack ||
                                                  (*node).kind == DockNodeKind::document_stack);
        const Rect bounds = (*found).frame;
        gf_logical_rect made{};
        made.x = bounds.x;
        made.y = bounds.y;
        made.width = bounds.width;
        made.height = bounds.height;
        *frame = made;
        *first_stack = is_stack ? stack.value : 0U;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(floating_site_slot);
    }
}

gf_status GF_CALL docking_update_panel(gf_object workspace, const gf_dock_panel* panel) noexcept {
    try {
        const ShadowFrameScope shadow_frame(update_panel_slot.table_id, update_panel_slot.slot_index);
        WorkspacePtr typed;
        const gf_status entered = enter_workspace(workspace, update_panel_slot, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        DockPanelDescriptor descriptor{};
        std::string title;
        const gf_status read = read_descriptor(panel, update_panel_slot, descriptor, title);
        if (read != GF_STATUS_OK) {
            return read;
        }
        const DockPanelRegistration* const registered = (*typed).find_registration(descriptor.id);
        if (registered == nullptr) {
            return fail(GF_STATUS_NOT_FOUND, update_panel_slot, "no panel of that id is registered");
        }
        if ((*registered).descriptor.kind != descriptor.kind) {
            return fail(GF_STATUS_INVALID_ARGUMENT, update_panel_slot, "a panel keeps the kind it was registered with");
        }
        // The group is not in the record: the registered one stays.
        descriptor.group = (*registered).descriptor.group;
        const DockPanelId id = descriptor.id;
        if (!(*typed).update_panel(descriptor)) {
            return fail(GF_STATUS_INVALID_STATE, update_panel_slot, "the workspace refused the descriptor");
        }
        static_cast<void>((*typed).set_panel_title(id, std::move(title)));
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(update_panel_slot);
    }
}

gf_status GF_CALL docking_set_options(gf_object workspace, const gf_dock_options* options) noexcept {
    try {
        const ShadowFrameScope shadow_frame(set_options_slot.table_id, set_options_slot.slot_index);
        if (options == nullptr || (*options).struct_size < options_size_1_1 || (*options).reserved != 0U ||
            ((*options).flags & ~GF_DOCK_OPTION_ALL) != 0U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, set_options_slot,
                        "an options record has at least the 1.1 size, known flags and reserved zero");
        }
        WorkspacePtr typed;
        const gf_status entered = enter_workspace(workspace, set_options_slot, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const gf_dock_option_flags flags = (*options).flags;
        (*typed).set_floats_shown_while_inactive((flags & GF_DOCK_OPTION_FLOATS_SHOWN_WHILE_INACTIVE) != 0U);
        (*typed).set_document_tabs_shown((flags & GF_DOCK_OPTION_DOCUMENT_TABS_HIDDEN) == 0U);
        (*typed).set_auto_hide_opens_on_hover((flags & GF_DOCK_OPTION_AUTO_HIDE_NO_HOVER) == 0U);
        std::optional<Color> area{};
        if ((flags & GF_DOCK_OPTION_AREA_BACKGROUND) != 0U) {
            const std::uint32_t rgba = (*options).area_background;
            area = Color::rgba(static_cast<std::uint8_t>(rgba >> 24U), static_cast<std::uint8_t>(rgba >> 16U),
                               static_cast<std::uint8_t>(rgba >> 8U), static_cast<std::uint8_t>(rgba));
        }
        (*typed).set_area_background(area);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(set_options_slot);
    }
}

gf_status GF_CALL docking_set_floating_window_visibility(gf_object workspace, std::uint32_t site,
                                                         gf_dock_window_visibility visibility) noexcept {
    try {
        const ShadowFrameScope shadow_frame(set_floating_window_visibility_slot.table_id,
                                            set_floating_window_visibility_slot.slot_index);
        if (visibility > GF_DOCK_WINDOW_VISIBILITY_POLICY) {
            return fail(GF_STATUS_INVALID_ARGUMENT, set_floating_window_visibility_slot,
                        "a visibility is WORKSPACE, SHOWN or POLICY");
        }
        WorkspacePtr typed;
        const gf_status entered = enter_workspace(workspace, set_floating_window_visibility_slot, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        if ((*typed).layout().find_floating_site(DockSiteId{site}) == nullptr) {
            return fail(GF_STATUS_NOT_FOUND, set_floating_window_visibility_slot, "no floating site of that number");
        }
        std::optional<bool> shown{};
        if (visibility == GF_DOCK_WINDOW_VISIBILITY_SHOWN) shown = true;
        if (visibility == GF_DOCK_WINDOW_VISIBILITY_POLICY) shown = false;
        (*typed).set_floating_window_shown_while_inactive(DockSiteId{site}, shown);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(set_floating_window_visibility_slot);
    }
}

const gf_docking_table table{
    .struct_size = static_cast<std::uint32_t>(sizeof(gf_docking_table)),
    .abi_minor = GF_ABI_MINOR,
    .register_panel = &docking_register_panel,
    .set_panel_content = &docking_set_panel_content,
    .set_panel_title = &docking_set_panel_title,
    .apply_edit = &docking_apply_edit,
    .locate = &docking_locate,
    .save_layout = &docking_save_layout,
    .load_layout = &docking_load_layout,
    .activate_panel = &docking_activate_panel,
    .attach_windows = &docking_attach_windows,
    .document_area = &docking_document_area,
    .panel_at = &docking_panel_at,
    .floating_sites = &docking_floating_sites,
    .floating_site = &docking_floating_site,
    .update_panel = &docking_update_panel,
    .set_options = &docking_set_options,
    .set_floating_window_visibility = &docking_set_floating_window_visibility,
};

} // namespace

const gf_docking_table& docking_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
