// DockSite's auto-hide (stage DK phase D5; owner decisions DK-27, DK-28,
// DK-41, DK-42): the side strips, the flyout and its timers. The layout pass
// that sizes the strips' bands and plans collapsing is in dock_site.cpp.

#include "gui_forms/controls/docking/dock_site/dock_site.hpp"

#include "dock_site_internal.hpp"

#include "gui_forms/delegate.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace gui_forms {
namespace {

using dock_site_detail::focus_candidate;
using dock_site_detail::moved;
using dock_site_detail::seam_bar_thickness;
using dock_site_detail::strip_extent;

// The flyout's grip along its inner edge, its smallest extent, and the
// extent of a collapsed stack's flyout when nothing else gives one, in
// logical units.
constexpr double flyout_grip_thickness = 5.0;
constexpr double flyout_minimum_extent = 48.0;
constexpr double flyout_fallback_extent = 240.0;
// The repeat interval of a timer that fires once: its target disconnects it
// when it fires, so the repeat is never reached.
constexpr std::chrono::hours one_shot_interval{24};

constexpr std::array<std::string_view, 4> edge_names{"left", "top", "right", "bottom"};

std::size_t edge_index(DockSide edge) noexcept {
    const std::size_t index = static_cast<std::size_t>(edge);
    return index;
}

bool across(DockSide edge) noexcept {
    const bool horizontal = edge == DockSide::left || edge == DockSide::right;
    return horizontal;
}

PageStripPlacement placement_for(DockSide edge) noexcept {
    if (edge == DockSide::left) return PageStripPlacement::left;
    if (edge == DockSide::right) return PageStripPlacement::right;
    if (edge == DockSide::top) return PageStripPlacement::top;
    return PageStripPlacement::bottom;
}

} // namespace

// -- timer targets ---------------------------------------------------------------

// Each fires on the window's UI timer and finds the site through a weak
// reference; a site that is gone or disposed does nothing.
struct DockSite::HoverElapsed final {
    std::weak_ptr<Control> target;
    DockPanelId panel{};

    void operator()(FrameTime) const {
        const std::shared_ptr<Control> control = target.lock();
        if (!control || !(*control).is_alive()) return;
        static_cast<DockSite&>(*control).hover_elapsed(panel);
    }
};

struct DockSite::LeaveElapsed final {
    std::weak_ptr<Control> target;

    void operator()(FrameTime) const {
        const std::shared_ptr<Control> control = target.lock();
        if (!control || !(*control).is_alive()) return;
        static_cast<DockSite&>(*control).leave_elapsed();
    }
};

struct DockSite::StaleFlyout final {
    std::weak_ptr<Control> target;

    void operator()(FrameTime) const {
        const std::shared_ptr<Control> control = target.lock();
        if (!control || !(*control).is_alive()) return;
        static_cast<DockSite&>(*control).stale_flyout_elapsed();
    }
};

// -- the flyout control ------------------------------------------------------------

// The flyout: a window overlay root holding the stack's view while it is
// out. It observes the site, which owns it and releases it (release_site)
// before letting it go. Its bounds are the revealed part of the flyout; the
// view is laid out where the whole flyout stands in its slide, so the part
// still under the strip is clipped.
class DockSite::Flyout final : public Control {
public:
    Flyout(StableId stable_id, DockSite& site) : Control(std::move(stable_id)), site_(&site) {
        set_paint_plane(PaintPlane::overlay);
        cursor = CursorKind::arrow;
        // Focus lands here when the panel has nothing focusable, so Escape
        // still reaches the flyout.
        focusable = true;
    }

    void release_site() noexcept { site_ = nullptr; }

    // Takes the stack's view and lays it out at once, so its controls can
    // take focus before the next layout pass.
    void adopt(const Control::Ptr& view) {
        add_child(view);
        set_child_layout_collapsed(view, false);
        if (site_ == nullptr) return;
        const FlyoutGeometry placed = (*site_).flyout_geometry();
        const Point offset{-placed.revealed.x, -placed.revealed.y};
        set_child_layout(view, moved(placed.content, offset));
    }

    void arrange(Rect final_bounds) override {
        arrange_self(final_bounds);
        if (site_ == nullptr) return;
        const FlyoutGeometry placed = (*site_).flyout_geometry();
        const Point offset{-placed.revealed.x, -placed.revealed.y};
        const Rect content = moved(placed.content, offset);
        const std::vector<Control::Ptr> children = snapshot_layout_children();
        for (const Control::Ptr& child : children) {
            if (!is_current_layout_child(child)) continue;
            set_child_layout_collapsed(child, false);
            set_child_layout(child, content);
        }
    }

    void on_paint(Painter& painter, Rect) override {
        if (site_ != nullptr) (*site_).paint_flyout(painter, *this);
    }

    [[nodiscard]] Insets visual_outsets() const noexcept override {
        const Insets shadow = overlay_shadow_outsets(*this);
        return shadow;
    }

    void on_pointer_preview(PointerEvent& event) override {
        if (site_ != nullptr) (*site_).on_flyout_pointer_preview(event);
    }

    void on_pointer(PointerEvent& event) override {
        if (site_ != nullptr) (*site_).on_flyout_pointer(event);
    }

    void on_key(KeyEvent& event) override {
        if (site_ != nullptr) (*site_).on_flyout_key(event);
    }

    void on_key_bubble(KeyEvent& event) override {
        if (site_ != nullptr) (*site_).on_flyout_key(event);
    }

    // The stack's view inside is the pane; the flyout itself is not exposed.
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override {
        SemanticDescriptor descriptor{};
        descriptor.exposed = false;
        descriptor.include_descendants = true;
        return descriptor;
    }

private:
    DockSite* site_{};
};

// -- side strips ----------------------------------------------------------------------

DockSite::SideStrip& DockSite::side(DockSide edge) noexcept {
    return sides_[edge_index(edge)];
}

const DockSite::SideStrip& DockSite::side(DockSide edge) const noexcept {
    return sides_[edge_index(edge)];
}

void DockSite::ensure_side_strip(DockSide edge) {
    SideStrip& entry = side(edge);
    if (entry.strip) return;
    entry.pages = std::make_unique<PageSet>();
    const std::string identity =
        std::string(stable_id().value()) + ".side." + std::string(edge_names[edge_index(edge)]);
    entry.strip = make_control<PageStrip>(StableId(identity), *entry.pages);
    PageStrip& strip = *entry.strip;
    strip.placement = placement_for(edge);
    // The arrows move along the tabs; Enter or Space opens the flyout.
    strip.activation = PageStripActivation::manual;
    strip.mnemonics_enabled = false;
    strip.close_buttons = PageCloseButtons::none;
    strip.accessible_name = "Auto-hidden panels";
    on(strip.page_invoked(), *this, &DockSite::on_side_tab_invoked, edge);
    add_child(entry.strip);
    set_child_layout_collapsed(entry.strip, true);
}

void DockSite::release_side_strips() noexcept {
    for (SideStrip& entry : sides_) {
        if (entry.strip) {
            (*entry.strip).release_page_set();
            try {
                static_cast<void>(remove_child((*entry.strip).runtime_id()));
            } catch (...) {
            }
        }
        entry.strip.reset();
        entry.pages.reset();
        entry.panels.clear();
        entry.band = {};
    }
}

double DockSite::side_band_thickness() const {
    const double thickness = strip_extent(*this);
    return thickness;
}

// An edge's tabs: the auto-hidden stacks on it in layout order, then the
// stacks collapsed onto it in tree order, each stack's panels in its order.
// A site without a workspace shows no side tabs.
void DockSite::collect_side_panels(DockSide edge, std::vector<DockPanelId>& out) const {
    out.clear();
    if (workspace_ == nullptr) return;
    const DockLayout& layout = (*workspace_).layout();
    for (int pass = 0; pass < 2; ++pass) {
        for (const StackSlot& slot : stacks_) {
            const bool behind = pass == 0 ? slot.auto_hidden : (slot.side_tabbed && !slot.auto_hidden);
            if (!behind || slot.edge != edge) continue;
            const DockNode* const node = layout.find_node(slot.stack);
            if (node != nullptr) out.insert(out.end(), (*node).panels.begin(), (*node).panels.end());
        }
    }
}

// Each edge's strip is made on first use and kept (collapsed while empty);
// its pages follow collect_side_panels, the open flyout's tab selected.
void DockSite::sync_side_strips() {
    if (!is_alive()) return;
    bool membership_changed = false;
    std::vector<DockPanelId> wanted{};
    for (std::size_t edge = 0; edge < sides_.size(); ++edge) {
        const DockSide side_edge = static_cast<DockSide>(edge);
        collect_side_panels(side_edge, wanted);
        SideStrip& entry = sides_[edge];
        if (wanted.empty() && !entry.strip) continue;
        ensure_side_strip(side_edge);
        if (wanted != entry.panels) membership_changed = true;
        PageSet& pages = *entry.pages;
        {
            PageSetBatch batch(pages);
            std::size_t position = pages.count();
            while (position > 0U) {
                --position;
                const PageId page = pages.record_at(PagePosition{position}).id;
                if (std::find(wanted.begin(), wanted.end(), DockPanelId(page.value)) == wanted.end()) {
                    static_cast<void>(pages.remove(page));
                }
            }
            for (std::size_t index = 0; index < wanted.size(); ++index) {
                const PageId page{wanted[index].value};
                const std::string title = (*workspace_).panel_title(wanted[index]);
                const std::optional<PagePosition> found = pages.find(page);
                if (!found) {
                    PageRecord record{};
                    record.id = page;
                    record.title = title;
                    static_cast<void>(pages.insert(record, PagePlacement::at(PagePosition{index})));
                    continue;
                }
                if ((*found).value != index) static_cast<void>(pages.move(page, PagePosition{index}));
                if (pages.record_at(PagePosition{index}).title != title) static_cast<void>(pages.set_title(page, title));
            }
            // The tab of the open flyout is the selected one.
            if (flyout_panel_ && pages.find(PageId{(*flyout_panel_).value})) {
                static_cast<void>(pages.select(PageId{(*flyout_panel_).value}, PageSelectionCause::programmatic));
            }
            batch.close();
        }
        entry.panels = wanted;
    }
    if (membership_changed) update_focus_regions();
}

Rect DockSite::tree_area(Size surface, const std::array<bool, 4>& edges, std::array<Rect, 4>& bands) const {
    const double thickness = side_band_thickness();
    const double width = std::max(0.0, surface.width);
    const double height = std::max(0.0, surface.height);
    const double left = edges[edge_index(DockSide::left)] ? std::min(thickness, width) : 0.0;
    const double right = edges[edge_index(DockSide::right)] ? std::min(thickness, std::max(0.0, width - left)) : 0.0;
    const double inner_width = std::max(0.0, width - left - right);
    const double top = edges[edge_index(DockSide::top)] ? std::min(thickness, height) : 0.0;
    const double bottom = edges[edge_index(DockSide::bottom)] ? std::min(thickness, std::max(0.0, height - top)) : 0.0;
    bands[edge_index(DockSide::left)] = {0.0, 0.0, left, height};
    bands[edge_index(DockSide::right)] = {width - right, 0.0, right, height};
    bands[edge_index(DockSide::top)] = {left, 0.0, inner_width, top};
    bands[edge_index(DockSide::bottom)] = {left, height - bottom, inner_width, bottom};
    const Rect area{left, top, inner_width, std::max(0.0, height - top - bottom)};
    return area;
}

// The smallest gap between the rectangle and an edge of the area; on equal
// gaps left, then right, then bottom, then top.
DockSide DockSite::nearest_edge(Rect bounds, Rect area) noexcept {
    const std::array<DockSide, 4> order{DockSide::left, DockSide::right, DockSide::bottom, DockSide::top};
    const std::array<double, 4> gaps{bounds.x - area.x, area.right() - bounds.right(), area.bottom() - bounds.bottom(),
                                     bounds.y - area.y};
    std::size_t best = 0U;
    for (std::size_t index = 1; index < order.size(); ++index) {
        if (gaps[index] < gaps[best] - 1e-9) best = index;
    }
    return order[best];
}

void DockSite::assign_collapsed_edges(Rect area, std::span<const DockNodeId> collapsed) {
    if (collapsed.empty() || workspace_ == nullptr) return;
    const DockLayout& layout = (*workspace_).layout();
    layout.arrange(layout.state().main_root, area, seam_bar_thickness, arranged_scratch_);
    for (StackSlot& slot : stacks_) {
        if (slot.auto_hidden || !dock_site_detail::contains_node(collapsed, slot.stack)) continue;
        for (const DockArrangedNode& placed : arranged_scratch_) {
            if (!(placed.node == slot.stack)) continue;
            slot.edge = nearest_edge(placed.bounds, area);
            break;
        }
    }
}

std::shared_ptr<PageStrip> DockSite::side_strip(DockSide edge) const {
    const SideStrip& entry = side(edge);
    if (!entry.strip || entry.panels.empty()) return {};
    return entry.strip;
}

std::vector<DockPanelId> DockSite::side_panels(DockSide edge) const {
    const std::vector<DockPanelId> panels = side(edge).panels;
    return panels;
}

// -- the flyout's geometry ----------------------------------------------------------------

DockSite::StackSlot* DockSite::side_slot_of(const DockPanelId& panel) {
    if (workspace_ == nullptr) return nullptr;
    const DockPanelLocation location = (*workspace_).layout().locate(panel);
    StackSlot* const slot = slot_for(location.stack);
    if (slot == nullptr || !((*slot).auto_hidden || (*slot).side_tabbed)) return nullptr;
    return slot;
}

std::size_t DockSite::flyout_slot_index() const noexcept {
    if (!flyout_panel_) return stacks_.size();
    for (std::size_t index = 0; index < stacks_.size(); ++index) {
        if (stacks_[index].stack == flyout_stack_ && stacks_[index].in_flyout) return index;
    }
    return stacks_.size();
}

// The grip's extent for the session, else the auto-hide entry's, else the
// collapsed stack's minimum across its edge (at least 240 units).
double DockSite::flyout_default_extent(const StackSlot& slot) const {
    for (const FlyoutExtent& kept : flyout_extents_) {
        if (kept.stack == slot.stack) return kept.extent;
    }
    if (workspace_ == nullptr) return flyout_fallback_extent;
    const DockLayout& layout = (*workspace_).layout();
    for (const DockAutoHideEntry& entry : layout.state().auto_hidden) {
        if (entry.stack == slot.stack) return entry.extent;
    }
    const DockNode* const node = layout.find_node(slot.stack);
    if (node == nullptr) return flyout_fallback_extent;
    const Size minimum = stack_minimum(*node);
    const double needed = across(slot.edge) ? minimum.width : minimum.height;
    const double extent = std::max(flyout_fallback_extent, needed);
    return extent;
}

DockSite::FlyoutGeometry DockSite::flyout_geometry() const {
    FlyoutGeometry placed{};
    const std::size_t index = flyout_slot_index();
    if (index >= stacks_.size()) return placed;
    const StackSlot& slot = stacks_[index];
    const Rect area = tree_area_;
    const bool horizontal = across(slot.edge);
    const double span = horizontal ? area.width : area.height;
    const double scale = effective_text_scale();
    const double smallest = std::min(flyout_minimum_extent * scale, span);
    const double extent = std::clamp(flyout_default_extent(slot), smallest, std::max(smallest, span));
    const double visible = extent * flyout_slide_.current();
    const double grip = std::min(flyout_grip_thickness * scale, extent);
    Rect inner{};
    if (slot.edge == DockSide::left) {
        placed.full = {area.x + visible - extent, area.y, extent, area.height};
        placed.revealed = {area.x, area.y, visible, area.height};
        placed.grip = {placed.full.right() - grip, placed.full.y, grip, placed.full.height};
        inner = {placed.full.x, placed.full.y, extent - grip, placed.full.height};
    } else if (slot.edge == DockSide::right) {
        placed.full = {area.right() - visible, area.y, extent, area.height};
        placed.revealed = {area.right() - visible, area.y, visible, area.height};
        placed.grip = {placed.full.x, placed.full.y, grip, placed.full.height};
        inner = {placed.full.x + grip, placed.full.y, extent - grip, placed.full.height};
    } else if (slot.edge == DockSide::top) {
        placed.full = {area.x, area.y + visible - extent, area.width, extent};
        placed.revealed = {area.x, area.y, area.width, visible};
        placed.grip = {placed.full.x, placed.full.bottom() - grip, placed.full.width, grip};
        inner = {placed.full.x, placed.full.y, placed.full.width, extent - grip};
    } else {
        placed.full = {area.x, area.bottom() - visible, area.width, extent};
        placed.revealed = {area.x, area.bottom() - visible, area.width, visible};
        placed.grip = {placed.full.x, placed.full.y, placed.full.width, grip};
        inner = {placed.full.x, placed.full.y + grip, placed.full.width, extent - grip};
    }
    placed.caption = {inner.x, inner.y, inner.width, std::min(inner.height, caption_height())};
    placed.content = {inner.x, placed.caption.bottom(), inner.width, std::max(0.0, inner.bottom() - placed.caption.bottom())};
    return placed;
}

double DockSite::flyout_extent() const {
    if (!flyout_panel_) return 0.0;
    const std::size_t index = flyout_slot_index();
    if (index >= stacks_.size()) return 0.0;
    const FlyoutGeometry placed = flyout_geometry();
    const double extent = across(stacks_[index].edge) ? placed.full.width : placed.full.height;
    return extent;
}

Control::Ptr DockSite::flyout_control() const noexcept {
    if (!flyout_panel_) return {};
    const Control::Ptr control = flyout_;
    return control;
}

// Moves the flyout to where its slide stands: the slot's rectangles (for
// painting and the parts) and the overlay's bounds in window coordinates.
void DockSite::place_flyout() {
    const std::size_t index = flyout_slot_index();
    if (!flyout_ || index >= stacks_.size()) return;
    const FlyoutGeometry placed = flyout_geometry();
    StackSlot& slot = stacks_[index];
    slot.bounds = placed.full;
    slot.caption = placed.caption;
    const Rect absolute = absolute_bounds();
    const Rect window_bounds = moved(placed.revealed, Point{absolute.x, absolute.y});
    if (!((*flyout_).requested_bounds() == window_bounds)) (*flyout_).set_requested_bounds(window_bounds);
    (*flyout_).invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    invalidate(Dirty::paint | Dirty::semantics);
}

bool DockSite::resize_flyout(double extent) {
    require_mutable();
    const std::size_t index = flyout_slot_index();
    if (index >= stacks_.size() || !std::isfinite(extent)) return false;
    const StackSlot& slot = stacks_[index];
    const double span = across(slot.edge) ? tree_area_.width : tree_area_.height;
    const double smallest = std::min(flyout_minimum_extent * effective_text_scale(), span);
    const double clamped = std::clamp(extent, smallest, std::max(smallest, span));
    bool kept = false;
    for (FlyoutExtent& entry : flyout_extents_) {
        if (!(entry.stack == slot.stack)) continue;
        entry.extent = clamped;
        kept = true;
    }
    if (!kept) flyout_extents_.push_back(FlyoutExtent{slot.stack, clamped});
    place_flyout();
    return true;
}

// -- opening and closing ------------------------------------------------------------------

void DockSite::flyout_adopt(const Control::Ptr& view) {
    if (flyout_) (*flyout_).adopt(view);
}

void DockSite::release_flyout_control() noexcept {
    if (flyout_) (*flyout_).release_site();
    flyout_root_.reset();
    flyout_.reset();
}

bool DockSite::focus_in_flyout() const {
    const Window* const host = attached_window();
    if (host == nullptr || !flyout_ || !flyout_panel_) return false;
    const Control::Ptr focused = (*host).focused_control();
    if (!focused) return false;
    const bool inside = (*focused).runtime_id() == (*flyout_).runtime_id() || (*flyout_).contains(*focused);
    return inside;
}

// Focus goes into the panel (its remembered control, else its first
// focusable one), else onto the flyout itself.
void DockSite::focus_into_flyout() {
    Window* const host = attached_window();
    if (host == nullptr || !flyout_panel_ || !flyout_) return;
    const DockPanelId panel = *flyout_panel_;
    focus_panel(panel);
    if (!focus_in_flyout()) static_cast<void>((*host).request_focus(flyout_));
}

// The control that had focus before the flyout took it (never a side strip),
// else the active document.
void DockSite::restore_focus_from_flyout() {
    Window* const host = attached_window();
    if (host == nullptr) return;
    const Control::Ptr target = return_focus_.lock();
    const bool inside = target && flyout_ &&
                        ((*target).runtime_id() == (*flyout_).runtime_id() || (*flyout_).contains(*target));
    if (target && !inside && focus_candidate(*target) && (*host).request_focus(target)) return;
    if (workspace_ == nullptr) return;
    const std::optional<DockPanelId> document = (*workspace_).active_document();
    if (document) static_cast<void>((*workspace_).activate_panel(*document, DockActivationCause::fallback));
}

bool DockSite::open_flyout(const DockPanelId& panel, DockFlyoutCause cause) {
    require_mutable();
    Window* const host = attached_window();
    if (host == nullptr || workspace_ == nullptr) return false;
    StackSlot* slot = side_slot_of(panel);
    if (slot == nullptr || !(*slot).view) return false;
    const bool take_focus = cause != DockFlyoutCause::hover;
    if (flyout_panel_ && *flyout_panel_ == panel) {
        if (take_focus && !focus_in_flyout()) {
            return_focus_ = focus_outside_;
            focus_into_flyout();
        }
        return true;
    }
    if (flyout_panel_) {
        close_flyout(DockFlyoutCause::swapped);
        slot = side_slot_of(panel);
        if (slot == nullptr || !(*slot).view) return false;
    }
    stop_hover();
    leave_timer_.disconnect();
    stale_timer_.disconnect();
    flyout_panel_ = panel;
    flyout_stack_ = (*slot).stack;
    pointer_in_flyout_ = false;
    flyout_focused_ = false;
    flyout_ = make_control<Flyout>(StableId(std::string(stable_id().value()) + ".flyout"), *this);
    const std::shared_ptr<const Theme> site_theme = inherited_theme_override();
    if (site_theme) (*flyout_).set_theme_override(site_theme);
    flyout_root_ = flyout_;
    move_view_to_flyout(*slot);
    // The slide starts under the strip; under reduced motion or in an
    // occluded window it is all out at once.
    flyout_slide_.jump_to(0.0);
    static_cast<void>(flyout_slide_.animate_to(1.0));
    place_flyout();
    flyout_token_ = (*host).open_overlay(shared_from_this(), flyout_);
    if (Event<>* const closed = flyout_token_.closed_event()) {
        flyout_revoked_ = (*closed).subscribe(*this, Delegate<>::bind<DockSite, &DockSite::on_flyout_revoked>(*this));
    }
    sync_side_strips();
    if (take_focus) {
        return_focus_ = focus_outside_;
        focus_into_flyout();
    }
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
    DockFlyoutChange change{};
    change.panel = panel;
    change.open = true;
    change.cause = cause;
    flyout_changed_.emit(change);
    return true;
}

// Closing order: focus returns (when it was inside), the view goes back to
// the site, the window drops the overlay, then flyout_changed(false).
void DockSite::close_flyout(DockFlyoutCause cause) noexcept {
    if (!flyout_panel_ || flyout_closing_) return;
    flyout_closing_ = true;
    const DockPanelId panel = *flyout_panel_;
    try {
        if (cause != DockFlyoutCause::swapped && focus_in_flyout()) restore_focus_from_flyout();
        if (menu_from_flyout_) close_position_menu();
    } catch (...) {
    }
    hover_timer_.disconnect();
    hover_panel_ = {};
    leave_timer_.disconnect();
    stale_timer_.disconnect();
    const std::size_t index = flyout_slot_index();
    if (hot_.index == index && (hot_.zone != Zone::seam)) hot_ = {};
    if (pressed_.index == index && (pressed_.zone != Zone::seam)) pressed_ = {};
    grip_captured_ = false;
    flyout_revoked_.disconnect();
    try {
        if (index < stacks_.size()) move_view_to_site(stacks_[index]);
    } catch (...) {
    }
    flyout_token_.disconnect();
    release_flyout_control();
    flyout_panel_.reset();
    flyout_stack_ = {};
    flyout_slide_.jump_to(1.0);
    flyout_focused_ = false;
    flyout_focus_pending_ = false;
    pointer_in_flyout_ = false;
    menu_from_flyout_ = false;
    flyout_closing_ = false;
    if (!is_alive()) return;
    try {
        invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
        DockFlyoutChange change{};
        change.panel = panel;
        change.open = false;
        change.cause = cause;
        flyout_changed_.emit(change);
    } catch (...) {
    }
}

// The window dropped the overlay (the site became unavailable).
void DockSite::on_flyout_revoked() {
    flyout_token_ = {};
    close_flyout(DockFlyoutCause::programmatic);
}

// A step of the slide on the window's motion clock: the flyout moves to
// where it now stands.
void DockSite::on_slide_motion(const MotionStep&) {
    if (!flyout_panel_) return;
    place_flyout();
}

void DockSite::schedule_stale_check() {
    if (stale_timer_.connected() || attached_window() == nullptr) return;
    const std::weak_ptr<Control> weak = shared_from_this();
    stale_timer_ = (*attached_window()).schedule_ui_timer(*this, one_shot_interval, FrameClock::now(),
                                                          StaleFlyout{weak});
}

void DockSite::stale_flyout_elapsed() {
    stale_timer_.disconnect();
    if (!flyout_panel_) return;
    const StackSlot* const slot = slot_for(flyout_stack_);
    const bool behind_tabs = slot != nullptr && ((*slot).auto_hidden || (*slot).side_tabbed);
    if (!behind_tabs) close_flyout(DockFlyoutCause::layout);
}

std::shared_ptr<const Theme> DockSite::inherited_theme_override() const {
    std::shared_ptr<const Theme> found = theme_override();
    for (Control::Ptr ancestor = parent(); !found && ancestor; ancestor = (*ancestor).parent()) {
        found = (*ancestor).theme_override();
    }
    return found;
}

// -- hover, leave and outside presses ------------------------------------------------------------

void DockSite::start_hover(const DockPanelId& panel) {
    if (workspace_ != nullptr && !(*workspace_).auto_hide_opens_on_hover()) return;
    if (hover_panel_ == panel && hover_timer_.connected()) return;
    stop_hover();
    Window* const host = attached_window();
    if (host == nullptr) return;
    hover_panel_ = panel;
    const std::weak_ptr<Control> weak = shared_from_this();
    hover_timer_ = (*host).schedule_ui_timer(*this, one_shot_interval, FrameClock::now() + flyout_hover_delay,
                                             HoverElapsed{weak, panel});
}

void DockSite::stop_hover() noexcept {
    hover_timer_.disconnect();
    hover_panel_ = {};
}

void DockSite::start_leave() {
    if (leave_timer_.connected()) return;
    Window* const host = attached_window();
    if (host == nullptr) return;
    const std::weak_ptr<Control> weak = shared_from_this();
    leave_timer_ = (*host).schedule_ui_timer(*this, one_shot_interval, FrameClock::now() + flyout_leave_delay,
                                             LeaveElapsed{weak});
}

void DockSite::hover_elapsed(const DockPanelId& panel) {
    hover_timer_.disconnect();
    if (!(hover_panel_ == panel)) return;
    hover_panel_ = {};
    // A flyout the user is working in is not replaced by a hover.
    if (flyout_panel_ && focus_in_flyout()) return;
    static_cast<void>(open_flyout(panel, DockFlyoutCause::hover));
}

void DockSite::leave_elapsed() {
    leave_timer_.disconnect();
    if (!flyout_panel_ || pointer_in_flyout_ || focus_in_flyout()) return;
    close_flyout(DockFlyoutCause::pointer_left);
}

// Whether a window point is on a side strip, and the panel of the side tab
// under it, if any.
bool DockSite::side_tab_at(Point point, std::optional<DockPanelId>& tab) const {
    bool on_strip = false;
    for (const SideStrip& entry : sides_) {
        if (!entry.strip || entry.panels.empty() || !(*entry.strip).absolute_bounds().contains(point)) continue;
        on_strip = true;
        const std::optional<PageId> page = (*entry.strip).page_at(point);
        if (page) tab = DockPanelId((*page).value);
    }
    return on_strip;
}

void DockSite::close_on_outside_press(const PointerEvent& event) {
    if (!flyout_panel_) return;
    std::optional<DockPanelId> tab{};
    if (side_tab_at(event.position, tab)) return;
    close_flyout(DockFlyoutCause::outside_press);
}

// Every pointer event outside the window's overlays. Moves start the hover
// delay over a side tab and the leave delay elsewhere; a press outside the
// flyout and the side strips closes the flyout and goes on (owner decision
// DK-27, E2-1), so the event is observed, never handled.
void DockSite::on_root_pointer(const PointerEvent& event) {
    if (workspace_ == nullptr) return;
    std::optional<DockPanelId> tab{};
    const bool on_strip = side_tab_at(event.position, tab);
    if (event.action == PointerAction::move || event.action == PointerAction::enter) {
        pointer_in_flyout_ = false;
        if (tab && flyout_panel_ && *flyout_panel_ == *tab) {
            // Resting on the open flyout's own tab keeps it out.
            stop_hover();
            leave_timer_.disconnect();
            return;
        }
        if (tab && (*workspace_).flyout_opening.read() == DockFlyoutOpening::hover_or_click) {
            start_hover(*tab);
        } else {
            stop_hover();
        }
        if (flyout_panel_ && !focus_in_flyout()) start_leave();
        return;
    }
    if (event.action == PointerAction::up) flyout_focus_pending_ = false;
    if (event.action != PointerAction::down) return;
    stop_hover();
    if (!flyout_panel_ || on_strip) return;
    close_on_outside_press(event);
}

void DockSite::on_side_tab_invoked(DockSide, const PageInvocation& invocation) {
    const DockPanelId panel(invocation.page.value);
    const bool pointer = invocation.cause == PageSelectionCause::pointer;
    const DockFlyoutCause cause = pointer ? DockFlyoutCause::pointer : DockFlyoutCause::keyboard;
    const bool opened = open_flyout(panel, cause);
    // The strip takes focus after raising page_invoked for a press; that
    // focus is sent on into the flyout (on_focus_moved) until the release.
    flyout_focus_pending_ = opened && pointer;
}

// -- the flyout's own input --------------------------------------------------------------------

void DockSite::on_flyout_pointer_preview(const PointerEvent& event) {
    if (event.action == PointerAction::leave) return;
    pointer_in_flyout_ = true;
    leave_timer_.disconnect();
    stop_hover();
}

DockSite::Hit DockSite::flyout_hit(Point local) const {
    Hit hit{};
    const std::size_t index = flyout_slot_index();
    if (index >= stacks_.size()) return hit;
    const FlyoutGeometry placed = flyout_geometry();
    if (!placed.revealed.contains(local)) return hit;
    hit.index = index;
    if (placed.grip.contains(local)) {
        hit.zone = Zone::grip;
        return hit;
    }
    if (!placed.caption.contains(local)) return hit;
    const CaptionGeometry caption = caption_geometry(placed.caption, pin_shown(stacks_[index]), close_shown(stacks_[index]));
    hit.zone = Zone::caption;
    if (caption.menu.contains(local)) hit.zone = Zone::menu;
    if (caption.pin.contains(local)) hit.zone = Zone::pin;
    if (caption.close.contains(local)) hit.zone = Zone::close;
    return hit;
}

// Presses and moves on the flyout's own surface (its caption, grip and face;
// the panel's controls take their own). A press stays in the flyout.
void DockSite::on_flyout_pointer(PointerEvent& event) {
    const std::size_t index = flyout_slot_index();
    if (index >= stacks_.size() || !flyout_) return;
    const Rect absolute = absolute_bounds();
    const Point local{event.position.x - absolute.x, event.position.y - absolute.y};
    const DockSide edge = stacks_[index].edge;
    if (event.action == PointerAction::leave) {
        if (!grip_captured_ && hot_.index == index && hot_.zone != Zone::seam) {
            hot_ = {};
            (*flyout_).invalidate(Dirty::paint);
        }
        return;
    }
    // A press on the flyout's caption may drag its stack out (phase D3).
    if (drag_.phase == DockDragPhase::armed && event.action == PointerAction::move) {
        update_drag_pointer(local, event.modifiers);
        event.handled = true;
        return;
    }
    if (drag_.phase == DockDragPhase::armed && event.action == PointerAction::up &&
        event.button == PointerButton::primary) {
        reset_drag();
    }
    if (event.action == PointerAction::move || event.action == PointerAction::enter) {
        if (grip_captured_) {
            const double dx = event.position.x - grip_origin_.x;
            const double dy = event.position.y - grip_origin_.y;
            double delta = dx;
            if (edge == DockSide::right) delta = -dx;
            if (edge == DockSide::top) delta = dy;
            if (edge == DockSide::bottom) delta = -dy;
            static_cast<void>(resize_flyout(grip_start_extent_ + delta));
            event.handled = true;
            return;
        }
        const Hit hit = flyout_hit(local);
        if (!(hot_ == hit)) {
            hot_ = hit;
            CursorKind cursor = CursorKind::arrow;
            if (hit.zone == Zone::grip) cursor = across(edge) ? CursorKind::resize_horizontal : CursorKind::resize_vertical;
            (*flyout_).cursor = cursor;
            (*flyout_).invalidate(Dirty::paint);
            invalidate(Dirty::paint);
        }
        return;
    }
    if (event.action == PointerAction::down) event.handled = true;
    if (event.button != PointerButton::primary) return;
    const Control::Ptr keep = shared_from_this();
    if (event.action == PointerAction::down) {
        const Hit hit = flyout_hit(local);
        if (hit.zone == Zone::grip) {
            grip_captured_ = true;
            grip_start_extent_ = flyout_extent();
            grip_origin_ = event.position;
            invalidate(Dirty::paint);
            (*flyout_).invalidate(Dirty::paint);
            return;
        }
        if (hit.zone == Zone::menu && flyout_panel_) {
            if (menu_open_ && menu_from_flyout_) {
                close_position_menu();
                return;
            }
            const DockPanelId panel = *flyout_panel_;
            static_cast<void>(show_position_menu(panel, MenuOpenMode::pointer));
            return;
        }
        if ((hit.zone == Zone::pin || hit.zone == Zone::close) &&
            !button_states(hit.index, hit.zone).contains(AnatomyState::disabled)) {
            pressed_ = hit;
            (*flyout_).invalidate(Dirty::paint);
            return;
        }
        // A press on the caption or the face takes focus into the panel; on
        // the caption it may become a drag of the stack.
        if (!focus_in_flyout()) {
            return_focus_ = focus_outside_;
            focus_into_flyout();
        }
        if (hit.zone == Zone::caption && is_alive()) arm_caption_drag(hit.index, local, event.pointer_id);
        return;
    }
    if (event.action != PointerAction::up) return;
    event.handled = true;
    if (grip_captured_) {
        grip_captured_ = false;
        invalidate(Dirty::paint);
        if (flyout_) (*flyout_).invalidate(Dirty::paint);
        return;
    }
    const Hit pressed = pressed_;
    pressed_ = {};
    if (pressed.zone == Zone::none) return;
    const Hit released = flyout_hit(local);
    if (flyout_) (*flyout_).invalidate(Dirty::paint);
    if (pressed != released) return;
    press_caption_button(pressed.index, pressed.zone);
}

// Escape closes the flyout and returns focus; the docking keys (Shift+Escape,
// the navigators, the position menu) work in it as in a docked stack.
void DockSite::on_flyout_key(KeyEvent& event) {
    if (event.handled || event.action != KeyAction::down) return;
    if (event.physical_key == PhysicalKey::escape && event.modifiers == Modifier::none) {
        close_flyout(DockFlyoutCause::escape);
        event.handled = true;
        return;
    }
    static_cast<void>(handle_docking_key(event));
}

// -- paint ------------------------------------------------------------------------------------

// The flyout's surface into its own root, from the site's rectangles moved
// into the flyout's coordinates: the overlay shadow and the face, the
// caption, and the grip; the panel paints itself above.
void DockSite::paint_flyout(Painter& painter, const Control& surface) const {
    const std::size_t index = flyout_slot_index();
    if (index >= stacks_.size()) return;
    const FlyoutGeometry placed = flyout_geometry();
    const Point offset{-placed.revealed.x, -placed.revealed.y};
    PartPaint part{};
    part.family = ControlVisualRole::panel;
    part.role = PartRole::shadow;
    part.bounds = moved(placed.full, offset);
    part.corner_radius = 0.0;
    paint_part(painter, surface, part);
    part.corner_radius.reset();
    part.role = PartRole::face;
    paint_part(painter, surface, part);
    paint_caption(painter, surface, stacks_[index], index, offset);
    PartPaint grip{};
    grip.family = ControlVisualRole::panel;
    grip.role = PartRole::grip;
    grip.mark = PartMark::grip_dots;
    grip.bounds = moved(placed.grip, offset);
    if (hot_.zone == Zone::grip) grip.states.insert(AnatomyState::hot);
    if (grip_captured_) grip.states.insert(AnatomyState::captured);
    paint_part(painter, surface, grip);
}

} // namespace gui_forms
