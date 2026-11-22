#include "gui_forms/controls/dock_workspace.hpp"

#include "gui_forms/controls/dock_site.hpp"
#include "gui_forms/controls/menu_strip.hpp"
#include "gui_forms/controls/tool_strip_manager.hpp"
#include "gui_forms/window.hpp"

#include <stxlib/optional.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

// The workspace's side of the frames presentation and of the multiple
// document interface (DK decisions 60 to 63): the frame calls, the document
// area it presents to its main window (AreaLink), the panels' menu strips
// merging into the main menu strip, and a maximized frame carried to the
// newly active document.

namespace gui_forms {
namespace {

// The stacks under a node in tree order (a split's first child first).
// Recurses once per dock node level: a committed tree is at most
// dock_maximum_depth (32) deep, refused beyond by the invariant check.
void collect_area_stacks(const DockLayout& layout, DockNodeId node_id, std::vector<DockNodeId>& out) {
    const DockNode* const node = layout.find_node(node_id);
    if (node == nullptr) return;
    if ((*node).kind == DockNodeKind::split || (*node).kind == DockNodeKind::document_area) {
        if (!((*node).first == DockNodeId{})) collect_area_stacks(layout, (*node).first, out);
        if (!((*node).second == DockNodeId{})) collect_area_stacks(layout, (*node).second, out);
        return;
    }
    out.push_back(node_id);
}

// The documents of the document area (the main tree holds the only one), in
// tree and tab order.
void collect_area_panels(const DockLayout& layout, detail::LendingVector<DockPanelId>& out) {
    out.clear();
    DockNodeId area{};
    for (const DockNode& node : layout.state().nodes) {
        if (node.kind == DockNodeKind::document_area) area = node.id;
    }
    if (area == DockNodeId{}) return;
    std::vector<DockNodeId> stacks{};
    collect_area_stacks(layout, area, stacks);
    for (const DockNodeId stack_id : stacks) {
        const DockNode* const stack = layout.find_node(stack_id);
        if (stack == nullptr) continue;
        for (const DockPanelId& panel : (*stack).panels) out.push_back(panel);
    }
}

const DockFrameMemory* frame_of(const DockLayout& layout, const DockPanelId& panel) noexcept {
    for (const DockPanelRecord& record : layout.state().panels) {
        if (record.id == panel) return record.frame.value();
    }
    return nullptr;
}
// The result points into `layout`: a temporary layout may not be passed.
const DockFrameMemory* frame_of(DockLayout&& layout, const DockPanelId& panel) = delete;

bool frame_in_state(const DockLayout& layout, const DockPanelId& panel, DockFrameState state) noexcept {
    const DockFrameMemory* const frame = frame_of(layout, panel);
    const bool in_state = frame != nullptr && (*frame).state == state;
    return in_state;
}

stxlib::Optional<DockPanelId> some_panel(const DockPanelId& panel) {
    stxlib::Optional<DockPanelId> held{};
    held.set(DockPanelId(panel));
    return held;
}

DockFrameArrangement arrangement_of(MdiLayout layout) noexcept {
    switch (layout) {
    case MdiLayout::cascade:
        return DockFrameArrangement::cascade;
    case MdiLayout::tile_horizontal:
        return DockFrameArrangement::tile_horizontal;
    case MdiLayout::tile_vertical:
        return DockFrameArrangement::tile_vertical;
    case MdiLayout::arrange_icons:
        return DockFrameArrangement::arrange_icons;
    }
    return DockFrameArrangement::cascade;
}

std::shared_ptr<MenuStrip> menu_strip_of(const Control::Ptr& control) {
    std::shared_ptr<MenuStrip> strip = std::dynamic_pointer_cast<MenuStrip>(control);
    return strip;
}

} // namespace

// -- the frame calls -------------------------------------------------------------

DockTransactionResult DockWorkspace::set_document_presentation(DockDocumentPresentation presentation,
                                                               DockEditOrigin origin) {
    DockTransaction transaction{};
    transaction.origin = origin;
    if (layout_.state().document_presentation != presentation) {
        transaction.edits.push_back(DockEdit::set_document_presentation(presentation));
    }
    const DockTransactionResult result = layout_.apply(transaction);
    return result;
}

// Maximizing one frame restores a maximized other in the same transaction,
// and activates the maximized document, as WinForms' MDI does: a maximized
// frame is the active one.
DockTransactionResult DockWorkspace::set_frame_state(const DockPanelId& document, DockFrameState state,
                                                     DockEditOrigin origin) {
    DockTransaction transaction{};
    transaction.origin = origin;
    if (state == DockFrameState::maximized) {
        detail::LendingVector<DockPanelId> documents{};
        collect_area_panels(layout_, documents);
        for (const DockPanelId& other : documents) {
            if (other == document || !frame_in_state(layout_, other, DockFrameState::maximized)) continue;
            transaction.edits.push_back(DockEdit::set_frame_state(other, DockFrameState::normal));
        }
    }
    transaction.edits.push_back(DockEdit::set_frame_state(document, state));
    if (state == DockFrameState::maximized) transaction.edits.push_back(DockEdit::select(document));
    const DockTransactionResult result = layout_.apply(transaction);
    if (!result.applied || state != DockFrameState::maximized) return result;
    const stxlib::Optional<DockPanelId> active = area_active_document();
    const bool already = active.present() && *active.value() == document;
    if (!already) {
        const DockActivationCause cause =
            origin == DockEditOrigin::user ? DockActivationCause::pointer : DockActivationCause::programmatic;
        static_cast<void>(activate_panel(document, cause));
    }
    return result;
}

DockTransactionResult DockWorkspace::set_frame_bounds(const DockPanelId& document, Rect bounds,
                                                      DockEditOrigin origin) {
    DockTransaction transaction{};
    transaction.origin = origin;
    transaction.edits.push_back(DockEdit::set_frame_bounds(document, bounds));
    const DockTransactionResult result = layout_.apply(transaction);
    return result;
}

DockTransactionResult DockWorkspace::arrange_frames(DockFrameArrangement arrangement, DockEditOrigin origin) {
    DockTransaction transaction{};
    if (site_ != nullptr) {
        const Rect area = (*site_).document_area_;
        transaction = plan_frame_arrangement(layout_, arrangement, Size{area.width, area.height}, frame_metrics());
    }
    transaction.origin = origin;
    const DockTransactionResult result = layout_.apply(transaction);
    return result;
}

stxlib::Optional<DockFrameMemory> DockWorkspace::frame_memory(const DockPanelId& document) const {
    const DockFrameMemory* const frame = frame_of(layout_, document);
    if (frame == nullptr) return stxlib::Optional<DockFrameMemory>::none();
    return stxlib::Optional<DockFrameMemory>::of(*frame);
}

void DockWorkspace::set_frame_icons_at_bottom(bool bottom) {
    if (frame_icons_at_bottom_ == bottom) return;
    frame_icons_at_bottom_ = bottom;
    if (site_ != nullptr) (*site_).invalidate(Dirty::arrange | Dirty::paint);
}

DockFrameMetrics DockWorkspace::frame_metrics() const {
    if (site_ != nullptr) return (*site_).frame_metrics();
    DockFrameMetrics metrics{};
    metrics.icons_at_bottom = frame_icons_at_bottom_;
    return metrics;
}

BorrowedSpan<DockPanelId> DockWorkspace::area_documents() const& {
    area_link_.refresh();
    return detail::lend_view(area_link_.documents_);
}

bool DockWorkspace::in_document_area(const DockPanelId& panel) const {
    area_link_.refresh();
    const detail::LendingVector<DockPanelId>& documents = area_link_.documents_;
    const bool inside = std::find(documents.begin(), documents.end(), panel) != documents.end();
    return inside;
}

stxlib::Optional<DockPanelId> DockWorkspace::area_active_document() const {
    for (const DockPanelId& recent : document_history_) {
        if (in_document_area(recent)) return some_panel(recent);
    }
    area_link_.refresh();
    const detail::LendingVector<DockPanelId>& documents = area_link_.documents_;
    if (documents.empty()) return stxlib::Optional<DockPanelId>::none();
    if (layout_.state().document_presentation == DockDocumentPresentation::frames) {
        const DockPanelId* front = nullptr;
        std::uint32_t front_order = 0U;
        for (const DockPanelId& document : documents) {
            const DockFrameMemory* const frame = frame_of(layout_, document);
            const std::uint32_t order = frame != nullptr ? (*frame).order : 0U;
            if (front == nullptr || order > front_order) {
                front = &document;
                front_order = order;
            }
        }
        return some_panel(*front);
    }
    const DockPanelLocation location = layout_.locate(documents[0]);
    const DockNode* const stack = layout_.find_node(location.stack);
    if (stack != nullptr && (*stack).selected < (*stack).panels.size()) {
        return some_panel((*stack).panels[(*stack).selected]);
    }
    return some_panel(documents[0]);
}

// -- following the area's active document ----------------------------------------

void DockWorkspace::follow_area_activation() {
    stxlib::Optional<DockPanelId> current = area_active_document();
    if (!(current == followed_active_)) {
        stxlib::Optional<DockPanelId> previous = std::move(followed_active_);
        if (current.present()) {
            followed_active_.set(DockPanelId(*current.value()));
        } else {
            followed_active_.reset();
        }
        if (previous.present() && current.present()) carry_maximized(*previous.value(), *current.value());
    }
    update_menu_merge();
    area_link_.follow_active();
}

// In the frames presentation, a maximized frame stays maximized across
// activation: the document left behind is restored and the newly active one
// maximized, in one program transaction (WinForms' MDI).
void DockWorkspace::carry_maximized(const DockPanelId& previous, const DockPanelId& current) {
    if (layout_.state().document_presentation != DockDocumentPresentation::frames) return;
    if (!frame_in_state(layout_, previous, DockFrameState::maximized)) return;
    if (frame_in_state(layout_, current, DockFrameState::maximized)) return;
    if (!in_document_area(previous) || !in_document_area(current)) return;
    DockTransaction transaction{};
    transaction.origin = DockEditOrigin::program;
    transaction.edits.push_back(DockEdit::set_frame_state(previous, DockFrameState::normal));
    transaction.edits.push_back(DockEdit::set_frame_state(current, DockFrameState::maximized));
    static_cast<void>(layout_.apply(transaction));
}

// The active area document's menu strip merges into the main window's main
// menu strip; another document's merge is reverted first. A merge the
// strips refuse (merging off, a repeated item ID, a strip not owned by a
// shared pointer) leaves the main strip as it was.
void DockWorkspace::update_menu_merge() {
    Window* const main = main_model();
    const Control::Ptr target_control = main != nullptr ? (*main).main_menu_strip() : Control::Ptr{};
    const stxlib::Optional<DockPanelId> active = area_active_document();
    Control::Ptr source_control{};
    if (active.present()) {
        const PanelEntry* const entry = find_entry(*active.value());
        if (entry != nullptr) source_control = (*entry).menu_strip;
    }
    const std::shared_ptr<MenuStrip> target = menu_strip_of(target_control);
    const std::shared_ptr<MenuStrip> source = menu_strip_of(source_control);
    const bool unchanged = merged_document_.present() && source && target && merged_source_.lock() == source_control &&
                           merged_target_.lock() == target_control;
    if (unchanged) return;
    revert_menu_merge();
    if (!source || !target || source == target) return;
    bool merged = false;
    try {
        merged = merge_strips(*source, *target);
    } catch (const std::invalid_argument&) {
        merged = false;
    } catch (const std::bad_weak_ptr&) {
        merged = false;
    }
    if (!merged) return;
    merged_source_ = source_control;
    merged_target_ = target_control;
    merged_document_.set(DockPanelId(*active.value()));
}

void DockWorkspace::revert_menu_merge() noexcept {
    const std::shared_ptr<MenuStrip> target = menu_strip_of(merged_target_.lock());
    const std::shared_ptr<MenuStrip> source = menu_strip_of(merged_source_.lock());
    merged_source_.reset();
    merged_target_.reset();
    merged_document_.reset();
    if (!target || !source) return;
    try {
        static_cast<void>(revert_merge(*target, *source));
    } catch (...) {
        // The merge stays in the main strip's history; the next revert of
        // that strip takes it.
    }
}

bool DockWorkspace::set_panel_menu_strip(const DockPanelId& panel, MenuStrip& strip) {
    PanelEntry* const entry = find_entry(panel);
    if (entry == nullptr) return false;
    const Control::Ptr held = strip.shared_from_this();
    if ((*entry).menu_strip == held) return true;
    if (merged_document_.present() && *merged_document_.value() == panel) revert_menu_merge();
    (*entry).menu_strip = held;
    update_menu_merge();
    return true;
}

bool DockWorkspace::clear_panel_menu_strip(const DockPanelId& panel) {
    PanelEntry* const entry = find_entry(panel);
    if (entry == nullptr) return false;
    if (merged_document_.present() && *merged_document_.value() == panel) revert_menu_merge();
    (*entry).menu_strip.reset();
    update_menu_merge();
    return true;
}

Control::Ptr DockWorkspace::panel_menu_strip(const DockPanelId& panel) const {
    const PanelEntry* const entry = find_entry(panel);
    if (entry == nullptr) return Control::Ptr{};
    return (*entry).menu_strip;
}

stxlib::Optional<DockPanelId> DockWorkspace::merged_document() const {
    if (!merged_document_.present()) return stxlib::Optional<DockPanelId>::none();
    return some_panel(*merged_document_.value());
}

// -- AreaLink ----------------------------------------------------------------------

void DockWorkspace::AreaLink::refresh() const {
    const DockLayout& layout = (*workspace_).layout_;
    if (documents_read_ && documents_generation_ == layout.generation()) return;
    collect_area_panels(layout, documents_);
    documents_generation_ = layout.generation();
    documents_read_ = true;
}

bool DockWorkspace::AreaLink::document_frames_shown() const {
    const bool frames = (*workspace_).document_presentation() == DockDocumentPresentation::frames;
    return frames;
}

void DockWorkspace::AreaLink::show_document_frames(bool frames) {
    const DockDocumentPresentation presentation =
        frames ? DockDocumentPresentation::frames : DockDocumentPresentation::tabs;
    static_cast<void>((*workspace_).set_document_presentation(presentation, DockEditOrigin::program));
}

std::size_t DockWorkspace::AreaLink::document_count() const {
    refresh();
    return documents_.size();
}

DockPanelId DockWorkspace::AreaLink::document_at(std::size_t index) const {
    refresh();
    if (index >= documents_.size()) return DockPanelId{};
    return documents_[index];
}

// A registered panel's title, else (no title, or a panel the layout holds
// without a registration) its identity, as the site's tab shows it.
BorrowedText DockWorkspace::AreaLink::document_title(const DockPanelId& document) const {
    refresh();
    const detail::LendingVector<DockPanelId>::const_iterator found =
        std::find(documents_.begin(), documents_.end(), document);
    if (found == documents_.end()) return BorrowedText{};
    const PanelEntry* const entry = (*workspace_).find_entry(document);
    if (entry != nullptr && !(*entry).registration.title.empty()) {
        return detail::lend_text((*entry).registration.title, titles_);
    }
    return detail::lend_text((*found).value, titles_);
}

stxlib::Optional<DockPanelId> DockWorkspace::AreaLink::active_document() const {
    return (*workspace_).area_active_document();
}

bool DockWorkspace::AreaLink::activate_document(const DockPanelId& document) {
    if (!(*workspace_).in_document_area(document)) return false;
    const bool activated = (*workspace_).activate_panel(document, DockActivationCause::programmatic);
    return activated;
}

bool DockWorkspace::AreaLink::arrange_documents(MdiLayout layout) {
    if (!document_frames_shown() || document_count() == 0U) return false;
    const DockTransactionResult result =
        (*workspace_).arrange_frames(arrangement_of(layout), DockEditOrigin::program);
    return result.applied;
}

void DockWorkspace::AreaLink::set_icons_at_bottom(bool bottom) {
    (*workspace_).set_frame_icons_at_bottom(bottom);
}

void DockWorkspace::AreaLink::follow_active() {
    stxlib::Optional<DockPanelId> current = (*workspace_).area_active_document();
    if (current == reported_) return;
    MdiChildActivation activation{};
    activation.previous = std::move(reported_);
    if (current.present()) activation.current.set(DockPanelId(*current.value()));
    reported_ = std::move(current);
    raise_document_activated(activation);
}

void DockWorkspace::AreaLink::titles_changed() noexcept {
    titles_.invalidate();
}

} // namespace gui_forms
