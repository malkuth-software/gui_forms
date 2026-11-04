#include "gallery_parts.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <variant>

namespace gui_forms::gallery {
namespace {

constexpr Color accent = Color::rgba(38, 114, 185);
constexpr Color shadow = Color::rgba(148, 162, 175);
constexpr Color card_face = Color::rgba(247, 249, 251, 238);
constexpr Color drop_wash = Color::rgba(216, 235, 249, 96);

bool same_rect(Rect left, Rect right) noexcept {
    const bool same = left.x == right.x && left.y == right.y && left.width == right.width &&
                      left.height == right.height;
    return same;
}

void request_bounds(const Control::Ptr& child, Rect bounds) {
    if (!same_rect((*child).requested_bounds(), bounds)) (*child).set_requested_bounds(bounds);
}

// Counts a drop's payload items by kind.
class DropDataCounter final {
public:
    DropDataCounter(std::size_t& files, std::size_t& text_items, std::size_t& binary_items) noexcept
        : files_(&files), text_items_(&text_items), binary_items_(&binary_items) {}

    void operator()(const DragFileListData& data) const noexcept { *files_ += data.paths_utf8.size(); }
    void operator()(const DragTextData&) const noexcept { ++*text_items_; }
    void operator()(const DragBinaryData&) const noexcept { ++*binary_items_; }

private:
    std::size_t* files_{};
    std::size_t* text_items_{};
    std::size_t* binary_items_{};
};

} // namespace

// ---- LifecycleCard ------------------------------------------------------------

LifecycleCard::LifecycleCard(StableId stable_id) : UserControl(std::move(stable_id)) {
    initialized_ = initialization_completed().subscribe(
        Delegate<Dirty, bool>::bind<LifecycleCard, &LifecycleCard::on_initialization_completed>(*this));
    load_ = loaded().subscribe(Delegate<>::bind<LifecycleCard, &LifecycleCard::on_loaded>(*this));
}

void LifecycleCard::arrange(Rect final_bounds) {
    for (const Control::Ptr& child : children()) {
        if ((*child).stable_id().value() == "gallery.lifecycle-title") {
            request_bounds(child, {9.0, 2.0, 82.0, 20.0});
        } else if ((*child).stable_id().value() == "gallery.lifecycle-status") {
            request_bounds(child, {95.0, 2.0, std::max(0.0, final_bounds.width - 101.0), 20.0});
        }
    }
    UserControl::arrange(final_bounds);
}

void LifecycleCard::on_paint(Painter& painter, Rect) {
    const Rect bounds{0.0, 0.0, committed_arranged_bounds().width, committed_arranged_bounds().height};
    painter.fill_rect(bounds, card_face);
    painter.fill_rect({0.0, 0.0, 4.0, bounds.height}, accent);
    painter.stroke_rect({0.5, 0.5, std::max(0.0, bounds.width - 1.0), std::max(0.0, bounds.height - 1.0)}, shadow,
                        1.0);
}

void LifecycleCard::on_attached_to_window() {
    UserControl::on_attached_to_window();
    for (const Control::Ptr& child : children()) {
        if ((*child).stable_id().value() != "gallery.lifecycle-status") continue;
        const std::shared_ptr<Label> status = std::dynamic_pointer_cast<Label>(child);
        if (!status) continue;
        char value[96]{};
        std::snprintf(value, sizeof(value), "Load 1 · attach %llu · init %llu",
                      static_cast<unsigned long long>(attachment_count() + 1),
                      static_cast<unsigned long long>(initialization_batches_));
        (*status).set_text(value);
    }
}

void LifecycleCard::on_initialization_completed(Dirty, bool) {
    ++initialization_batches_;
}

void LifecycleCard::on_loaded() {
    // Two nested initialization scopes complete as one batch.
    begin_init();
    invalidate(Dirty::style | Dirty::paint);
    begin_init();
    invalidate(Dirty::semantics | Dirty::accessibility);
    end_init();
    end_init();
}

// ---- CollectionList -----------------------------------------------------------

CollectionList::CollectionList(StableId stable_id) : ListBox(std::move(stable_id)) {
    set_allow_drop(true);
}

void CollectionList::on_paint(Painter& painter, Rect local_damage) {
    ListBox::on_paint(painter, local_damage);
    if (!drag_hovered_) return;
    const Rect bounds{0.0, 0.0, committed_arranged_bounds().width, committed_arranged_bounds().height};
    painter.fill_rect({2.0, 2.0, std::max(0.0, bounds.width - 4.0), std::max(0.0, bounds.height - 4.0)}, drop_wash);
    painter.stroke_rect({1.5, 1.5, std::max(0.0, bounds.width - 3.0), std::max(0.0, bounds.height - 3.0)}, accent,
                        2.0);
}

void CollectionList::on_drag(DragEvent& event) {
    if (event.action == DragAction::leave) {
        drag_hovered_ = false;
        invalidate(Dirty::paint);
        event.handled = true;
        return;
    }
    drag_hovered_ = event.action != DragAction::drop;
    if (has_drag_effect(event.allowed_effects, DragEffect::copy)) event.accepted_effect = DragEffect::copy;
    invalidate(Dirty::paint);
    event.handled = true;
    if (event.action != DragAction::drop) return;
    std::size_t files = 0U;
    std::size_t text_items = 0U;
    std::size_t binary_items = 0U;
    for (const DragDataItem& item : event.items) std::visit(DropDataCounter(files, text_items, binary_items), item);
    char status[128]{};
    std::snprintf(status, sizeof(status), "Drop received · %zu file%s · %zu text · %zu data", files,
                  files == 1U ? "" : "s", text_items, binary_items);
    const std::string summary(status);
    dropped_.emit(summary);
}

} // namespace gui_forms::gallery
