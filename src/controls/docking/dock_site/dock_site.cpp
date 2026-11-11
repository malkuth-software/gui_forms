#include "gui_forms/controls/docking/dock_site/dock_site.hpp"
#include "dock_site_children.hpp"

#include "dock_site_internal.hpp"
#include "../../drag_gesture/drag_gesture.hpp"

#include "gui_forms/delegate.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

// Geometry in logical units; the caption scales with the text, the seams
// (dock_site_detail::seam_bar_thickness) do not.
// The seam's hit band reaches this far past the bar on each side.
constexpr double seam_hit_extension = 3.0;
// The grip's length along the bar.
constexpr double seam_grip_length = 24.0;
// The caption strip (DK-41's look studies: 21 units, a 7-unit identity key
// 7 units in, 16-unit buttons 1 unit apart, 3 units from the end).
constexpr double caption_strip_height = 21.0;
constexpr double key_side = 7.0;
constexpr double key_inset = 7.0;
constexpr double key_gap = 6.0;
constexpr double button_side = 16.0;
constexpr double button_gap = 1.0;
constexpr double button_end_inset = 3.0;
constexpr double glyph_side = 9.0;
constexpr double label_gap = 4.0;
// The active caption's accent line (DK-41, T4-3).
constexpr double accent_height = 2.0;
constexpr double extent_tolerance = 1e-9;
// The drag's motions, run on the window's motion clock: the stamp's is the
// shared drag curve (drag_gesture.hpp, 220 ms, D1 as corrected); the tiles
// fade in over about 100 ms (T2-4).
constexpr std::chrono::milliseconds tile_fade_duration{100};

// In DockPositionCommand's order.
constexpr std::array<std::string_view, 9> position_names{
    "float", "dock", "dock-as-document", "auto-hide", "hide", "close", "move", "close-all-documents",
    "close-other-documents",
};
constexpr std::array<std::string_view, 9> position_texts{
    "Float", "Dock",  "Dock as Tabbed Document", "Auto Hide", "Hide", "Close", "Move", "Close All Documents",
    "Close Other Documents",
};

using dock_site_detail::centred;
using dock_site_detail::contains_node;
using dock_site_detail::first_focusable;
using dock_site_detail::focus_candidate;
using dock_site_detail::moved;
using dock_site_detail::seam_bar_thickness;
using dock_site_detail::strip_extent;
using dock_site_detail::to_window;

std::size_t command_index(DockPositionCommand command) noexcept {
    const std::size_t index = static_cast<std::size_t>(command);
    return index;
}

// The order tool stacks collapse in: lower collapse priority first; on equal
// priority the stack later in tree order first.
struct CollapseCandidate final {
    DockNodeId stack{};
    std::int32_t priority{};
    std::size_t order{};
};

struct CollapsesEarlier final {
    [[nodiscard]] bool operator()(const CollapseCandidate& left, const CollapseCandidate& right) const noexcept {
        if (left.priority != right.priority) return left.priority < right.priority;
        const bool later = left.order > right.order;
        return later;
    }
};

} // namespace

// -- construction --------------------------------------------------------------

DockSite::DockSite(StableId stable_id, DockWorkspace& workspace)
    : DockSite(std::move(stable_id), workspace, DockSiteId{}) {}

DockSite::DockSite(StableId stable_id, DockWorkspace& workspace, DockSiteId floating_site)
    : Control(std::move(stable_id)), workspace_(&workspace), floating_site_(floating_site),
      stamp_motion_(*this, drag_gesture::stamp_curve(), 1.0),
      tile_fade_(*this, MotionCurve::timed(tile_fade_duration, MotionEasing::linear), 1.0),
      flyout_slide_(*this, MotionCurve::timed(flyout_slide_duration, MotionEasing::linear), 1.0) {
    cursor = CursorKind::arrow;
    set_paint_plane(PaintPlane::control);
    stamp_motion_.set_listener(MotionListener::bind<DockSite, &DockSite::on_drag_motion>(*this));
    tile_fade_.set_listener(MotionListener::bind<DockSite, &DockSite::on_drag_motion>(*this));
    flyout_slide_.set_listener(MotionListener::bind<DockSite, &DockSite::on_slide_motion>(*this));
    const std::string prefix((*this).stable_id().value());
    menu_ = std::make_unique<ContextMenu>(prefix + ".position-menu");
    menu_invoked_ = (*menu_).item_invoked().subscribe(
        *this, Delegate<const MenuItemInvocation&>::bind<DockSite, &DockSite::on_menu_item_invoked>(*this));
    menu_changed_ = (*menu_).open_changed().subscribe(
        *this, Delegate<bool>::bind<DockSite, &DockSite::on_menu_open_changed>(*this));
    for (std::size_t index = 0; index < menu_commands_.size(); ++index) {
        menu_commands_[index] = std::make_shared<Command>(prefix + ".position." + std::string(position_names[index]),
                                                          std::string(position_texts[index]));
    }
    // The workspace tells the site of each layout change.
    (*workspace_).attach_site(*this);
    activation_token_ = (*workspace_).activation_changed().subscribe(
        *this, Delegate<const DockActivationChange&>::bind<DockSite, &DockSite::on_activation_changed>(*this));
}

void DockSite::initialize_control_tree() {
    create_drag_layer();
    realize();
}

DockSite::~DockSite() {
    capture_token_.disconnect();
    active_token_.disconnect();
    focus_token_.disconnect();
    window_pointer_.disconnect();
    root_pointer_.disconnect();
    navigator_revoked_.disconnect();
    navigator_token_.disconnect();
    hover_timer_.disconnect();
    leave_timer_.disconnect();
    stale_timer_.disconnect();
    flyout_revoked_.disconnect();
    release_flyout_control();
    flyout_token_.disconnect();
    release_side_strips();
    if (menu_) (*menu_).close();
    activation_token_.disconnect();
    for (StackSlot& slot : stacks_) {
        if (slot.view) (*slot.view).release_site();
    }
    if (drag_layer_) (*drag_layer_).release_site();
    if (workspace_ != nullptr) (*workspace_).detach_site(*this);
    workspace_ = nullptr;
}

void DockSite::on_dispose() noexcept {
    capture_token_.disconnect();
    active_token_.disconnect();
    reset_drag();
    close_flyout(DockFlyoutCause::programmatic);
    close_navigator();
    close_position_menu();
    root_pointer_.disconnect();
    if (menu_) (*menu_).dispose();
    focus_token_.disconnect();
    activation_token_.disconnect();
    if (workspace_ != nullptr) (*workspace_).detach_site(*this);
    workspace_ = nullptr;
    for (StackSlot& slot : stacks_) {
        if (slot.view) (*slot.view).release_site();
    }
    if (drag_layer_) (*drag_layer_).release_site();
    Control::on_dispose();
}

void DockSite::release_workspace() noexcept {
    close_flyout(DockFlyoutCause::programmatic);
    close_navigator();
    close_position_menu();
    activation_token_.disconnect();
    workspace_ = nullptr;
    try {
        for (StackSlot& slot : stacks_) remove_slot(slot);
        stacks_.clear();
        seams_.clear();
        collapsed_.clear();
        sync_side_strips();
        if (is_alive()) {
            focusable = false;
            invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
        }
    } catch (...) {
    }
}

void DockSite::set_document_area_maximized(bool maximized) {
    require_mutable();
    if (maximized_ == maximized) return;
    maximized_ = maximized;
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// -- realization ---------------------------------------------------------------

DockSite::StackSlot* DockSite::slot_for(DockNodeId stack) noexcept {
    for (StackSlot& slot : stacks_) {
        if (slot.stack == stack) return &slot;
    }
    return nullptr;
}

const DockSite::StackSlot* DockSite::slot_for(DockNodeId stack) const noexcept {
    for (const StackSlot& slot : stacks_) {
        if (slot.stack == stack) return &slot;
    }
    return nullptr;
}

std::size_t DockSite::slot_index_of(const Control& control) const {
    for (std::size_t index = 0; index < stacks_.size(); ++index) {
        const std::shared_ptr<StackView>& view = stacks_[index].view;
        if (!view) continue;
        if ((*view).runtime_id() == control.runtime_id() || (*view).contains(control)) return index;
    }
    return stacks_.size();
}

std::optional<DockPanelId> DockSite::selected_panel(const StackSlot& slot) const {
    if (!slot.view) return std::nullopt;
    const std::optional<PageId>& selected = (*slot.view).pages().selected_page();
    if (!selected) return std::nullopt;
    const DockPanelId panel((*selected).value);
    return panel;
}

std::shared_ptr<PageView> DockSite::stack_view(DockNodeId stack) const {
    const StackSlot* const slot = slot_for(stack);
    if (slot == nullptr) return {};
    const std::shared_ptr<PageView> view = (*slot).view;
    return view;
}

std::vector<DockNodeId> DockSite::realized_stacks() const {
    std::vector<DockNodeId> order{};
    order.reserve(stacks_.size());
    for (const StackSlot& slot : stacks_) order.push_back(slot.stack);
    return order;
}

std::vector<DockNodeId> DockSite::collapsed_stacks() const {
    const std::vector<DockNodeId> collapsed = collapsed_;
    return collapsed;
}

// The stacks of the main site's tree in tree order: a split's first child
// before its second, the document area's content in place.
namespace {

void collect_main_stacks(const DockLayout& layout, DockNodeId node_id, std::vector<DockNodeId>& out) {
    const DockNode* const node = layout.find_node(node_id);
    if (node == nullptr) return;
    if ((*node).kind == DockNodeKind::split) {
        collect_main_stacks(layout, (*node).first, out);
        collect_main_stacks(layout, (*node).second, out);
        return;
    }
    if ((*node).kind == DockNodeKind::document_area) {
        if (!((*node).first == DockNodeId{})) collect_main_stacks(layout, (*node).first, out);
        return;
    }
    out.push_back(node_id);
}

} // namespace

void DockSite::remove_slot(StackSlot& slot) noexcept {
    if (!slot.view) return;
    (*slot.view).release_site();
    try {
        // A view is the site's child, or the flyout's while it is out.
        const Control::Ptr parent = (*slot.view).parent();
        if (parent) static_cast<void>((*parent).remove_child((*slot.view).runtime_id()));
    } catch (...) {
    }
    slot.view.reset();
    slot.in_flyout = false;
}

DockNodeId DockSite::site_root() const {
    if (workspace_ == nullptr) return DockNodeId{};
    const DockLayout& layout = (*workspace_).layout();
    if (!floating()) return layout.state().main_root;
    const DockFloatingSite* const site = layout.find_floating_site(floating_site_);
    if (site == nullptr) return DockNodeId{};
    return (*site).root;
}

// Brings the views in line with the site's tree: one per stack in tree
// order, reused by node identity, each holding its stack's panels.
void DockSite::realize() {
    if (workspace_ == nullptr || !is_alive()) return;
    const DockLayout& layout = (*workspace_).layout();
    std::vector<DockNodeId> order{};
    collect_main_stacks(layout, site_root(), order);
    // In the main site the auto-hidden stacks follow the tree's, in layout
    // order.
    const std::size_t tree_count = order.size();
    if (!floating()) {
        for (const DockAutoHideEntry& entry : layout.state().auto_hidden) order.push_back(entry.stack);
    }
    std::vector<StackSlot> next{};
    next.reserve(order.size());
    for (std::size_t position = 0; position < order.size(); ++position) {
        const DockNodeId stack = order[position];
        const bool auto_hidden = position >= tree_count;
        StackSlot* const existing = slot_for(stack);
        if (existing != nullptr) {
            next.push_back(std::move(*existing));
            (*existing).view.reset();
            next.back().auto_hidden = auto_hidden;
            if (auto_hidden) {
                next.back().collapsed = true;
                next.back().side_tabbed = false;
                next.back().edge = layout.state().auto_hidden[position - tree_count].edge;
            }
            continue;
        }
        const DockNode& node = *layout.find_node(stack);
        StackSlot slot{};
        slot.stack = stack;
        slot.kind = node.kind;
        slot.auto_hidden = auto_hidden;
        if (auto_hidden) {
            slot.collapsed = true;
            slot.edge = layout.state().auto_hidden[position - tree_count].edge;
        }
        slot.view = make_control<StackView>(StableId(stack_identity(stack)), *this, node.kind);
        PageView& view = *slot.view;
        PageStrip& strip = *view.strip();
        strip.mnemonics_enabled = false;
        if (node.kind == DockNodeKind::tool_stack) strip.placement = PageStripPlacement::bottom;
        // Documents reorder by dragging along their strip (owner decision
        // DK-39) and tear off when the pointer leaves it; a tool tab's drag
        // is the docking drag at once (phase D3).
        if (node.kind == DockNodeKind::document_stack) strip.reorder_enabled = true;
        // The workspace's navigator takes Ctrl+Tab (owner decision DK-30).
        view.ctrl_tab = PageViewCtrlTab::pass;
        on(view.pages().selected_page_changed(), *this, &DockSite::on_view_selection, stack);
        on(view.content_requested(), *this, &DockSite::on_view_content, stack);
        on(strip.page_close_requested(), *this, &DockSite::on_view_close, stack);
        on(strip.page_drag_started(), *this, &DockSite::on_tab_drag, stack);
        add_child(slot.view);
        // An auto-hidden stack's view waits, collapsed, until its flyout
        // takes it.
        if (auto_hidden) set_child_layout_collapsed(slot.view, true);
        next.push_back(std::move(slot));
    }
    for (StackSlot& slot : stacks_) remove_slot(slot);
    stacks_ = std::move(next);
    std::size_t child_index = 0U;
    for (StackSlot& slot : stacks_) {
        if (!slot.in_flyout) {
            static_cast<void>(set_child_index_from_top((*slot.view).runtime_id(), child_index));
            ++child_index;
        }
        sync_pages(slot, *layout.find_node(slot.stack));
    }
    // The drag layer paints over every stack.
    if (drag_layer_) (*drag_layer_).bring_to_front();
    sync_side_strips();
    update_focus_regions();
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void DockSite::sync_pages(StackSlot& slot, const DockNode& node) {
    PageView& view = *slot.view;
    PageSet& pages = view.pages();
    syncing_ = true;
    {
        PageSetBatch batch(pages);
        // Pages whose panels left the stack go first, from the end.
        std::size_t position = pages.count();
        while (position > 0U) {
            --position;
            const PageId page = pages.record_at(PagePosition{position}).id;
            const DockPanelId panel(page.value);
            if (std::find(node.panels.begin(), node.panels.end(), panel) == node.panels.end() ||
                dragged_out(slot.stack, panel)) {
                static_cast<void>(pages.remove(page));
            }
        }
        // While a tab drag holds one panel out of the stack, the pages after
        // it move up one place.
        std::size_t shown = 0U;
        for (std::size_t index = 0; index < node.panels.size(); ++index) {
            const DockPanelId& panel = node.panels[index];
            if (dragged_out(slot.stack, panel)) continue;
            const std::size_t place_index = shown;
            ++shown;
            const PageId page{panel.value};
            const std::string title = (*workspace_).panel_title(panel);
            const std::optional<PagePosition> found = pages.find(page);
            if (!found) {
                PageRecord record{};
                record.id = page;
                record.title = title;
                record.closable = node.kind == DockNodeKind::document_stack && tab_closable(panel);
                static_cast<void>(view.add_page(record, (*workspace_).panel_content(panel),
                                                PagePlacement::at(PagePosition{place_index})));
                continue;
            }
            if ((*found).value != place_index) static_cast<void>(pages.move(page, PagePosition{place_index}));
            if (pages.record_at(PagePosition{place_index}).title != title) {
                static_cast<void>(pages.set_title(page, title));
            }
            // A descriptor updated after the page was made (update_panel).
            const bool closable = node.kind == DockNodeKind::document_stack && tab_closable(panel);
            if (pages.record_at(PagePosition{place_index}).closable != closable) {
                static_cast<void>(pages.set_closable(page, closable));
            }
        }
        if (!node.panels.empty() && node.selected < node.panels.size() &&
            !dragged_out(slot.stack, node.panels[node.selected])) {
            const PageId selected{node.panels[node.selected].value};
            static_cast<void>(pages.select(selected, PageSelectionCause::programmatic));
        }
        batch.close();
    }
    syncing_ = false;
    const std::optional<DockPanelId> panel = selected_panel(slot);
    slot.title = panel ? (*workspace_).panel_title(*panel) : std::string{};
    // A tool stack shows its tabs only with two or more panels; behind side
    // tabs and in the flyout, never (the side tabs choose the panel).
    // A document stack shows its tabs unless the workspace is a single
    // document interface (DockWorkspace::document_tabs_shown).
    const bool document_tabs = node.kind == DockNodeKind::document_stack && (*workspace_).document_tabs_shown();
    const bool strip_shown = (document_tabs || (node.kind == DockNodeKind::tool_stack && pages.count() >= 2U)) &&
                             !slot.auto_hidden && !slot.in_flyout;
    PageStrip& strip = *view.strip();
    if (strip.visible.read() != strip_shown) {
        strip.visible = strip_shown;
        view.invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test);
    }
}

// The selected panel of a stack being shown gets its content, created by
// its factory the first time (lazy creation).
void DockSite::supply_content(StackSlot& slot) {
    if (workspace_ == nullptr || !slot.view || (slot.collapsed && !slot.in_flyout)) return;
    const std::optional<DockPanelId> panel = selected_panel(slot);
    if (!panel) return;
    const PageId page{(*panel).value};
    if ((*slot.view).page_content(page)) return;
    const Control::Ptr content = (*workspace_).create_panel_content(*panel);
    if (!content) return;
    static_cast<void>((*slot.view).set_page_content(page, content));
}

// The stacks are the window's F6 regions in tree order, where the site's
// regions were before (or at the end); the program's other regions keep
// their places.
void DockSite::update_focus_regions() {
    Window* const host = attached_window();
    if (host == nullptr) return;
    std::vector<Control::Ptr> current = (*host).focus_regions();
    std::vector<Control::Ptr> regions{};
    regions.reserve(current.size() + stacks_.size());
    std::optional<std::size_t> insert_at{};
    for (const Control::Ptr& region : current) {
        bool ours = false;
        for (const Control::WeakPtr& registered : registered_regions_) {
            if (registered.lock() == region) ours = true;
        }
        if (ours) {
            if (!insert_at) insert_at = regions.size();
            continue;
        }
        regions.push_back(region);
    }
    const std::size_t at = insert_at.value_or(regions.size());
    std::vector<Control::Ptr> ours{};
    ours.reserve(stacks_.size() + sides_.size());
    for (const StackSlot& slot : stacks_) {
        if (slot.view && !slot.auto_hidden) ours.push_back(slot.view);
    }
    // Then each side strip that has tabs.
    for (const SideStrip& entry : sides_) {
        if (entry.strip && !entry.panels.empty()) ours.push_back(entry.strip);
    }
    regions.insert(regions.begin() + static_cast<std::ptrdiff_t>(at), ours.begin(), ours.end());
    try {
        (*host).set_focus_regions(std::move(regions));
    } catch (const std::invalid_argument&) {
        // The program made the site (or an ancestor) a region itself; its
        // stacks then stay out of the list.
        return;
    }
    registered_regions_.clear();
    for (const Control::Ptr& view : ours) registered_regions_.push_back(view);
}

namespace {

bool focus_within(const Control& site, Window* host) {
    if (host == nullptr) return false;
    const Control::Ptr focused = (*host).focused_control();
    if (!focused) return false;
    const bool inside = (*focused).runtime_id() == site.runtime_id() || site.contains(*focused);
    return inside;
}

} // namespace

void DockSite::on_layout_changed(const DockLayoutChange&) {
    // The program changed the layout under a drag: the drag ends, never
    // applied to a stale tree (docking study 5.1, invariant 2). A drop's own
    // transaction runs while settling. A drag this site only shows ends
    // with its own site.
    if (drag_.guest) {
        leave_guest_drag();
    } else if (drag_.phase == DockDragPhase::armed || drag_.phase == DockDragPhase::dragging) {
        end_drag(DockDragEnd::layout_changed);
    }
    const bool focus_was_inside = focus_within(*this, attached_window());
    // The flyout stays only while its panel is still in the same stack and
    // that stack is still auto-hidden or docked (a collapsed stack's flyout
    // is checked again by the next layout pass).
    if (flyout_panel_ && workspace_ != nullptr) {
        const DockPanelLocation location = (*workspace_).layout().locate(*flyout_panel_);
        const StackSlot* const slot = slot_for(flyout_stack_);
        const bool was_auto_hidden = slot != nullptr && (*slot).auto_hidden;
        const bool still_auto_hidden = location.placement == DockPlacementKind::auto_hidden && was_auto_hidden;
        const bool still_collapsed = location.placement == DockPlacementKind::docked && !was_auto_hidden;
        const bool kept = location.stack == flyout_stack_ && (still_auto_hidden || still_collapsed);
        if (!kept) close_flyout(DockFlyoutCause::layout);
    }
    realize();
    if (flyout_panel_) {
        StackSlot* const slot = slot_for(flyout_stack_);
        if (slot != nullptr && (*slot).in_flyout) {
            syncing_ = true;
            static_cast<void>((*(*slot).view).pages().select(PageId{(*flyout_panel_).value}, PageSelectionCause::programmatic));
            syncing_ = false;
            (*slot).title = (*workspace_).panel_title(*flyout_panel_);
            supply_content(*slot);
            place_flyout();
        }
    }
    refocus_if_lost(focus_was_inside);
}

// Focus was in the site and left it with a stack (a panel hidden, closed,
// moved or collapsed): it goes to the active panel, else the active
// document, else the first stack shown.
void DockSite::refocus_if_lost(bool focus_was_inside) {
    if (!focus_was_inside || workspace_ == nullptr) return;
    Window* const host = attached_window();
    if (host == nullptr || focus_within(*this, host)) return;
    const std::array<std::optional<DockPanelId>, 2> candidates{(*workspace_).active_panel(),
                                                               (*workspace_).active_document()};
    for (const std::optional<DockPanelId>& candidate : candidates) {
        if (!candidate) continue;
        const DockPanelLocation location = (*workspace_).layout().locate(*candidate);
        const StackSlot* const slot = slot_for(location.stack);
        if (slot == nullptr || (*slot).collapsed) continue;
        static_cast<void>((*workspace_).activate_panel(*candidate, DockActivationCause::fallback));
        return;
    }
    for (const StackSlot& slot : stacks_) {
        if (slot.collapsed) continue;
        const std::optional<DockPanelId> panel = selected_panel(slot);
        if (!panel) continue;
        static_cast<void>((*workspace_).activate_panel(*panel, DockActivationCause::fallback));
        return;
    }
}

void DockSite::on_view_selection(DockNodeId stack, const PageSelectionChange& change) {
    if (syncing_ || workspace_ == nullptr || !change.new_page) return;
    if (change.cause == PageSelectionCause::programmatic || change.cause == PageSelectionCause::first_page ||
        change.cause == PageSelectionCause::fallback) {
        return;
    }
    // The user chose a tab: the layout records the selection.
    const DockPanelId panel((*change.new_page).value);
    DockTransaction transaction{};
    transaction.origin = DockEditOrigin::user;
    transaction.edits.push_back(DockEdit::select(panel));
    static_cast<void>((*workspace_).apply(transaction));
    // A stack that held the active panel now shows another: it becomes
    // active (focus, if it moved, says the same).
    const std::optional<DockPanelId>& active = (*workspace_).active_panel();
    if (change.old_page && active && (*active).value == (*change.old_page).value) {
        (*workspace_).note_active(panel, DockActivationCause::keyboard);
    }
    StackSlot* const slot = slot_for(stack);
    if (slot != nullptr) {
        (*slot).title = (*workspace_).panel_title(panel);
        invalidate(Dirty::paint | Dirty::semantics);
    }
}

void DockSite::on_view_content(DockNodeId stack, const PageContentRequest&) {
    if (syncing_) return;
    StackSlot* const slot = slot_for(stack);
    if (slot != nullptr) supply_content(*slot);
}

// A document tab's close (the accessibility action until the document
// preset's glyph arrives): the workspace closes the document.
void DockSite::on_view_close(DockNodeId, PageCloseRequest& request) {
    if (workspace_ == nullptr || request.handled) return;
    const DockPanelId panel(request.page.value);
    if (!position_command_enabled(panel, DockPositionCommand::close)) return;
    request.handled = true;
    static_cast<void>(run_position_command(panel, DockPositionCommand::close));
}

void DockSite::on_activation_changed(const DockActivationChange&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

// Focus entered a stack: its selected panel is the active panel, and a
// control in the panel's content is remembered for its next activation.
void DockSite::on_focus_moved(const FocusedControlChange&) {
    if (workspace_ == nullptr) return;
    Window* const host = attached_window();
    if (host == nullptr) return;
    const Control::Ptr focused = (*host).focused_control();
    const bool in_flyout = focus_in_flyout();
    bool on_side_strip = false;
    for (const SideStrip& entry : sides_) {
        if (focused && entry.strip && (*entry.strip).runtime_id() == (*focused).runtime_id()) on_side_strip = true;
    }
    if (focused && !in_flyout && !on_side_strip) focus_outside_ = focused;
    if (flyout_panel_ && !flyout_closing_) {
        if (in_flyout) {
            flyout_focused_ = true;
        } else if (on_side_strip && flyout_focus_pending_) {
            // A side tab's press opened the flyout and the strip then took
            // focus (PageStrip's order): focus goes into the flyout.
            flyout_focus_pending_ = false;
            focus_into_flyout();
            return;
        } else if (flyout_focused_ && !on_side_strip && !menu_from_flyout_ && !position_menu_open()) {
            // Focus left a flyout that had it (a side strip keeps it out);
            // where it went is noted below as for any focus move.
            close_flyout(DockFlyoutCause::focus_lost);
        }
    }
    if (!focused) return;
    const std::size_t index = slot_index_of(*focused);
    if (index >= stacks_.size()) return;
    const std::optional<DockPanelId> panel = selected_panel(stacks_[index]);
    if (!panel) return;
    (*workspace_).note_active(*panel, DockActivationCause::focus);
    // Only a control in the panel's content is remembered (not the stack's
    // tabs), so activation returns into the panel itself.
    const Control::Ptr content = (*stacks_[index].view).page_content(PageId{(*panel).value});
    const bool in_content =
        content && ((*content).runtime_id() == (*focused).runtime_id() || (*content).contains(*focused));
    if (!in_content) return;
    bool remembered = false;
    for (RememberedFocus& entry : remembered_focus_) {
        if (!(entry.panel == *panel)) continue;
        entry.control = focused;
        remembered = true;
    }
    if (!remembered) {
        RememberedFocus entry{};
        entry.panel = *panel;
        entry.control = focused;
        remembered_focus_.push_back(std::move(entry));
    }
}

// Moves focus into a panel's content: the control that last had focus
// there, else its first focusable control, else the stack's tabs.
void DockSite::focus_panel(const DockPanelId& panel) {
    Window* const host = attached_window();
    if (host == nullptr || workspace_ == nullptr) return;
    const DockPanelLocation location = (*workspace_).layout().locate(panel);
    StackSlot* const slot = slot_for(location.stack);
    if (slot == nullptr) return;
    // A panel behind a side tab opens in its flyout, which takes focus.
    if (!(*slot).in_flyout && ((*slot).auto_hidden || (*slot).side_tabbed)) {
        static_cast<void>(open_flyout(panel, DockFlyoutCause::programmatic));
        return;
    }
    if ((*slot).collapsed && !(*slot).in_flyout) return;
    supply_content(*slot);
    const PageId page{panel.value};
    for (const RememberedFocus& entry : remembered_focus_) {
        if (!(entry.panel == panel)) continue;
        const Control::Ptr control = entry.control.lock();
        if (control && focus_candidate(*control) && (*(*slot).view).contains(*control) &&
            (*host).request_focus(control)) {
            return;
        }
    }
    const Control::Ptr content = (*(*slot).view).page_content(page);
    if (content) {
        if (focus_candidate(*content) && (*host).request_focus(content)) return;
        const Control::Ptr inner = first_focusable(content);
        if (inner && (*host).request_focus(inner)) return;
    }
    const std::shared_ptr<PageStrip>& strip = (*(*slot).view).strip();
    if (strip && focus_candidate(*strip)) static_cast<void>((*host).request_focus(strip));
}

std::optional<DockPanelId> DockSite::focused_panel() const {
    const Window* const host = attached_window();
    if (host == nullptr) return std::nullopt;
    const Control::Ptr focused = (*host).focused_control();
    if (!focused) return std::nullopt;
    const std::size_t index = slot_index_of(*focused);
    if (index >= stacks_.size()) return std::nullopt;
    const std::optional<DockPanelId> panel = selected_panel(stacks_[index]);
    return panel;
}

bool DockSite::focus_in_tool_stack() const {
    const Window* const host = attached_window();
    if (host == nullptr) return false;
    const Control::Ptr focused = (*host).focused_control();
    if (!focused) return false;
    const std::size_t index = slot_index_of(*focused);
    const bool tool = index < stacks_.size() && stacks_[index].kind == DockNodeKind::tool_stack;
    return tool;
}

// -- attachment ----------------------------------------------------------------

void DockSite::on_attachment_committed() noexcept {
    Control::on_attachment_committed();
    Window* const host = attached_window();
    if (host == nullptr) return;
    try {
        if (followed_window_ != host) {
            focus_token_.disconnect();
            focus_token_ = (*host).focused_control_changed().subscribe(
                *this, Delegate<const FocusedControlChange&>::bind<DockSite, &DockSite::on_focus_moved>(*this));
            capture_token_.disconnect();
            capture_token_ = (*host).pointer_capture_changed().subscribe(
                *this, Delegate<const PointerCaptureChange&>::bind<DockSite, &DockSite::on_capture_changed>(*this));
            active_token_.disconnect();
            active_token_ = (*host).active_changed().subscribe(
                *this, Delegate<bool>::bind<DockSite, &DockSite::on_window_active>(*this));
            followed_window_ = host;
        }
        // The window's root reports every pointer event outside overlays:
        // hover over the side tabs, the pointer leaving a flyout, and presses
        // outside it.
        if (observed_root_window_ != host) {
            root_pointer_.disconnect();
            observed_root_window_ = nullptr;
            if (const Control::Ptr root = (*host).root()) {
                root_pointer_ = (*root).pointer_preview_observed().subscribe(
                    *this, Delegate<const PointerEvent&>::bind<DockSite, &DockSite::on_root_pointer>(*this));
                observed_root_window_ = host;
            }
        }
        update_focus_regions();
        if (workspace_ != nullptr) (*workspace_).publish_command_targets();
    } catch (...) {
    }
}

void DockSite::on_detaching_from_window(SurfaceRoot& former_window) noexcept {
    close_flyout(DockFlyoutCause::programmatic);
    close_navigator();
    close_position_menu();
    capture_token_.disconnect();
    active_token_.disconnect();
    // The stamp's picture is the former window's image. A release that
    // fails (allocation while marking the repaint) leaves the image to the
    // window's end.
    if (drag_.picture && !drag_.guest) {
        try {
            static_cast<void>(former_window.release_control_snapshot((*drag_.picture).image));
        } catch (...) {
        }
    }
    drag_.picture.reset();
    reset_drag();
    focus_token_.disconnect();
    followed_window_ = nullptr;
    root_pointer_.disconnect();
    observed_root_window_ = nullptr;
    try {
        std::vector<Control::Ptr> regions = former_window.focus_regions();
        std::vector<Control::Ptr> kept{};
        kept.reserve(regions.size());
        for (const Control::Ptr& region : regions) {
            bool ours = false;
            for (const Control::WeakPtr& registered : registered_regions_) {
                if (registered.lock() == region) ours = true;
            }
            if (!ours) kept.push_back(region);
        }
        former_window.set_focus_regions(std::move(kept));
    } catch (...) {
    }
    registered_regions_.clear();
    // The workspace's targets leave the window with the site.
    const std::array<CommandRouteStage, 2> stages{CommandRouteStage::active_panel, CommandRouteStage::active_document};
    for (const CommandRouteStage stage : stages) {
        try {
            former_window.set_command_target(stage, {});
        } catch (...) {
        }
    }
    Control::on_detaching_from_window(former_window);
}

// -- the flyout's view ---------------------------------------------------------

// The flyout takes the stack's view (it needs StackView, defined here); the
// rest of auto-hide is in dock_site_auto_hide.cpp.
void DockSite::move_view_to_flyout(StackSlot& slot) {
    const Control::Ptr view = slot.view;
    static_cast<void>(remove_child((*view).runtime_id()));
    slot.in_flyout = true;
    flyout_adopt(view);
    const std::shared_ptr<PageStrip>& strip = (*slot.view).strip();
    if (strip && (*strip).visible.read()) (*strip).visible = false;
    // The side tab chose the panel: the view shows it without a layout edit.
    syncing_ = true;
    static_cast<void>((*slot.view).pages().select(PageId{(*flyout_panel_).value}, PageSelectionCause::programmatic));
    syncing_ = false;
    slot.title = (*workspace_).panel_title(*flyout_panel_);
    supply_content(slot);
}

void DockSite::move_view_to_site(StackSlot& slot) {
    if (!slot.in_flyout || !flyout_root_ || !slot.view) return;
    const Control::Ptr view = (*flyout_root_).remove_child((*slot.view).runtime_id());
    slot.in_flyout = false;
    if (view) {
        add_child(view);
        set_child_layout_collapsed(view, true);
    }
    if (workspace_ == nullptr) return;
    const DockNode* const node = (*workspace_).layout().find_node(slot.stack);
    if (node != nullptr) sync_pages(slot, *node);
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// -- layout --------------------------------------------------------------------

double DockSite::caption_height() const {
    const double height = caption_strip_height * effective_text_scale();
    return height;
}

Size DockSite::stack_minimum(const DockNode& node) const {
    double width = 0.0;
    double height = 0.0;
    for (const DockPanelId& panel : node.panels) {
        const DockPanelDescriptor* const descriptor = (*workspace_).layout().find_descriptor(panel);
        if (descriptor == nullptr) continue;
        width = std::max(width, (*descriptor).minimum_size.width);
        height = std::max(height, (*descriptor).minimum_size.height);
    }
    if (node.kind == DockNodeKind::tool_stack) {
        height += caption_height();
        if (node.panels.size() >= 2U) height += strip_extent(*this);
    } else if ((*workspace_).document_tabs_shown()) {
        height += strip_extent(*this);
    }
    const Size minimum{width, height};
    return minimum;
}

DockSite::Extent DockSite::extent_of(DockNodeId node_id, std::span<const DockNodeId> collapsed) const {
    Extent extent{};
    const DockNode* const node = (*workspace_).layout().find_node(node_id);
    if (node == nullptr) return extent;
    if ((*node).kind == DockNodeKind::tool_stack) {
        if (contains_node(collapsed, node_id) || hidden_by_drag(node_id)) return extent;
        extent.present = true;
        extent.minimum = stack_minimum(*node);
        return extent;
    }
    if ((*node).kind == DockNodeKind::document_stack) {
        if (hidden_by_drag(node_id)) return extent;
        extent.present = true;
        extent.minimum = stack_minimum(*node);
        return extent;
    }
    if ((*node).kind == DockNodeKind::document_area) {
        // The document area is always there, empty or not (decision DK-19).
        extent.present = true;
        if (!((*node).first == DockNodeId{})) {
            const Extent content = extent_of((*node).first, collapsed);
            if (content.present) extent.minimum = content.minimum;
        }
        return extent;
    }
    const Extent first = extent_of((*node).first, collapsed);
    const Extent second = extent_of((*node).second, collapsed);
    if (!first.present) return second;
    if (!second.present) return first;
    extent.present = true;
    if ((*node).axis == DockAxis::horizontal) {
        extent.minimum.width = first.minimum.width + seam_bar_thickness + second.minimum.width;
        extent.minimum.height = std::max(first.minimum.height, second.minimum.height);
    } else {
        extent.minimum.width = std::max(first.minimum.width, second.minimum.width);
        extent.minimum.height = first.minimum.height + seam_bar_thickness + second.minimum.height;
    }
    return extent;
}

Size DockSite::minimum_size() const {
    if (workspace_ == nullptr) return Size{};
    const std::vector<DockNodeId> none{};
    const Extent extent = extent_of(site_root(), none);
    return extent.minimum;
}

// The tool stacks to collapse at `available` (decision DK-23): every tool
// stack while the document area is maximized; else, while the tree does not
// fit, the next stack in collapse order whose collapse reduces the shortfall.
std::vector<DockNodeId> DockSite::plan_collapse(Size available) const {
    std::vector<DockNodeId> collapsed{};
    // A floating window keeps its minimum size instead (phase D4).
    if (floating()) return collapsed;
    std::vector<CollapseCandidate> candidates{};
    candidates.reserve(stacks_.size());
    for (std::size_t order = 0; order < stacks_.size(); ++order) {
        const StackSlot& slot = stacks_[order];
        if (slot.kind != DockNodeKind::tool_stack || slot.auto_hidden) continue;
        const DockNode* const node = (*workspace_).layout().find_node(slot.stack);
        if (node == nullptr) continue;
        CollapseCandidate candidate{};
        candidate.stack = slot.stack;
        candidate.order = order;
        candidate.priority = std::numeric_limits<std::int32_t>::min();
        for (const DockPanelId& panel : (*node).panels) {
            const DockPanelDescriptor* const descriptor = (*workspace_).layout().find_descriptor(panel);
            const std::int32_t priority = descriptor == nullptr ? 0 : (*descriptor).collapse_priority;
            candidate.priority = std::max(candidate.priority, priority);
        }
        candidates.push_back(candidate);
    }
    if (maximized_) {
        for (const StackSlot& slot : stacks_) {
            if (slot.kind == DockNodeKind::tool_stack && !slot.auto_hidden) collapsed.push_back(slot.stack);
        }
        return collapsed;
    }
    const DockNodeId root = (*workspace_).layout().state().main_root;
    Extent extent = extent_of(root, collapsed);
    std::sort(candidates.begin(), candidates.end(), CollapsesEarlier{});
    for (const CollapseCandidate& candidate : candidates) {
        const bool wide = extent.minimum.width > available.width + extent_tolerance;
        const bool tall = extent.minimum.height > available.height + extent_tolerance;
        if (!wide && !tall) break;
        collapsed.push_back(candidate.stack);
        const Extent trial = extent_of(root, collapsed);
        const bool narrower = wide && trial.minimum.width < extent.minimum.width - extent_tolerance;
        const bool shorter = tall && trial.minimum.height < extent.minimum.height - extent_tolerance;
        if (!narrower && !shorter) {
            collapsed.pop_back();
            continue;
        }
        extent = trial;
    }
    return collapsed;
}

void DockSite::place(DockNodeId node_id, Rect bounds, std::span<const DockNodeId> collapsed) {
    const DockNode* const node = (*workspace_).layout().find_node(node_id);
    if (node == nullptr) return;
    if ((*node).kind == DockNodeKind::tool_stack || (*node).kind == DockNodeKind::document_stack) {
        StackSlot* const slot = slot_for(node_id);
        if (slot == nullptr) return;
        (*slot).bounds = bounds;
        if ((*node).kind == DockNodeKind::tool_stack) {
            (*slot).caption = {bounds.x, bounds.y, bounds.width, std::min(bounds.height, caption_height())};
        }
        return;
    }
    if ((*node).kind == DockNodeKind::document_area) {
        document_area_ = bounds;
        if ((*node).first == DockNodeId{}) return;
        // Content wholly held out by a drag leaves the area empty.
        if (extent_of((*node).first, collapsed).present) place((*node).first, bounds, collapsed);
        return;
    }
    const Extent first = extent_of((*node).first, collapsed);
    const Extent second = extent_of((*node).second, collapsed);
    if (!first.present) {
        place((*node).second, bounds, collapsed);
        return;
    }
    if (!second.present) {
        place((*node).first, bounds, collapsed);
        return;
    }
    const bool horizontal = (*node).axis == DockAxis::horizontal;
    const double extent = horizontal ? bounds.width : bounds.height;
    const double seam = seam_bar_thickness;
    const double span = std::max(0.0, extent - seam);
    const double first_minimum = horizontal ? first.minimum.width : first.minimum.height;
    const double second_minimum = horizontal ? second.minimum.width : second.minimum.height;
    const DockSplitSizes sizes = solve_dock_split(span, (*node).ratio, first_minimum, second_minimum);
    SeamSlot record{};
    record.split = node_id;
    record.axis = (*node).axis;
    record.span = span;
    record.first = sizes.first;
    record.first_minimum = first_minimum;
    record.second_minimum = second_minimum;
    Rect first_bounds{};
    Rect second_bounds{};
    if (horizontal) {
        record.span_start = bounds.x;
        first_bounds = {bounds.x, bounds.y, sizes.first, bounds.height};
        record.bar = {bounds.x + sizes.first, bounds.y, seam, bounds.height};
        second_bounds = {bounds.x + sizes.first + seam, bounds.y, sizes.second, bounds.height};
        record.hit = {record.bar.x - seam_hit_extension, bounds.y, seam + seam_hit_extension * 2.0, bounds.height};
    } else {
        record.span_start = bounds.y;
        first_bounds = {bounds.x, bounds.y, bounds.width, sizes.first};
        record.bar = {bounds.x, bounds.y + sizes.first, bounds.width, seam};
        second_bounds = {bounds.x, bounds.y + sizes.first + seam, bounds.width, sizes.second};
        record.hit = {bounds.x, record.bar.y - seam_hit_extension, bounds.width, seam + seam_hit_extension * 2.0};
    }
    seams_.push_back(record);
    place((*node).first, first_bounds, collapsed);
    place((*node).second, second_bounds, collapsed);
}

Size DockSite::measure(Size available) {
    return available;
}

// Whether this pass's seams differ from the last pass's.
bool DockSite::seams_moved() const noexcept {
    if (previous_seams_.size() != seams_.size()) return true;
    for (std::size_t index = 0; index < seams_.size(); ++index) {
        const bool same = previous_seams_[index].split == seams_[index].split &&
                          previous_seams_[index].bar == seams_[index].bar;
        if (!same) return true;
    }
    return false;
}

// One layout pass, in stages: remember the last pass and clear its
// rectangles; hold a dragged panel's page out of its view; take the side
// strips' bands and plan the collapse; place the tree; lay out the stack
// views and the side strips; follow the flyout; then publish what moved and
// update what depends on the pass (focusability, content, drag targets,
// focus).
void DockSite::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    const PreviousPass previous = begin_layout_pass();
    if (workspace_ == nullptr) {
        collapsed_.clear();
        return;
    }
    const bool focus_was_inside = focus_within(*this, attached_window());
    hold_out_dragged_page();
    const Size surface{final_bounds.width, final_bounds.height};
    std::array<bool, 4> edges{};
    std::array<Rect, 4> bands{};
    tree_area_ = plan_bands_and_collapse(surface, edges, bands);
    place(site_root(), tree_area_, collapsed_);
    arrange_stack_views();
    arrange_side_strips(edges, bands);
    follow_flyout_after_pass();
    finish_layout_pass(previous, surface, focus_was_inside);
}

// The last pass's seams, collapse, document area and tree area, kept to tell
// whether the site's own parts moved (the views report their own changes);
// the slots' rectangles are cleared for this pass.
DockSite::PreviousPass DockSite::begin_layout_pass() {
    previous_seams_.swap(seams_);
    seams_.clear();
    previous_collapsed_.swap(collapsed_);
    PreviousPass previous{};
    previous.document_area = document_area_;
    previous.tree_area = tree_area_;
    document_area_ = {};
    for (StackSlot& slot : stacks_) {
        slot.bounds = {};
        slot.caption = {};
    }
    return previous;
}

// A tab drag of one panel holds its page out of its view from the first
// pass of the drag (never inside the strip's own drag event).
void DockSite::hold_out_dragged_page() {
    if (drag_.phase != DockDragPhase::dragging || !drag_.panel_only || drag_.page_held_out) return;
    drag_.page_held_out = true;
    StackSlot* const source = slot_for(drag_.source_stack);
    const DockNode* const node = (*workspace_).layout().find_node(drag_.source_stack);
    if (source != nullptr && node != nullptr) sync_pages(*source, *node);
}

// The side strips' bands come off the edges first: those of the auto-hidden
// stacks, then, while collapsing puts stacks behind side tabs on further
// edges, theirs, planning the collapse again each time (at most once per
// edge). Fills `edges` (which edges have a band) and `bands`, sets
// collapsed_, and returns the tree's rectangle.
Rect DockSite::plan_bands_and_collapse(Size surface, std::array<bool, 4>& edges, std::array<Rect, 4>& bands) {
    for (const StackSlot& slot : stacks_) {
        if (slot.auto_hidden) edges[static_cast<std::size_t>(slot.edge)] = true;
    }
    Rect area = tree_area(surface, edges, bands);
    for (std::size_t attempt = 0; attempt <= edges.size(); ++attempt) {
        collapsed_ = plan_collapse(Size{area.width, area.height});
        if (maximized_) break;
        assign_collapsed_edges(area, collapsed_);
        bool added = false;
        for (const StackSlot& slot : stacks_) {
            if (!contains_node(collapsed_, slot.stack)) continue;
            const std::size_t edge = static_cast<std::size_t>(slot.edge);
            if (edges[edge]) continue;
            edges[edge] = true;
            added = true;
        }
        if (!added) break;
        area = tree_area(surface, edges, bands);
    }
    return area;
}

// Each slot's collapse from this pass's plan, and each view not in the
// flyout laid out below its caption (or collapsed).
void DockSite::arrange_stack_views() {
    for (StackSlot& slot : stacks_) {
        if (slot.auto_hidden) {
            slot.collapsed = true;
            slot.side_tabbed = false;
        } else {
            const bool shrunk = contains_node(collapsed_, slot.stack);
            // A stack a drag holds out of the layout is hidden as a collapsed
            // one is (collapsed_stacks does not list it), and not behind side
            // tabs.
            slot.collapsed = shrunk || hidden_by_drag(slot.stack);
            // Collapsed by shrinking, a tool stack goes behind side tabs; the
            // maximized document area hides it in place.
            slot.side_tabbed = shrunk && !maximized_ && slot.kind == DockNodeKind::tool_stack;
        }
        if (slot.in_flyout) continue;
        set_child_layout_collapsed(slot.view, slot.collapsed);
        if (slot.collapsed) continue;
        Rect content = slot.bounds;
        if (slot.kind == DockNodeKind::tool_stack) {
            content.y += slot.caption.height;
            content.height = std::max(0.0, content.height - slot.caption.height);
        }
        set_child_layout(slot.view, content);
    }
}

// Each edge's band, then the strips brought in line with the slots behind
// side tabs and laid out in their bands (collapsed while empty).
void DockSite::arrange_side_strips(const std::array<bool, 4>& edges, const std::array<Rect, 4>& bands) {
    for (std::size_t edge = 0; edge < sides_.size(); ++edge) sides_[edge].band = edges[edge] ? bands[edge] : Rect{};
    sync_side_strips();
    for (const SideStrip& entry : sides_) {
        if (!entry.strip) continue;
        const bool shown = !entry.panels.empty() && !entry.band.empty();
        set_child_layout_collapsed(entry.strip, !shown);
        if (shown) set_child_layout(entry.strip, entry.band);
    }
}

// A collapsed stack's flyout whose stack has room again closes once the pass
// is over (the window's overlays are not edited during layout); one still
// behind side tabs follows the pass.
void DockSite::follow_flyout_after_pass() {
    if (!flyout_panel_) return;
    const StackSlot* const slot = slot_for(flyout_stack_);
    const bool behind_tabs = slot != nullptr && ((*slot).auto_hidden || (*slot).side_tabbed);
    if (!behind_tabs) {
        schedule_stale_check();
    } else {
        place_flyout();
    }
}

void DockSite::finish_layout_pass(const PreviousPass& previous, Size surface, bool focus_was_inside) {
    if (keyboard_seam_ >= seams_.size()) keyboard_seam_ = 0U;
    const bool moved = seams_moved() || previous_collapsed_ != collapsed_ ||
                       !(previous.document_area == document_area_) || !(previous.tree_area == tree_area_);
    if (moved) invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
    // The site takes focus (for its seams) only while it has some, and while
    // a drag runs (for its keys).
    focusable = !seams_.empty() || drag_.phase != DockDragPhase::idle;
    for (StackSlot& slot : stacks_) supply_content(slot);
    if (drag_layer_) set_child_layout(drag_layer_, Rect{0.0, 0.0, surface.width, surface.height});
    if (drag_.phase == DockDragPhase::dragging) plan_drag_targets();
    if (drag_.phase == DockDragPhase::settling) place_settle_target();
    // During a drag the focus may stay lost; the drag's end puts it back.
    if (drag_.phase == DockDragPhase::idle) refocus_if_lost(focus_was_inside);
}

DockSite::CaptionGeometry DockSite::caption_geometry(Rect caption, bool pin, bool close) const {
    const double scale = effective_text_scale();
    CaptionGeometry placed{};
    placed.caption = caption;
    placed.accent = {caption.x, caption.y, caption.width, std::min(caption.height, accent_height * scale)};
    const double key = key_side * scale;
    placed.key = {caption.x + key_inset * scale, caption.y + (caption.height - key) * 0.5, key, key};
    const double button = button_side * scale;
    const double gap = button_gap * scale;
    const double top = caption.y + (caption.height - button) * 0.5;
    const double end = caption.x + caption.width - button_end_inset * scale;
    // Without a close button the buttons before it move to the end; its
    // rectangle stays empty, so nothing hits or paints there.
    double next = end;
    if (close) {
        placed.close = {end - button, top, button, button};
        next = placed.close.x - gap;
    }
    if (pin) {
        placed.pin = {next - button, top, button, button};
        next = placed.pin.x - gap;
    }
    placed.menu = {next - button, top, button, button};
    const double glyph = glyph_side * scale;
    if (close) placed.close_glyph = centred(placed.close, glyph, glyph);
    if (pin) placed.pin_glyph = centred(placed.pin, glyph, glyph);
    placed.menu_glyph = centred(placed.menu, glyph, glyph);
    const double label_start = placed.key.x + key + key_gap * scale;
    const double label_end = placed.menu.x - label_gap * scale;
    placed.label = {label_start, caption.y, std::max(0.0, label_end - label_start), caption.height};
    return placed;
}

// -- states --------------------------------------------------------------------

bool DockSite::focus_cue() const noexcept {
    const Window* const host = attached_window();
    const bool cue = focused_ && host != nullptr && (*host).focus_cue_visible();
    return cue;
}

bool DockSite::stack_current(const StackSlot& slot) const {
    if (workspace_ == nullptr) return false;
    const std::optional<DockPanelId>& active = (*workspace_).active_panel();
    if (!active || !slot.view) return false;
    const bool current = (*slot.view).pages().find(PageId{(*active).value}).has_value();
    return current;
}

bool DockSite::pin_shown(const StackSlot& slot) const {
    if (workspace_ == nullptr || slot.kind != DockNodeKind::tool_stack) return false;
    const std::optional<DockPanelId> panel = selected_panel(slot);
    if (!panel) return false;
    const std::optional<DockNodeId> owner = (*workspace_).layout().pin_owner(*panel);
    const bool shown = owner.has_value() && *owner == slot.stack;
    return shown;
}

bool DockSite::close_shown(const StackSlot& slot) const {
    if (workspace_ == nullptr) return true;
    const std::optional<DockPanelId> panel = selected_panel(slot);
    if (!panel) return true;
    const DockPanelDescriptor* const descriptor = (*workspace_).layout().find_descriptor(*panel);
    const bool shown = descriptor == nullptr || (*descriptor).close_button_visible;
    return shown;
}

bool DockSite::tab_closable(const DockPanelId& panel) const {
    if (workspace_ == nullptr) return true;
    const DockPanelDescriptor* const descriptor = (*workspace_).layout().find_descriptor(panel);
    const bool closable = descriptor == nullptr || (*descriptor).close_button_visible;
    return closable;
}

AnatomyStateSet DockSite::seam_states(std::size_t seam) const {
    AnatomyStateSet states{};
    if (hot_.zone == Zone::seam && hot_.index == seam) states.insert(AnatomyState::hot);
    if (seam_captured_ && drag_seam_ == seam) states.insert(AnatomyState::captured);
    if (focus_cue() && keyboard_seam_ == seam) states.insert(AnatomyState::focus_visible);
    if (!effectively_enabled()) states.insert(AnatomyState::disabled);
    return states;
}

AnatomyStateSet DockSite::stack_states(const StackSlot& slot) const {
    AnatomyStateSet states{};
    if (stack_current(slot)) states.insert(AnatomyState::current);
    if (!effectively_enabled()) states.insert(AnatomyState::disabled);
    return states;
}

AnatomyStateSet DockSite::button_states(std::size_t stack, Zone zone) const {
    AnatomyStateSet states{};
    if (hot_.zone == zone && hot_.index == stack) states.insert(AnatomyState::hot);
    if (pressed_.zone == zone && pressed_.index == stack) states.insert(AnatomyState::pressed);
    bool available = effectively_enabled();
    if (zone == Zone::menu && menu_open_ && menu_stack_ == stack) states.insert(AnatomyState::open);
    if (zone == Zone::pin && stack < stacks_.size()) {
        // Docked is pinned: pressing the pin auto-hides the stack; an
        // auto-hidden stack's pin docks it back.
        const StackSlot& slot = stacks_[stack];
        if (!slot.auto_hidden) states.insert(AnatomyState::checked);
        const std::optional<DockPanelId> panel = selected_panel(slot);
        const DockPositionCommand command = slot.auto_hidden ? DockPositionCommand::dock : DockPositionCommand::auto_hide;
        available = available && panel && position_command_enabled(*panel, command);
    }
    if (zone == Zone::close && stack < stacks_.size()) {
        const std::optional<DockPanelId> panel = selected_panel(stacks_[stack]);
        const bool closable = panel && (position_command_enabled(*panel, DockPositionCommand::hide) ||
                                        position_command_enabled(*panel, DockPositionCommand::close));
        available = available && closable;
    }
    if (!available) states.insert(AnatomyState::disabled);
    return states;
}

FontSpec DockSite::caption_font() const {
    const FontSpec font{FontRole::control, 10.5, 400, false, 0.12};
    return effective_font(font);
}

// -- paint ---------------------------------------------------------------------

// Paints from the last layout pass and the slots' cached titles: no
// allocation per seam or caption.
void DockSite::on_paint(Painter& painter, Rect) {
    PartPaint part{};
    part.font = caption_font();
    part.family = ControlVisualRole::panel;
    part.role = PartRole::well;
    part.bounds = document_area_;
    // The workspace's area colour (DockBackColor) is the well's authored
    // background; the theme engine paints it (and withdraws it under high
    // contrast).
    if (workspace_ != nullptr) part.authored_background = (*workspace_).area_background();
    paint_part(painter, *this, part);
    for (std::size_t seam = 0; seam < seams_.size(); ++seam) {
        const SeamParts parts = seam_parts(seam);
        part.family = ControlVisualRole::panel;
        part.states = seam_states(seam);
        part.role = PartRole::seam;
        part.bounds = parts.seam_bar.bounds;
        paint_part(painter, *this, part);
        part.role = PartRole::grip;
        part.mark = PartMark::grip_dots;
        part.bounds = parts.seam_grip.bounds;
        paint_part(painter, *this, part);
        part.mark = PartMark::none;
    }
    for (std::size_t index = 0; index < stacks_.size(); ++index) {
        const StackSlot& slot = stacks_[index];
        if (slot.collapsed || slot.kind != DockNodeKind::tool_stack || slot.caption.empty()) continue;
        paint_caption(painter, *this, slot, index, Point{});
    }
}

// A tool stack's caption, its rectangles in the site's coordinates moved by
// `offset` into the surface's (the site's own, or the flyout's).
void DockSite::paint_caption(Painter& painter, const Control& surface, const StackSlot& slot, std::size_t index,
                             Point offset) const {
    const CaptionGeometry placed = caption_geometry(slot.caption, pin_shown(slot), close_shown(slot));
    const AnatomyStateSet states = stack_states(slot);
    PartPaint part{};
    part.font = caption_font();
    // The caption strip and, while current, its accent line: the pale tint
    // and the line are the default theme's (T4, decision DK-41); today's
    // panel recipe draws its selected face.
    part.family = ControlVisualRole::panel;
    part.states = states;
    part.role = PartRole::face;
    part.bounds = moved(placed.caption, offset);
    paint_part(painter, surface, part);
    if (states.contains(AnatomyState::current)) {
        part.role = PartRole::keyline;
        part.bounds = moved(placed.accent, offset);
        part.accent = AccentIndex{0U};
        paint_part(painter, surface, part);
        part.accent.reset();
    }
    part.role = PartRole::chip;
    part.bounds = moved(placed.key, offset);
    paint_part(painter, surface, part);
    part.role = PartRole::caption;
    part.bounds = moved(placed.label, offset);
    part.text = slot.title;
    paint_part(painter, surface, part);
    part.text = {};
    // The buttons: flat at rest with a plate when hot (the menu-item family,
    // as the tool strip's).
    const std::array<Zone, 3> zones{Zone::menu, Zone::pin, Zone::close};
    const std::array<Rect, 3> buttons{placed.menu, placed.pin, placed.close};
    const std::array<Rect, 3> glyphs{placed.menu_glyph, placed.pin_glyph, placed.close_glyph};
    const std::array<PartMark, 3> marks{PartMark::chevron_down, PartMark::pin, PartMark::close};
    part.family = ControlVisualRole::menu_item;
    for (std::size_t button = 0; button < zones.size(); ++button) {
        if (buttons[button].empty()) continue;
        part.states = button_states(index, zones[button]);
        part.role = PartRole::actuator;
        part.bounds = moved(buttons[button], offset);
        paint_part(painter, surface, part);
        part.role = PartRole::glyph;
        part.bounds = moved(glyphs[button], offset);
        part.mark = marks[button];
        paint_part(painter, surface, part);
        part.mark = PartMark::none;
    }
}

// -- input ---------------------------------------------------------------------

DockSite::Hit DockSite::hit_at(Point local) const {
    Hit hit{};
    for (std::size_t seam = 0; seam < seams_.size(); ++seam) {
        if (!seams_[seam].hit.contains(local)) continue;
        hit.zone = Zone::seam;
        hit.index = seam;
        return hit;
    }
    for (std::size_t index = 0; index < stacks_.size(); ++index) {
        const StackSlot& slot = stacks_[index];
        if (slot.collapsed || slot.kind != DockNodeKind::tool_stack || !slot.caption.contains(local)) continue;
        const CaptionGeometry placed = caption_geometry(slot.caption, pin_shown(slot), close_shown(slot));
        hit.index = index;
        hit.zone = Zone::caption;
        if (placed.menu.contains(local)) hit.zone = Zone::menu;
        if (placed.pin.contains(local)) hit.zone = Zone::pin;
        if (placed.close.contains(local)) hit.zone = Zone::close;
        return hit;
    }
    return hit;
}

void DockSite::set_hot(Hit hit) {
    if (hot_ == hit) return;
    hot_ = hit;
    CursorKind shape = CursorKind::arrow;
    if (hit.zone == Zone::seam && hit.index < seams_.size()) {
        shape = seams_[hit.index].axis == DockAxis::horizontal ? CursorKind::resize_horizontal
                                                               : CursorKind::resize_vertical;
    }
    cursor = shape;
    invalidate(Dirty::paint);
}

void DockSite::begin_seam_drag(std::size_t seam, Point local) {
    const SeamSlot& slot = seams_[seam];
    const double along = slot.axis == DockAxis::horizontal ? local.x : local.y;
    drag_offset_ = along - (slot.span_start + slot.first);
    drag_seam_ = seam;
    seam_captured_ = true;
    keyboard_seam_ = seam;
    invalidate(Dirty::paint);
}

void DockSite::continue_seam_drag(Point local) {
    if (drag_seam_ >= seams_.size()) return;
    const SeamSlot& slot = seams_[drag_seam_];
    const double along = slot.axis == DockAxis::horizontal ? local.x : local.y;
    const double first = along - drag_offset_ - slot.span_start;
    static_cast<void>(set_seam_first(drag_seam_, first));
}

// Sets the first child's extent of a seam's split, clamped to both
// minimums, as one user transaction recording the ratio.
bool DockSite::set_seam_first(std::size_t seam, double first) {
    if (workspace_ == nullptr || seam >= seams_.size() || !std::isfinite(first)) return false;
    SeamSlot& slot = seams_[seam];
    if (slot.span <= 0.0) return false;
    const DockSplitSizes sizes =
        solve_dock_split(slot.span, first / slot.span, slot.first_minimum, slot.second_minimum);
    const double clamped = sizes.first;
    if (std::abs(clamped - slot.first) < 1e-6) return false;
    // The ratio lies strictly inside (0, 1); a child at zero keeps a
    // vanishing share.
    const double smallest = 1e-6;
    const double ratio = std::clamp(clamped / slot.span, smallest, 1.0 - smallest);
    DockTransaction transaction{};
    transaction.origin = DockEditOrigin::user;
    transaction.edits.push_back(DockEdit::set_ratio(slot.split, ratio));
    const DockTransactionResult result = (*workspace_).apply(transaction);
    if (!result) return false;
    slot.first = clamped;
    return true;
}

bool DockSite::move_seam(std::size_t seam, double delta) {
    if (seam >= seams_.size()) return false;
    const double first = seams_[seam].first + delta;
    const bool moved = set_seam_first(seam, first);
    return moved;
}

bool DockSite::focus_seam(std::size_t seam) {
    require_mutable();
    Window* const host = attached_window();
    if (host == nullptr || seam >= seams_.size()) return false;
    keyboard_seam_ = seam;
    invalidate(Dirty::paint | Dirty::semantics);
    const bool focused = (*host).request_focus(shared_from_this());
    return focused;
}

std::optional<std::size_t> DockSite::focused_seam() const noexcept {
    if (!focused_ || seams_.empty()) return std::nullopt;
    const std::optional<std::size_t> seam = keyboard_seam_;
    return seam;
}

// A press in a seam's hit band, which reaches past the bar into the stacks
// on both sides, starts dragging the seam (the site captures the pointer).
// A press anywhere else in a stack's content activates its panel before the
// pressed control takes focus, so a click on an unfocusable part of a panel
// still activates it. Presses on a stack's tabs are left to the strip.
void DockSite::on_pointer_preview(PointerEvent& event) {
    // A press outside the flyout closes it before the press activates
    // anything (the window's root observes it too, after this when the site
    // is the root).
    if (event.action == PointerAction::down) close_on_outside_press(event);
    if (workspace_ == nullptr) return;
    // A document tab sliding along its strip tears off when the pointer
    // leaves the strip's band.
    if (event.action == PointerAction::move && drag_.phase == DockDragPhase::idle && tear_off_document_tab(event)) {
        event.handled = true;
        return;
    }
    if (event.action != PointerAction::down || event.button != PointerButton::primary) return;
    document_stepping_ = false;
    if (drag_.phase == DockDragPhase::settling) {
        event.handled = true;
        return;
    }
    const Rect absolute = absolute_bounds();
    const Point local{event.position.x - absolute.x, event.position.y - absolute.y};
    const Hit hit = hit_at(local);
    Window* const host = attached_window();
    if (hit.zone == Zone::seam && host != nullptr) {
        const std::uint64_t pointer = event.pointer_id == 0U ? 1U : event.pointer_id;
        (*host).capture_pointer(shared_from_this(), pointer);
        begin_seam_drag(hit.index, local);
        event.handled = true;
        return;
    }
    for (const StackSlot& slot : stacks_) {
        if (slot.collapsed || !slot.view) continue;
        if (!(*slot.view).absolute_bounds().contains(event.position)) continue;
        const std::shared_ptr<PageStrip>& strip = (*slot.view).strip();
        if (strip && (*strip).visible.read() && (*strip).absolute_bounds().contains(event.position)) return;
        const std::optional<DockPanelId> panel = selected_panel(slot);
        const std::optional<DockPanelId>& active = (*workspace_).active_panel();
        if (panel && !(active && *active == *panel)) {
            static_cast<void>((*workspace_).activate_panel(*panel, DockActivationCause::pointer));
        }
        return;
    }
}

void DockSite::on_pointer(PointerEvent& event) {
    if (event.action == PointerAction::leave) {
        if (!seam_captured_) set_hot({});
        return;
    }
    const Rect absolute = absolute_bounds();
    const Point local{event.position.x - absolute.x, event.position.y - absolute.y};
    // A floating window moved by its caption (phase D4).
    if (window_move_.active) {
        const Control::Ptr keep = shared_from_this();
        if (event.action == PointerAction::move) {
            continue_window_move(event.modifiers);
            event.handled = true;
            return;
        }
        if (event.action == PointerAction::up && event.button == PointerButton::primary) {
            end_window_move(false);
            event.handled = true;
            return;
        }
    }
    if ((drag_.phase == DockDragPhase::armed || drag_.phase == DockDragPhase::dragging) && !drag_.guest) {
        const Control::Ptr keep = shared_from_this();
        if (event.action == PointerAction::move) {
            update_drag_pointer(local, event.modifiers);
            event.handled = true;
            return;
        }
        if (event.action == PointerAction::up && event.button == PointerButton::primary) {
            if (drag_.phase == DockDragPhase::armed) {
                reset_drag();
            } else {
                update_drag_pointer(local, event.modifiers);
                // The drop's target may be this site's or another window's;
                // end_drag decides.
                end_drag(DockDragEnd::dropped);
            }
            event.handled = true;
            return;
        }
    }
    if (drag_.phase == DockDragPhase::settling) {
        event.handled = true;
        return;
    }
    if (event.action == PointerAction::move || event.action == PointerAction::enter) {
        if (seam_captured_) {
            continue_seam_drag(local);
            event.handled = true;
            return;
        }
        set_hot(hit_at(local));
        return;
    }
    if (event.button != PointerButton::primary || workspace_ == nullptr) return;
    if (event.action == PointerAction::up) {
        if (seam_captured_) {
            seam_captured_ = false;
            invalidate(Dirty::paint);
            event.handled = true;
            return;
        }
        const Hit released = hit_at(local);
        const Hit pressed = pressed_;
        pressed_ = {};
        invalidate(Dirty::paint);
        if (pressed.zone == Zone::none) return;
        event.handled = true;
        if (pressed != released) return;
        press_caption_button(pressed.index, pressed.zone);
        return;
    }
    if (event.action != PointerAction::down) return;
    const Hit hit = hit_at(local);
    if (hit.zone == Zone::none) return;
    const Control::Ptr keep = shared_from_this();
    event.handled = true;
    if (hit.zone == Zone::seam) {
        begin_seam_drag(hit.index, local);
        return;
    }
    const std::optional<DockPanelId> panel = selected_panel(stacks_[hit.index]);
    if (!panel) return;
    if (hit.zone == Zone::caption) {
        // A double-click on a floating caption docks the stack back (phase
        // D4).
        if (event.click_count >= 2U && floating() && hit.index < stacks_.size()) {
            static_cast<void>((*workspace_).dock_back(DockSubject::of_stack(stacks_[hit.index].stack)));
            return;
        }
        static_cast<void>((*workspace_).activate_panel(*panel, DockActivationCause::pointer));
        // The press may become a drag of the whole stack (D7-1).
        if (is_alive() && hit.index < stacks_.size()) arm_caption_drag(hit.index, local, event.pointer_id);
        return;
    }
    if (hit.zone == Zone::menu) {
        if (menu_open_ && menu_stack_ == hit.index) {
            close_position_menu();
            return;
        }
        static_cast<void>((*workspace_).activate_panel(*panel, DockActivationCause::pointer));
        static_cast<void>(show_position_menu(*panel, MenuOpenMode::pointer));
        return;
    }
    // An unavailable button takes no press.
    if (button_states(hit.index, hit.zone).contains(AnatomyState::disabled)) return;
    pressed_ = hit;
    invalidate(Dirty::paint);
}

void DockSite::on_focus_changed(bool focused) {
    focused_ = focused;
    invalidate(Dirty::paint | Dirty::semantics);
}

// A caption's close hides a tool panel (its placeholder stays), or closes
// one that may not be hidden; its pin auto-hides a docked stack or docks an
// auto-hidden one back.
void DockSite::press_caption_button(std::size_t index, Zone zone) {
    if (index >= stacks_.size() || workspace_ == nullptr) return;
    const StackSlot& slot = stacks_[index];
    const std::optional<DockPanelId> panel = selected_panel(slot);
    if (!panel) return;
    if (zone == Zone::close) {
        const DockPositionCommand command = position_command_enabled(*panel, DockPositionCommand::hide)
                                                ? DockPositionCommand::hide
                                                : DockPositionCommand::close;
        static_cast<void>(run_position_command(*panel, command));
        return;
    }
    if (zone != Zone::pin) return;
    const DockPositionCommand command = slot.auto_hidden ? DockPositionCommand::dock : DockPositionCommand::auto_hide;
    static_cast<void>(run_position_command(*panel, command));
}

// While the site itself has focus, the keyboard is on a seam: Tab and
// Shift+Tab step between seams (leaving the site past either end), the
// arrows along the split's axis move the seam.
bool DockSite::handle_seam_key(KeyEvent& event) {
    if (!focused_ || seams_.empty() || event.action != KeyAction::down) return false;
    if (keyboard_seam_ >= seams_.size()) keyboard_seam_ = 0U;
    const bool plain = event.modifiers == Modifier::none;
    if (event.physical_key == PhysicalKey::tab && (plain || event.modifiers == Modifier::shift)) {
        if (plain && keyboard_seam_ + 1U < seams_.size()) {
            ++keyboard_seam_;
        } else if (!plain && keyboard_seam_ > 0U) {
            --keyboard_seam_;
        } else {
            return false;
        }
        invalidate(Dirty::paint | Dirty::semantics);
        event.handled = true;
        return true;
    }
    if (!plain) return false;
    const bool horizontal = seams_[keyboard_seam_].axis == DockAxis::horizontal;
    double delta = 0.0;
    if (horizontal && event.physical_key == PhysicalKey::left) delta = -keyboard_increment.read();
    if (horizontal && event.physical_key == PhysicalKey::right) delta = keyboard_increment.read();
    if (!horizontal && event.physical_key == PhysicalKey::up) delta = -keyboard_increment.read();
    if (!horizontal && event.physical_key == PhysicalKey::down) delta = keyboard_increment.read();
    if (delta == 0.0) return false;
    static_cast<void>(move_seam(keyboard_seam_, delta));
    event.handled = true;
    return true;
}

namespace {

// The modifier whose release commits the navigator a key command opened: the
// first of Ctrl, Alt and Meta the chord holds (Ctrl after Ctrl+Tab, Alt after
// Alt+F7), or none for a chord rebound to hold none of them.
[[nodiscard]] Modifier navigator_release(Modifier held) noexcept {
    if (has_modifier(held, Modifier::control)) return Modifier::control;
    if (has_modifier(held, Modifier::alt)) return Modifier::alt;
    if (has_modifier(held, Modifier::meta)) return Modifier::meta;
    return Modifier::none;
}

} // namespace

// The docking keys (owner decision DK-30), wherever focus is in the site.
// They are the window's named keyboard commands (G6-1: an application may
// rebind them): cycle_forward and cycle_backward (Ctrl+Tab) open the
// documents' navigator, next_tool_panel and previous_tool_panel (Alt+F7) the
// tools', next_document and previous_document (Ctrl+F6) step the documents,
// maximize_document_area (Ctrl+Shift+F12), return_to_document (Escape in a
// tool stack), hide_tool_panel (Shift+Escape in a tool stack), and
// position_menu (Alt+Hyphen) or context_menu (Shift+F10, the context-menu
// key) open the focused panel's position menu.
bool DockSite::handle_docking_key(KeyEvent& event) {
    if (event.handled || event.action != KeyAction::down || workspace_ == nullptr) return false;
    const KeyMap& keys = effective_key_map();
    const Modifier release = navigator_release(event.modifiers);
    bool handled = false;
    if (keys.matches(KeyCommand::cycle_forward, event)) {
        handled = open_navigator(DockNavigatorList::documents, 1, release);
    } else if (keys.matches(KeyCommand::cycle_backward, event)) {
        handled = open_navigator(DockNavigatorList::documents, -1, release);
    } else if (keys.matches(KeyCommand::next_tool_panel, event)) {
        handled = open_navigator(DockNavigatorList::tools, 1, release);
    } else if (keys.matches(KeyCommand::previous_tool_panel, event)) {
        handled = open_navigator(DockNavigatorList::tools, -1, release);
    } else if (keys.matches(KeyCommand::next_document, event)) {
        handled = step_document(1, release);
    } else if (keys.matches(KeyCommand::previous_document, event)) {
        handled = step_document(-1, release);
    } else if (keys.matches(KeyCommand::maximize_document_area, event) && !floating()) {
        set_document_area_maximized(!maximized_);
        handled = true;
    } else if (keys.matches(KeyCommand::return_to_document, event)) {
        const std::optional<DockPanelId>& document = (*workspace_).active_document();
        if (focus_in_tool_stack() && document) {
            handled = (*workspace_).activate_panel(*document, DockActivationCause::keyboard);
        }
    } else if (keys.matches(KeyCommand::hide_tool_panel, event)) {
        const std::optional<DockPanelId> panel = focused_panel();
        if (focus_in_tool_stack() && panel) handled = run_position_command(*panel, DockPositionCommand::hide);
    } else if (keys.matches(KeyCommand::move_panel, event)) {
        std::optional<DockPanelId> panel = focused_panel();
        if (!panel) panel = (*workspace_).active_panel();
        if (panel) handled = begin_keyboard_move(*panel);
    } else if (keys.matches(KeyCommand::position_menu, event)) {
        std::optional<DockPanelId> panel = focused_panel();
        if (!panel) panel = (*workspace_).active_panel();
        if (panel) handled = show_position_menu(*panel, MenuOpenMode::keyboard);
    }
    if (handled) event.handled = true;
    return handled;
}

// A drag takes its keys (Escape, Ctrl, and the keyboard move mode's)
// before the focused control sees them.
void DockSite::on_key_preview(KeyEvent& event) {
    end_document_steps(event);
    static_cast<void>(handle_drag_key(event));
}

// A keyboard context request (the host's own: Windows' Shift+F10 and Menu
// key, the Linux desktop's, macOS's show-menu action) that nothing inside
// took opens the focused or active panel's position menu (G2-1).
void DockSite::on_context_menu_requested(ContextMenuRequest& request) {
    if (request.origin != ContextRequestOrigin::keyboard || workspace_ == nullptr) return;
    std::optional<DockPanelId> panel = focused_panel();
    if (!panel) panel = (*workspace_).active_panel();
    if (panel && show_position_menu(*panel, MenuOpenMode::keyboard)) request.handled = true;
}

void DockSite::on_key(KeyEvent& event) {
    if (handle_seam_key(event)) return;
    static_cast<void>(handle_docking_key(event));
    pass_key_to_main_window(event);
}

void DockSite::on_key_bubble(KeyEvent& event) {
    static_cast<void>(handle_docking_key(event));
    pass_key_to_main_window(event);
}

// A floating site is its window's root, so a key reaching it here was taken
// by nothing on the route: this window's accelerators are tried, then the
// main window's (owner decision DK-29, F2-1), so its shortcuts work from a
// floating window.
void DockSite::pass_key_to_main_window(KeyEvent& event) {
    if (event.handled || event.action != KeyAction::down || !floating() || workspace_ == nullptr) return;
    Window* const own = attached_window();
    const DockSite* const main_site = (*workspace_).site();
    Window* const main = main_site == nullptr ? nullptr : (*main_site).attached_window();
    if (own == nullptr || main == nullptr || main == own) return;
    const Control::Ptr keep = shared_from_this();
    if ((*own).offer_accelerators(event) || (*main).offer_accelerators(event)) event.handled = true;
}

// The shown documents in most-recently-used order (owner decision DK-46):
// the workspace's document history first (the active document leads it),
// then the documents never active, in tab order.
void DockSite::collect_document_steps(std::vector<DockPanelId>& out) const {
    out.clear();
    std::vector<DockPanelId> documents{};
    for (const StackSlot& slot : stacks_) {
        if (slot.kind != DockNodeKind::document_stack || slot.collapsed) continue;
        const DockNode* const node = (*workspace_).layout().find_node(slot.stack);
        if (node == nullptr) continue;
        documents.insert(documents.end(), (*node).panels.begin(), (*node).panels.end());
    }
    for (const DockPanelId& panel : (*workspace_).document_history()) {
        if (std::find(documents.begin(), documents.end(), panel) == documents.end()) continue;
        out.push_back(panel);
    }
    for (const DockPanelId& panel : documents) {
        if (std::find(out.begin(), out.end(), panel) == out.end()) out.push_back(panel);
    }
}

// One step along the order from the active document, wrapping: +1 is the
// document used before it, -1 the one used longest ago.
bool DockSite::activate_relative_document(int direction) {
    if (workspace_ == nullptr) return false;
    collect_document_steps(document_steps_);
    document_stepping_ = false;
    if (document_steps_.empty()) return false;
    const std::size_t count = document_steps_.size();
    std::size_t next = direction >= 0 ? 0U : count - 1U;
    const std::optional<DockPanelId>& active = (*workspace_).active_document();
    if (active && document_steps_.front() == *active) next = direction >= 0 ? 1U % count : count - 1U;
    const DockPanelId chosen = document_steps_[next];
    const bool activated = (*workspace_).activate_panel(chosen, DockActivationCause::keyboard);
    return activated;
}

// The keys' steps (Ctrl+F6, Ctrl+Shift+F6): the first press takes the order
// as it stands and each further press, with the chord's modifier still held,
// steps on along that same order, as the navigator's rows do; releasing the
// modifier, any other key or a press ends the run, and the document reached
// is then the most recent.
bool DockSite::step_document(int direction, Modifier release) {
    if (workspace_ == nullptr) return false;
    if (!document_stepping_) {
        collect_document_steps(document_steps_);
        if (document_steps_.empty()) return false;
        document_step_ = 0U;
        const std::optional<DockPanelId>& active = (*workspace_).active_document();
        const bool leads = active && document_steps_.front() == *active;
        if (!leads) document_step_ = direction >= 0 ? document_steps_.size() - 1U : 0U;
        document_stepping_ = true;
        document_step_release_ = release;
    }
    const std::size_t count = document_steps_.size();
    document_step_ = direction >= 0 ? (document_step_ + 1U) % count : (document_step_ + count - 1U) % count;
    const DockPanelId chosen = document_steps_[document_step_];
    const bool activated = (*workspace_).activate_panel(chosen, DockActivationCause::keyboard);
    return activated;
}

// Ends a run of document steps when its modifier is let go, or on any other
// key.
void DockSite::end_document_steps(const KeyEvent& event) noexcept {
    if (!document_stepping_) return;
    if (event.action == KeyAction::up) {
        const bool held = document_step_release_ != Modifier::none &&
                          has_modifier(event.modifiers, document_step_release_);
        if (!held) document_stepping_ = false;
        return;
    }
    if (workspace_ == nullptr) {
        document_stepping_ = false;
        return;
    }
    const KeyMap& keys = effective_key_map();
    const bool stepping = keys.matches(KeyCommand::next_document, event) ||
                          keys.matches(KeyCommand::previous_document, event);
    if (!stepping) document_stepping_ = false;
}

// -- the position menu ---------------------------------------------------------

bool DockSite::position_command_enabled(const DockPanelId& panel, DockPositionCommand command) const {
    if (workspace_ == nullptr) return false;
    const DockLayout& layout = (*workspace_).layout();
    const DockPanelDescriptor* const descriptor = layout.find_descriptor(panel);
    const DockPanelLocation location = layout.locate(panel);
    if (descriptor == nullptr) return false;
    const bool auto_hidden = location.placement == DockPlacementKind::auto_hidden;
    const bool floating_panel = location.placement == DockPlacementKind::floating;
    if (location.placement != DockPlacementKind::docked && !auto_hidden && !floating_panel) return false;
    const DockNode* const stack = layout.find_node(location.stack);
    const bool in_documents = stack != nullptr && (*stack).kind == DockNodeKind::document_stack;
    const bool tool = (*descriptor).kind == DockPanelKind::tool;
    switch (command) {
    case DockPositionCommand::float_panel: {
        // While the workspace has windows and the panel may float (phase D4).
        const bool floats = !floating_panel && (*workspace_).can_float(DockSubject::of_panel(panel));
        return floats;
    }
    case DockPositionCommand::auto_hide: {
        // A root-level tool stack (DK decision 28) that the layout lets go to
        // auto-hide.
        const std::optional<DockNodeId> owner = layout.pin_owner(panel);
        if (auto_hidden || !tool || !owner || !(*owner == location.stack)) return false;
        DockTransaction transaction{};
        transaction.origin = DockEditOrigin::user;
        transaction.edits.push_back(DockEdit::auto_hide(DockSubject::of_stack(location.stack), DockSide::left));
        const bool admitted = static_cast<bool>(layout.check(transaction));
        return admitted;
    }
    case DockPositionCommand::dock: {
        if (auto_hidden) {
            DockTransaction transaction{};
            transaction.origin = DockEditOrigin::user;
            transaction.edits.push_back(DockEdit::unpin(location.stack));
            const bool admitted = static_cast<bool>(layout.check(transaction));
            return admitted;
        }
        if (floating_panel) {
            const bool returns = (*workspace_).dock_back_edit(DockSubject::of_panel(panel)).has_value();
            return returns;
        }
        const bool docks = tool && in_documents && (*descriptor).allowed.docked;
        return docks;
    }
    case DockPositionCommand::dock_as_document: {
        const bool documents = tool && !in_documents && !auto_hidden && (*descriptor).dock_as_document &&
                               (*descriptor).allowed.document_area;
        return documents;
    }
    case DockPositionCommand::hide: {
        const bool hides = tool && (*descriptor).allowed.hidden;
        return hides;
    }
    case DockPositionCommand::close:
        return (*descriptor).allowed.closed;
    case DockPositionCommand::move: {
        const bool idle = drag_.phase == DockDragPhase::idle;
        return idle;
    }
    case DockPositionCommand::close_all_documents:
    case DockPositionCommand::close_other_documents: {
        // From a document's menu, while some document (for Close Other
        // Documents, one other than this) may close.
        if (tool || (*workspace_).close_pending()) return false;
        const std::vector<DockPanelId> documents = (*workspace_).closable_documents();
        std::size_t others = 0U;
        for (const DockPanelId& document : documents) {
            if (!(document == panel)) ++others;
        }
        const bool all = command == DockPositionCommand::close_all_documents;
        const bool available = all ? !documents.empty() : others != 0U;
        return available;
    }
    }
    // A command outside the enumeration is never available.
    return false;
}

bool DockSite::run_position_command(const DockPanelId& panel, DockPositionCommand command) {
    if (!position_command_enabled(panel, command)) return false;
    const DockPanelDescriptor& descriptor = *(*workspace_).layout().find_descriptor(panel);
    const DockPanelLocation location = (*workspace_).layout().locate(panel);
    DockTransactionResult result{};
    if (command == DockPositionCommand::move) {
        const bool started = begin_keyboard_move(panel);
        return started;
    }
    if (command == DockPositionCommand::close_all_documents || command == DockPositionCommand::close_other_documents) {
        // The documents are asked about first (phase D6); a prompt that
        // waits for its answer counts as run.
        const DockCloseOutcome outcome = command == DockPositionCommand::close_all_documents
                                             ? (*workspace_).close_all_documents()
                                             : (*workspace_).close_other_documents(panel);
        const bool ran = outcome == DockCloseOutcome::closed || outcome == DockCloseOutcome::pending;
        return ran;
    }
    if (command == DockPositionCommand::float_panel) {
        // Where it floated last, else where it stands now (phase D4).
        Rect frame{100.0, 100.0, 320.0, 240.0};
        bool remembered = false;
        for (const DockPanelRecord& record : (*workspace_).layout().state().panels) {
            if (!(record.id == panel) || !record.floating) continue;
            frame = (*record.floating).frame;
            remembered = true;
        }
        const StackSlot* const slot = slot_for(location.stack);
        if (!remembered && slot != nullptr && !(*slot).bounds.empty()) {
            const std::optional<Point> corner = to_desktop(Point{(*slot).bounds.x, (*slot).bounds.y});
            if (corner) frame = {(*corner).x, (*corner).y, (*slot).bounds.width, (*slot).bounds.height};
        }
        if (flyout_panel_ && flyout_stack_ == location.stack) close_flyout(DockFlyoutCause::pointer);
        result = (*workspace_).float_subject(DockSubject::of_panel(panel), frame);
        if (result) static_cast<void>((*workspace_).activate_panel(panel, DockActivationCause::keyboard));
        const bool applied = result.applied;
        return applied;
    }
    if (command == DockPositionCommand::dock && location.placement == DockPlacementKind::floating) {
        result = (*workspace_).dock_back(DockSubject::of_panel(panel));
        const bool applied = result.applied;
        return applied;
    }
    if (command == DockPositionCommand::auto_hide) {
        // The whole stack goes to the window edge nearest it, keeping its
        // extent across that edge (DK decision 27, E4-1). Only the site
        // realizing the stack knows where it stands; any other refuses.
        const StackSlot* const slot = slot_for(location.stack);
        if (slot == nullptr) return false;
        const Rect bounds = (*slot).bounds;
        const DockSide edge = (*slot).side_tabbed ? (*slot).edge : nearest_edge(bounds, tree_area_);
        const bool across = edge == DockSide::left || edge == DockSide::right;
        double extent = across ? bounds.width : bounds.height;
        if (!(extent > 0.0) || (*slot).side_tabbed) extent = flyout_default_extent(*slot);
        if (flyout_panel_ && flyout_stack_ == location.stack) close_flyout(DockFlyoutCause::pointer);
        DockTransaction transaction{};
        transaction.origin = DockEditOrigin::user;
        transaction.edits.push_back(DockEdit::auto_hide(DockSubject::of_stack(location.stack), edge, extent));
        result = (*workspace_).apply(transaction);
        const bool applied = result.applied;
        return applied;
    }
    if (command == DockPositionCommand::dock && location.placement == DockPlacementKind::auto_hidden) {
        // Back to where the stack stood; the panel stays active there.
        if (flyout_panel_ && flyout_stack_ == location.stack) close_flyout(DockFlyoutCause::pointer);
        DockTransaction transaction{};
        transaction.origin = DockEditOrigin::user;
        transaction.edits.push_back(DockEdit::unpin(location.stack));
        result = (*workspace_).apply(transaction);
        if (result) static_cast<void>((*workspace_).activate_panel(panel, DockActivationCause::keyboard));
        const bool applied = result.applied;
        return applied;
    }
    if (command == DockPositionCommand::hide) {
        result = (*workspace_).hide_panel(panel, DockEditOrigin::user);
    } else if (command == DockPositionCommand::close) {
        // A document is asked about first (DockWorkspace::request_close,
        // phase D6); a prompt that waits for its answer counts as run.
        const DockCloseOutcome outcome = (*workspace_).request_close(panel);
        const bool ran = outcome == DockCloseOutcome::closed || outcome == DockCloseOutcome::pending;
        return ran;
    } else {
        DockTransaction transaction{};
        transaction.origin = DockEditOrigin::user;
        const DockSubject subject = DockSubject::of_panel(panel);
        if (command == DockPositionCommand::dock) {
            const DockDefaultPlacement& placement = descriptor.default_placement;
            transaction.edits.push_back(DockEdit::dock_at_edge(subject, placement.side, placement.ratio));
        } else {
            transaction.edits.push_back(DockEdit::dock_into(subject, (*workspace_).layout().document_area()));
        }
        result = (*workspace_).apply(transaction);
        // The panel moved: it stays the active panel in its new place.
        if (result) static_cast<void>((*workspace_).activate_panel(panel, DockActivationCause::keyboard));
    }
    const bool applied = result.applied;
    return applied;
}

bool DockSite::show_position_menu(const DockPanelId& panel, MenuOpenMode mode) {
    require_mutable();
    if (mode != MenuOpenMode::keyboard && mode != MenuOpenMode::pointer) {
        throw std::invalid_argument("DockSite menu opening mode is invalid");
    }
    if (workspace_ == nullptr || attached_window() == nullptr) return false;
    const DockPanelLocation location = (*workspace_).layout().locate(panel);
    if (location.placement != DockPlacementKind::docked && location.placement != DockPlacementKind::auto_hidden &&
        location.placement != DockPlacementKind::floating) {
        return false;
    }
    std::size_t index = stacks_.size();
    for (std::size_t candidate = 0; candidate < stacks_.size(); ++candidate) {
        if (stacks_[candidate].stack == location.stack) index = candidate;
    }
    // A stack in the tree, or the stack whose flyout shows this panel.
    if (index == stacks_.size()) return false;
    const bool from_flyout = stacks_[index].in_flyout && flyout_panel_ && *flyout_panel_ == panel;
    if (stacks_[index].collapsed && !from_flyout) return false;
    const StackSlot& slot = stacks_[index];
    const bool tool_panel = (*(*workspace_).layout().find_descriptor(panel)).kind == DockPanelKind::tool;
    for (std::size_t command = 0; command < menu_commands_.size(); ++command) {
        const bool enabled = position_command_enabled(panel, static_cast<DockPositionCommand>(command));
        (*menu_commands_[command]).set_enabled(enabled);
    }
    // Dock is checked while the panel stands in a docked tool stack, Auto
    // Hide while it is auto-hidden.
    (*menu_commands_[command_index(DockPositionCommand::dock)])
        .set_checked(slot.kind == DockNodeKind::tool_stack && !slot.auto_hidden && !floating());
    (*menu_commands_[command_index(DockPositionCommand::auto_hide)]).set_checked(slot.auto_hidden);
    const std::string prefix(stable_id().value());
    std::vector<MenuItemSpec> items{};
    const std::array<DockPositionCommand, 4> placing{DockPositionCommand::float_panel, DockPositionCommand::dock,
                                                     DockPositionCommand::dock_as_document,
                                                     DockPositionCommand::auto_hide};
    for (const DockPositionCommand command : placing) {
        if (!tool_panel && command != DockPositionCommand::float_panel) continue;
        const std::size_t at = command_index(command);
        const bool checkable = command == DockPositionCommand::dock || command == DockPositionCommand::auto_hide;
        const MenuItemKind kind = checkable ? MenuItemKind::check : MenuItemKind::command;
        items.emplace_back(prefix + ".position." + std::string(position_names[at]), kind, menu_commands_[at],
                           std::string(position_texts[at]));
    }
    items.emplace_back(prefix + ".position.separator", MenuItemKind::separator);
    if (tool_panel) {
        const std::size_t hide = command_index(DockPositionCommand::hide);
        items.emplace_back(prefix + ".position.hide", MenuItemKind::command, menu_commands_[hide],
                           std::string(position_texts[hide]));
    }
    const std::size_t close = command_index(DockPositionCommand::close);
    items.emplace_back(prefix + ".position.close", MenuItemKind::command, menu_commands_[close],
                       std::string(position_texts[close]));
    if (!tool_panel) {
        const std::array<DockPositionCommand, 2> closing{DockPositionCommand::close_all_documents,
                                                         DockPositionCommand::close_other_documents};
        for (const DockPositionCommand command : closing) {
            const std::size_t at = command_index(command);
            items.emplace_back(prefix + ".position." + std::string(position_names[at]), MenuItemKind::command,
                               menu_commands_[at], std::string(position_texts[at]));
        }
    }
    const std::size_t move = command_index(DockPositionCommand::move);
    items.emplace_back(prefix + ".position.move", MenuItemKind::command, menu_commands_[move],
                       std::string(position_texts[move]));
    (*menu_).set_items(std::move(items));
    const Rect absolute = absolute_bounds();
    Point anchor{absolute.x + slot.bounds.x, absolute.y + slot.bounds.y};
    // From the flyout, focus in the menu does not count as leaving it.
    menu_from_flyout_ = from_flyout;
    if (slot.kind == DockNodeKind::tool_stack) {
        const CaptionGeometry placed = caption_geometry(slot.caption, pin_shown(slot), close_shown(slot));
        anchor = {absolute.x + placed.menu.x, absolute.y + placed.menu.y + placed.menu.height};
    } else {
        anchor.y += strip_extent(*this);
    }
    menu_panel_ = panel;
    menu_stack_ = index;
    (*menu_).show(shared_from_this(), anchor, mode);
    invalidate(Dirty::paint | Dirty::semantics);
    return true;
}

void DockSite::close_position_menu() noexcept {
    if (menu_) (*menu_).close();
}

bool DockSite::position_menu_open() const noexcept {
    const bool open = menu_ && (*menu_).is_open();
    return open;
}

void DockSite::on_menu_open_changed(bool open) {
    menu_open_ = open;
    if (!open) menu_from_flyout_ = false;
    invalidate(Dirty::paint | Dirty::semantics);
}

void DockSite::on_menu_item_invoked(const MenuItemInvocation& invocation) {
    const std::string prefix = std::string(stable_id().value()) + ".position.";
    for (std::size_t command = 0; command < position_names.size(); ++command) {
        if (invocation.item_id != prefix + std::string(position_names[command])) continue;
        const DockPanelId panel = menu_panel_;
        static_cast<void>(run_position_command(panel, static_cast<DockPositionCommand>(command)));
        return;
    }
}

// -- the navigator -------------------------------------------------------------

std::shared_ptr<DockNavigator> DockSite::navigator() const noexcept {
    if (!navigator_open_) return {};
    return navigator_;
}

bool DockSite::open_navigator(DockNavigatorList list, int direction, Modifier release) {
    require_mutable();
    Window* const host = attached_window();
    if (host == nullptr || workspace_ == nullptr) return false;
    if (navigator_open_) return true;
    close_position_menu();
    // Each list: the panels in most-recently-active order, then the others
    // shown in tree order; panels in collapsed stacks are left out.
    std::vector<DockNavigatorEntry> tools{};
    std::vector<DockNavigatorEntry> documents{};
    std::vector<DockPanelId> shown{};
    for (const StackSlot& slot : stacks_) {
        if (slot.collapsed) continue;
        const DockNode* const node = (*workspace_).layout().find_node(slot.stack);
        if (node != nullptr) shown.insert(shown.end(), (*node).panels.begin(), (*node).panels.end());
    }
    const std::array<std::span<const DockPanelId>, 2> histories{(*workspace_).tool_history(),
                                                                (*workspace_).document_history()};
    for (std::size_t kind = 0; kind < histories.size(); ++kind) {
        std::vector<DockNavigatorEntry>& entries = kind == 0U ? tools : documents;
        const DockPanelKind wanted = kind == 0U ? DockPanelKind::tool : DockPanelKind::document;
        for (const DockPanelId& panel : histories[kind]) {
            if (std::find(shown.begin(), shown.end(), panel) == shown.end()) continue;
            entries.push_back(DockNavigatorEntry{panel, (*workspace_).panel_title(panel)});
        }
        for (const DockPanelId& panel : shown) {
            const DockPanelDescriptor* const descriptor = (*workspace_).layout().find_descriptor(panel);
            if (descriptor == nullptr || (*descriptor).kind != wanted) continue;
            bool listed = false;
            for (const DockNavigatorEntry& entry : entries) {
                if (entry.panel == panel) listed = true;
            }
            if (!listed) entries.push_back(DockNavigatorEntry{panel, (*workspace_).panel_title(panel)});
        }
    }
    if (tools.empty() && documents.empty()) return false;
    const std::string identity = std::string(stable_id().value()) + ".navigator";
    navigator_ = make_control<DockNavigator>(StableId(identity));
    (*navigator_).set_entries(std::move(tools), std::move(documents), list);
    // The row after the active one (the second most recent), or the last.
    const DockNavigatorList shown_list = (*navigator_).current_list();
    const std::size_t count = (*navigator_).entries(shown_list).size();
    const std::size_t row = direction >= 0 ? std::min<std::size_t>(1U, count - 1U) : count - 1U;
    static_cast<void>((*navigator_).select(shown_list, row));
    (*navigator_).release_modifier = release;
    const Size wanted = (*navigator_).measure(Size{absolute_bounds().width, absolute_bounds().height});
    const Rect absolute = absolute_bounds();
    const Rect placed = centred(absolute, wanted.width, wanted.height);
    (*navigator_).set_requested_bounds(placed);
    navigator_chosen_ = (*navigator_).chosen().subscribe(
        *this, Delegate<const DockNavigatorChoice&>::bind<DockSite, &DockSite::on_navigator_chosen>(*this));
    navigator_cancelled_ =
        (*navigator_).cancelled().subscribe(*this, Delegate<>::bind<DockSite, &DockSite::on_navigator_cancelled>(*this));
    navigator_token_ = (*host).open_overlay(shared_from_this(), navigator_);
    if (Event<>* const closed = navigator_token_.closed_event()) {
        navigator_revoked_ =
            (*closed).subscribe(*this, Delegate<>::bind<DockSite, &DockSite::on_navigator_revoked>(*this));
    }
    if (const Control::Ptr root = (*host).root()) {
        window_pointer_ = (*root).pointer_preview_observed().subscribe(
            *this, Delegate<const PointerEvent&>::bind<DockSite, &DockSite::on_window_pointer>(*this));
    }
    FocusScopeOptions options{};
    options.contain_focus = true;
    navigator_scope_ = (*host).begin_focus_scope(navigator_, navigator_, options);
    static_cast<void>((*host).request_focus(navigator_));
    navigator_open_ = true;
    return true;
}

// Closing order: focus returns (the scope restores it), the window drops the
// overlay, then the site forgets it.
void DockSite::close_navigator() noexcept {
    if (!navigator_open_ || navigator_closing_) return;
    navigator_closing_ = true;
    Window* const host = attached_window();
    if (host != nullptr && navigator_scope_) {
        try {
            static_cast<void>((*host).end_focus_scope(navigator_scope_));
        } catch (...) {
        }
    }
    navigator_scope_ = {};
    window_pointer_.disconnect();
    navigator_revoked_.disconnect();
    navigator_chosen_.disconnect();
    navigator_cancelled_.disconnect();
    navigator_token_.disconnect();
    navigator_.reset();
    navigator_open_ = false;
    navigator_closing_ = false;
}

void DockSite::on_navigator_revoked() {
    if (!navigator_open_ || navigator_closing_) return;
    Window* const host = attached_window();
    if (host != nullptr && navigator_scope_) {
        try {
            static_cast<void>((*host).end_focus_scope(navigator_scope_, FocusScopeCloseReason::owner_unavailable));
        } catch (...) {
        }
    }
    navigator_scope_ = {};
    close_navigator();
}

void DockSite::on_navigator_chosen(const DockNavigatorChoice& choice) {
    const DockPanelId panel = choice.panel;
    close_navigator();
    if (workspace_ != nullptr) static_cast<void>((*workspace_).activate_panel(panel, DockActivationCause::navigator));
}

void DockSite::on_navigator_cancelled() {
    close_navigator();
}

// A press outside the navigator closes it and passes through.
void DockSite::on_window_pointer(const PointerEvent& event) {
    if (!navigator_open_ || event.action != PointerAction::down) return;
    close_navigator();
}

// -- semantics -----------------------------------------------------------------

std::string DockSite::seam_identity(DockNodeId split) const {
    const std::string identity = std::string(stable_id().value()) + ".seam." + std::to_string(split.value);
    return identity;
}

std::string DockSite::stack_identity(DockNodeId stack) const {
    const std::string identity = std::string(stable_id().value()) + ".stack." + std::to_string(stack.value);
    return identity;
}

SemanticDescriptor DockSite::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.read().empty() ? "Docking workspace" : accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

// Each seam is a split pane whose value is the first side's extent, between
// the two sides' minimums.
std::vector<SemanticNode> DockSite::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes{};
    nodes.reserve(seams_.size() + 1U);
    const Rect absolute = absolute_bounds();
    const bool enabled = effectively_enabled();
    const SemanticState enabled_state = enabled ? SemanticState::enabled : SemanticState::none;
    for (std::size_t seam = 0; seam < seams_.size(); ++seam) {
        const SeamSlot& slot = seams_[seam];
        SemanticNode node{};
        node.stable_id = seam_identity(slot.split);
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::split_pane;
        node.name = slot.axis == DockAxis::horizontal ? "Vertical seam" : "Horizontal seam";
        node.numeric_value = slot.first;
        node.minimum_value = slot.first_minimum;
        node.maximum_value = std::max(slot.first_minimum, slot.span - slot.second_minimum);
        node.value = std::to_string(static_cast<long long>(std::lround(slot.first)));
        node.bounds = to_window(absolute, slot.bar);
        node.states = SemanticState::visible | SemanticState::focusable | enabled_state;
        if (focused_ && keyboard_seam_ == seam) node.states |= SemanticState::focused;
        node.actions = {SemanticAction::focus, SemanticAction::increment, SemanticAction::decrement,
                        SemanticAction::set_value};
        nodes.push_back(std::move(node));
    }
    // The move mode's announcements (DK-51): a live region, always present
    // so that a reader knows it before it changes, named by the latest
    // announcement (empty before the first drag). It covers the site and
    // takes no action.
    SemanticNode announcements{};
    announcements.stable_id = std::string(stable_id().value()) + ".announcements";
    announcements.runtime_id = virtual_semantic_runtime_id(announcements.stable_id);
    announcements.role = SemanticRole::live_region;
    announcements.name = drag_.announcement;
    announcements.bounds = absolute;
    announcements.states = SemanticState::visible;
    if (enabled) announcements.states |= SemanticState::enabled;
    nodes.push_back(std::move(announcements));
    return nodes;
}

bool DockSite::on_semantic_child_action(std::string_view stable_id, SemanticAction action, std::string_view value) {
    for (std::size_t seam = 0; seam < seams_.size(); ++seam) {
        if (seam_identity(seams_[seam].split) != stable_id) continue;
        if (action == SemanticAction::focus) return focus_seam(seam);
        if (action == SemanticAction::increment) return move_seam(seam, keyboard_increment.read());
        if (action == SemanticAction::decrement) return move_seam(seam, -keyboard_increment.read());
        if (action != SemanticAction::set_value) return false;
        double requested = 0.0;
        const char* const begin = value.data();
        const char* const end = value.data() + value.size();
        const std::from_chars_result parsed = std::from_chars(begin, end, requested);
        if (parsed.ec != std::errc{} || parsed.ptr != end) return false;
        const bool set = set_seam_first(seam, requested);
        return set;
    }
    return false;
}

// A tool stack's caption buttons, as children of its pane: the position
// menu, the pin (root-level stacks only; checked while docked) and close.
std::vector<SemanticNode> DockSite::caption_nodes(const StackSlot& slot, std::size_t index) const {
    std::vector<SemanticNode> nodes{};
    const bool shown = !slot.collapsed || slot.in_flyout;
    if (!shown || slot.kind != DockNodeKind::tool_stack || slot.caption.empty() || !slot.view) return nodes;
    const CaptionGeometry placed = caption_geometry(slot.caption, pin_shown(slot), close_shown(slot));
    const Rect absolute = absolute_bounds();
    const std::string prefix((*slot.view).stable_id().value());
    nodes.reserve(3U);
    SemanticNode menu{};
    menu.stable_id = prefix + ".menu";
    menu.runtime_id = virtual_semantic_runtime_id(menu.stable_id);
    menu.role = SemanticRole::menu_button;
    menu.name = "Window position";
    menu.has_popup = true;
    menu.bounds = to_window(absolute, placed.menu);
    menu.states = SemanticState::visible;
    if (effectively_enabled()) menu.states |= SemanticState::enabled;
    if (menu_open_ && menu_stack_ == index) menu.states |= SemanticState::expanded;
    menu.actions = {SemanticAction::press, SemanticAction::show_menu};
    nodes.push_back(std::move(menu));
    if (!placed.pin.empty()) {
        SemanticNode pin{};
        pin.stable_id = prefix + ".pin";
        pin.runtime_id = virtual_semantic_runtime_id(pin.stable_id);
        pin.role = SemanticRole::check_box;
        pin.name = "Pinned";
        pin.bounds = to_window(absolute, placed.pin);
        const AnatomyStateSet pin_states = button_states(index, Zone::pin);
        pin.states = SemanticState::visible | SemanticState::checkable;
        if (pin_states.contains(AnatomyState::checked)) pin.states |= SemanticState::checked;
        if (!pin_states.contains(AnatomyState::disabled)) {
            pin.states |= SemanticState::enabled;
            pin.actions = {SemanticAction::press};
        }
        nodes.push_back(std::move(pin));
    }
    if (!close_shown(slot)) return nodes;
    SemanticNode close{};
    close.stable_id = prefix + ".close";
    close.runtime_id = virtual_semantic_runtime_id(close.stable_id);
    close.role = SemanticRole::button;
    close.name = "Close";
    close.bounds = to_window(absolute, placed.close);
    close.states = SemanticState::visible;
    if (!button_states(index, Zone::close).contains(AnatomyState::disabled)) close.states |= SemanticState::enabled;
    close.actions = {SemanticAction::press};
    nodes.push_back(std::move(close));
    return nodes;
}

bool DockSite::caption_action(std::size_t index, std::string_view stable_id, SemanticAction action) {
    if (index >= stacks_.size() || !stacks_[index].view) return false;
    const StackSlot& slot = stacks_[index];
    const std::string prefix((*slot.view).stable_id().value());
    const std::optional<DockPanelId> panel = selected_panel(slot);
    if (!panel) return false;
    if (stable_id == prefix + ".menu" && (action == SemanticAction::press || action == SemanticAction::show_menu)) {
        const bool shown = show_position_menu(*panel, MenuOpenMode::keyboard);
        return shown;
    }
    if (stable_id == prefix + ".close" && action == SemanticAction::press) {
        const DockPositionCommand command = position_command_enabled(*panel, DockPositionCommand::hide)
                                                ? DockPositionCommand::hide
                                                : DockPositionCommand::close;
        const bool ran = run_position_command(*panel, command);
        return ran;
    }
    if (stable_id == prefix + ".pin" && action == SemanticAction::press) {
        const DockPositionCommand command = slot.auto_hidden ? DockPositionCommand::dock : DockPositionCommand::auto_hide;
        const bool ran = run_position_command(*panel, command);
        return ran;
    }
    return false;
}

// -- anatomy -------------------------------------------------------------------

namespace {

constexpr std::array<AnatomyState, 1> drop_area_states{AnatomyState::drop_accepted};
constexpr std::array<AnatomyState, 1> drop_cue_states{AnatomyState::drop_rejected};
// The flyout grip's own states.
constexpr std::array<AnatomyState, 2> flyout_grip_states{AnatomyState::hot, AnatomyState::captured};

constexpr std::array<PartField<DockSite::Parts>, 11> site_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &DockSite::Parts::region},
    {.declaration = {.name = "document_area", .role = PartRole::well, .composed_from = {}},
     .member = &DockSite::Parts::document_area},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &DockSite::Parts::focus},
    // The drag layer (phase D3).
    {.declaration = {.name = "drag_stamp", .role = PartRole::face, .composed_from = {}},
     .member = &DockSite::Parts::drag_stamp},
    {.declaration = {.name = "drag_stamp_label", .role = PartRole::label, .composed_from = {}},
     .member = &DockSite::Parts::drag_stamp_label},
    // The panel's picture: an image part; icon is the nearest role.
    {.declaration = {.name = "drag_stamp_image", .role = PartRole::image, .composed_from = {}},
     .member = &DockSite::Parts::drag_stamp_image},
    {.declaration = {.name = "drop_area", .role = PartRole::drop, .composed_from = {},
                     .states = drop_area_states},
     .member = &DockSite::Parts::drop_area},
    // Where nothing accepts the drop: the rejected cue (D4-1). No mark says
    // "refused"; it draws the close mark, the nearest atom.
    {.declaration = {.name = "drop_cue", .role = PartRole::indicator, .composed_from = {},
                     .states = drop_cue_states},
     .member = &DockSite::Parts::drop_cue},
    // The flyout's overlay shadow (RB-37, DK-41 T5-1), face and inner-edge
    // grip.
    {.declaration = {.name = "flyout_shadow", .role = PartRole::shadow, .composed_from = {}},
     .member = &DockSite::Parts::flyout_shadow},
    {.declaration = {.name = "flyout_face", .role = PartRole::face, .composed_from = {}},
     .member = &DockSite::Parts::flyout_face},
    {.declaration = {.name = "flyout_grip", .role = PartRole::grip, .composed_from = {}, .states = flyout_grip_states},
     .member = &DockSite::Parts::flyout_grip},
}};
constexpr std::array<PartDeclaration, 11> site_parts = part_declarations(site_part_fields);
constexpr std::array<AnatomyState, 5> site_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
    AnatomyState::focused, AnatomyState::focus_visible,
};

constexpr std::array<PartField<DockSite::SeamParts>, 2> seam_part_fields{{
    {.declaration = {.name = "seam_bar", .role = PartRole::seam, .composed_from = {}},
     .member = &DockSite::SeamParts::seam_bar},
    {.declaration = {.name = "seam_grip", .role = PartRole::grip, .composed_from = {}},
     .member = &DockSite::SeamParts::seam_grip},
}};
constexpr std::array<PartDeclaration, 2> seam_parts_declared = part_declarations(seam_part_fields);
constexpr std::array<StateField<DockSite::SeamStates>, 4> seam_state_fields{{
    {AnatomyState::hot, &DockSite::SeamStates::hot},
    {AnatomyState::captured, &DockSite::SeamStates::captured},
    {AnatomyState::focus_visible, &DockSite::SeamStates::focus_visible},
    {AnatomyState::disabled, &DockSite::SeamStates::disabled},
}};
constexpr std::array<AnatomyState, 4> seam_states_declared = state_declarations(seam_state_fields);

// The caption buttons' own states, and their glyphs'.
constexpr std::array<AnatomyState, 4> menu_button_states{AnatomyState::hot, AnatomyState::pressed,
                                                         AnatomyState::open, AnatomyState::disabled};
constexpr std::array<AnatomyState, 4> pin_button_states{AnatomyState::hot, AnatomyState::pressed,
                                                        AnatomyState::checked, AnatomyState::disabled};
constexpr std::array<AnatomyState, 3> close_button_states{AnatomyState::hot, AnatomyState::pressed,
                                                          AnatomyState::disabled};
constexpr std::array<AnatomyState, 2> pin_glyph_states{AnatomyState::checked, AnatomyState::disabled};
constexpr std::array<AnatomyState, 1> glyph_states{AnatomyState::disabled};

constexpr std::array<PartField<DockSite::StackParts>, 11> stack_part_fields{{
    {.declaration = {.name = "stack_area", .role = PartRole::region, .composed_from = "page_view"},
     .member = &DockSite::StackParts::stack_area},
    {.declaration = {.name = "caption", .role = PartRole::face, .composed_from = {}},
     .member = &DockSite::StackParts::caption},
    // The active caption's accent line (DK-41): no accent-line role exists;
    // keyline is the nearest atom.
    {.declaration = {.name = "caption_accent", .role = PartRole::keyline, .composed_from = {}},
     .member = &DockSite::StackParts::caption_accent},
    // The identity key: chip is the nearest atom for a small coloured mark.
    {.declaration = {.name = "caption_key", .role = PartRole::chip, .composed_from = {}},
     .member = &DockSite::StackParts::caption_key},
    {.declaration = {.name = "caption_label", .role = PartRole::caption, .composed_from = {}},
     .member = &DockSite::StackParts::caption_label},
    {.declaration = {.name = "caption_menu", .role = PartRole::actuator, .composed_from = "button",
                     .states = menu_button_states},
     .member = &DockSite::StackParts::caption_menu},
    {.declaration = {.name = "caption_menu_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = glyph_states},
     .member = &DockSite::StackParts::caption_menu_glyph},
    {.declaration = {.name = "caption_pin", .role = PartRole::actuator, .composed_from = "button",
                     .states = pin_button_states},
     .member = &DockSite::StackParts::caption_pin},
    {.declaration = {.name = "caption_pin_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = pin_glyph_states},
     .member = &DockSite::StackParts::caption_pin_glyph},
    {.declaration = {.name = "caption_close", .role = PartRole::actuator, .composed_from = "button",
                     .states = close_button_states},
     .member = &DockSite::StackParts::caption_close},
    {.declaration = {.name = "caption_close_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = glyph_states},
     .member = &DockSite::StackParts::caption_close_glyph},
}};
constexpr std::array<PartDeclaration, 11> stack_parts_declared = part_declarations(stack_part_fields);
constexpr std::array<StateField<DockSite::StackStates>, 2> stack_state_fields{{
    {AnatomyState::current, &DockSite::StackStates::current},
    {AnatomyState::disabled, &DockSite::StackStates::disabled},
}};
constexpr std::array<AnatomyState, 2> stack_states_declared = state_declarations(stack_state_fields);

// A drop tile: its plate (lit while selected, disabled where the
// restrictions refuse, expanding while the cross fades in) and its
// dock-shape glyph (PartMark::dock_shape with the zone's region).
constexpr std::array<PartField<DockSite::TileParts>, 2> tile_part_fields{{
    {.declaration = {.name = "tile", .role = PartRole::actuator, .composed_from = {}},
     .member = &DockSite::TileParts::tile},
    {.declaration = {.name = "tile_glyph", .role = PartRole::glyph, .composed_from = {}},
     .member = &DockSite::TileParts::tile_glyph},
}};
constexpr std::array<PartDeclaration, 2> tile_parts_declared = part_declarations(tile_part_fields);
constexpr std::array<StateField<DockSite::TileStates>, 3> tile_state_fields{{
    {AnatomyState::selected, &DockSite::TileStates::selected},
    {AnatomyState::disabled, &DockSite::TileStates::disabled},
    {AnatomyState::expanding, &DockSite::TileStates::expanding},
}};
constexpr std::array<AnatomyState, 3> tile_states_declared = state_declarations(tile_state_fields);

constexpr std::array<ItemSystemDeclaration, 3> site_item_systems{{
    {.name = "seam", .parts = seam_parts_declared, .states = seam_states_declared, .custom_states = {}},
    {.name = "stack", .parts = stack_parts_declared, .states = stack_states_declared, .custom_states = {}},
    {.name = "drop_tile", .parts = tile_parts_declared, .states = tile_states_declared, .custom_states = {}},
}};

constexpr ControlAnatomy site_anatomy{
    .control_id = "dock_site",
    .parts = site_parts,
    .states = site_states,
    .custom_states = {},
    .item_systems = site_item_systems,
};

// Only the states a part declares are published with it.
AnatomyStateSet declared_only(AnatomyStateSet states, std::span<const AnatomyState> declared) noexcept {
    AnatomyStateSet kept{};
    for (const AnatomyState state : declared) {
        if (states.contains(state)) kept.insert(state);
    }
    return kept;
}

PartGeometry with_states(Rect bounds, AnatomyStateSet states, std::span<const AnatomyState> declared) noexcept {
    PartGeometry geometry = PartGeometry::from_bounds(bounds);
    geometry.states = declared_only(states, declared);
    return geometry;
}

} // namespace

const ControlAnatomy& DockSite::anatomy() const noexcept {
    return site_anatomy;
}

void DockSite::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect surface = client_rectangle();
    Parts record{};
    record.region = PartGeometry::from_bounds(surface);
    record.document_area = PartGeometry::from_bounds(document_area_);
    record.focus = PartGeometry::from_bounds(surface);
    const bool layer = drag_.phase == DockDragPhase::dragging || drag_.phase == DockDragPhase::settling;
    if (layer) {
        record.drag_stamp = PartGeometry::from_bounds(drag_.stamp);
        record.drag_stamp_label = PartGeometry::from_bounds(drag_label_bounds());
        if (drag_.picture) record.drag_stamp_image = PartGeometry::from_bounds(drag_image_bounds());
    }
    const std::optional<DockDropTarget> target = drag_target();
    if (target) {
        record.drop_area = PartGeometry::from_bounds((*target).highlight);
        record.drop_area.states.insert(AnatomyState::drop_accepted);
    }
    const Rect cue = drop_cue_bounds();
    if (!cue.empty()) {
        record.drop_cue = PartGeometry::from_bounds(cue);
        record.drop_cue.states.insert(AnatomyState::drop_rejected);
    }
    if (flyout_panel_) {
        const FlyoutGeometry placed = flyout_geometry();
        record.flyout_shadow = PartGeometry::from_bounds(placed.full);
        record.flyout_face = PartGeometry::from_bounds(placed.full);
        AnatomyStateSet grip{};
        if (hot_.zone == Zone::grip) grip.insert(AnatomyState::hot);
        if (grip_captured_) grip.insert(AnatomyState::captured);
        record.flyout_grip = with_states(placed.grip, grip, flyout_grip_states);
    }
    write_parts(anatomy(), site_part_fields, record, geometry);
}

DockSite::Parts DockSite::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record{};
    read_parts(anatomy(), site_part_fields, published.parts(), record);
    return record;
}

DockSite::SeamParts DockSite::seam_parts(std::size_t seam) const {
    const SeamSlot& slot = seams_[seam];
    SeamParts record{};
    record.seam_bar = PartGeometry::from_bounds(slot.bar);
    const bool horizontal = slot.axis == DockAxis::horizontal;
    const double length = std::min(seam_grip_length, horizontal ? slot.bar.height : slot.bar.width);
    const Rect grip = horizontal ? centred(slot.bar, slot.bar.width, length) : centred(slot.bar, length, slot.bar.height);
    record.seam_grip = PartGeometry::from_bounds(grip);
    return record;
}

DockSite::StackParts DockSite::stack_parts(const StackSlot& slot, std::size_t index) const {
    StackParts record{};
    record.stack_area = PartGeometry::from_bounds(slot.bounds);
    if (slot.kind != DockNodeKind::tool_stack || slot.caption.empty()) return record;
    const CaptionGeometry placed = caption_geometry(slot.caption, pin_shown(slot), close_shown(slot));
    record.caption = PartGeometry::from_bounds(placed.caption);
    if (stack_current(slot)) record.caption_accent = PartGeometry::from_bounds(placed.accent);
    record.caption_key = PartGeometry::from_bounds(placed.key);
    record.caption_label = PartGeometry::from_bounds(placed.label);
    const AnatomyStateSet menu = button_states(index, Zone::menu);
    const AnatomyStateSet pin = button_states(index, Zone::pin);
    const AnatomyStateSet close = button_states(index, Zone::close);
    record.caption_menu = with_states(placed.menu, menu, menu_button_states);
    record.caption_menu_glyph = with_states(placed.menu_glyph, menu, glyph_states);
    record.caption_pin = with_states(placed.pin, pin, pin_button_states);
    record.caption_pin_glyph = with_states(placed.pin_glyph, pin, pin_glyph_states);
    record.caption_close = with_states(placed.close, close, close_button_states);
    record.caption_close_glyph = with_states(placed.close_glyph, close, glyph_states);
    return record;
}

// The stacks shown: those in the tree that are not collapsed, then the
// open flyout's.
std::size_t DockSite::visible_stack_count() const {
    std::size_t count = 0U;
    for (const StackSlot& slot : stacks_) {
        if (!slot.collapsed) ++count;
    }
    if (flyout_slot_index() < stacks_.size()) ++count;
    return count;
}

// The index in stacks_ of the ordinal-th stack shown.
std::size_t DockSite::visible_stack_at(std::size_t ordinal) const {
    std::size_t seen = 0U;
    for (std::size_t index = 0; index < stacks_.size(); ++index) {
        if (stacks_[index].collapsed) continue;
        if (seen == ordinal) return index;
        ++seen;
    }
    const std::size_t flyout = flyout_slot_index();
    if (flyout < stacks_.size() && seen == ordinal) return flyout;
    return stacks_.size();
}

std::size_t DockSite::realized_item_count() const {
    const std::size_t count = seams_.size() + visible_stack_count() + tile_count();
    return count;
}

ItemSystemIndex DockSite::publish_realized_item_system(std::size_t item) const {
    std::uint8_t system = 0U;
    if (item >= seams_.size()) system = 1U;
    if (item >= seams_.size() + visible_stack_count()) system = 2U;
    const ItemSystemIndex index{system};
    return index;
}

std::string DockSite::realized_item_stable_id(std::size_t item) const {
    if (item >= realized_item_count()) throw std::out_of_range("DockSite item is out of range");
    if (item < seams_.size()) return seam_identity(seams_[item].split);
    const std::size_t first_tile = seams_.size() + visible_stack_count();
    if (item >= first_tile) return tile_identity(tile_at(item - first_tile));
    const std::size_t index = visible_stack_at(item - seams_.size());
    const std::string identity = stack_identity(stacks_[index].stack);
    return identity;
}

AnatomyStateSet DockSite::realized_item_anatomy_states(std::size_t item) const {
    if (item >= realized_item_count()) throw std::out_of_range("DockSite item is out of range");
    if (item < seams_.size()) return seam_states(item);
    const std::size_t first_tile = seams_.size() + visible_stack_count();
    if (item >= first_tile) return tile_states(tile_at(item - first_tile));
    const std::size_t index = visible_stack_at(item - seams_.size());
    const AnatomyStateSet states = stack_states(stacks_[index]);
    return states;
}

void DockSite::publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const {
    if (item < seams_.size()) {
        const SeamParts record = seam_parts(item);
        write_declared_parts(seam_parts_declared, seam_part_fields, record, geometry);
        return;
    }
    const std::size_t first_tile = seams_.size() + visible_stack_count();
    if (item >= first_tile) {
        const TileSlot tile = tile_at(item - first_tile);
        TileParts record{};
        record.tile = PartGeometry::from_bounds(tile.bounds);
        record.tile_glyph = PartGeometry::from_bounds(tile_glyph_bounds(tile.bounds));
        write_declared_parts(tile_parts_declared, tile_part_fields, record, geometry);
        return;
    }
    const std::size_t index = visible_stack_at(item - seams_.size());
    const StackParts record = stack_parts(stacks_[index], index);
    write_declared_parts(stack_parts_declared, stack_part_fields, record, geometry);
}

std::vector<DockSite::Seam> DockSite::realized_seams() const {
    std::vector<Seam> seams{};
    seams.reserve(seams_.size());
    for (std::size_t seam = 0; seam < seams_.size(); ++seam) {
        Seam record{};
        record.split = seams_[seam].split;
        record.axis = seams_[seam].axis;
        record.stable_id = seam_identity(record.split);
        const PartGeometryBuffer published = published_realized_item_geometry(seam);
        read_declared_parts(seam_parts_declared, seam_part_fields, published.parts(), record.parts);
        read_states(seam_state_fields, seam_states(seam), record.states);
        seams.push_back(std::move(record));
    }
    return seams;
}

std::vector<DockSite::Tile> DockSite::realized_tiles() const {
    std::vector<Tile> tiles{};
    const std::size_t count = tile_count();
    tiles.reserve(count);
    const std::size_t first_tile = seams_.size() + visible_stack_count();
    for (std::size_t index = 0; index < count; ++index) {
        const TileSlot slot = tile_at(index);
        Tile record{};
        record.kind = slot.kind;
        record.pane = slot.pane;
        record.zone = slot.zone;
        record.stable_id = tile_identity(slot);
        const PartGeometryBuffer published = published_realized_item_geometry(first_tile + index);
        read_declared_parts(tile_parts_declared, tile_part_fields, published.parts(), record.parts);
        read_states(tile_state_fields, tile_states(slot), record.states);
        tiles.push_back(std::move(record));
    }
    return tiles;
}

std::vector<DockSite::Stack> DockSite::realized_stack_items() const {
    std::vector<Stack> stacks{};
    const std::size_t count = visible_stack_count();
    stacks.reserve(count);
    for (std::size_t ordinal = 0; ordinal < count; ++ordinal) {
        const std::size_t index = visible_stack_at(ordinal);
        const StackSlot& slot = stacks_[index];
        Stack record{};
        record.stack = slot.stack;
        record.kind = slot.kind;
        record.panel = selected_panel(slot).value_or(DockPanelId{});
        record.stable_id = stack_identity(slot.stack);
        const PartGeometryBuffer published = published_realized_item_geometry(seams_.size() + ordinal);
        read_declared_parts(stack_parts_declared, stack_part_fields, published.parts(), record.parts);
        read_states(stack_state_fields, stack_states(slot), record.states);
        stacks.push_back(std::move(record));
    }
    return stacks;
}

namespace {

// The events DockSite adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 2> site_interactions{{
    {.name = "drag_ended", .payload = "DockDragOutcome", .mutable_payload = false},
    {.name = "flyout_changed", .payload = "DockFlyoutChange", .mutable_payload = false},
}};

} // namespace

void DockSite::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), site_interactions.begin(), site_interactions.end());
}

// property_codemod begin DockSite (tools/property_families/dock_site.json; generated, do not edit)

void DockSite::prepare_write(const decltype(keyboard_increment)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 1.0 || incoming > 256.0) {
        throw std::out_of_range("DockSite keyboard increment must lie in 1 to 256");
    }
}

void DockSite::written(const decltype(keyboard_increment)&) {}

void DockSite::prepare_write(const decltype(drag_threshold)&, double& incoming) {
    require_mutable();
    if (!drag_gesture::valid_threshold(incoming)) {
        throw std::out_of_range("DockSite drag threshold must lie in 1 to 64");
    }
}

void DockSite::written(const decltype(drag_threshold)&) {}

// property_codemod end DockSite

} // namespace gui_forms
