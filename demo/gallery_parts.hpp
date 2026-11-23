#pragma once

// Application types of the Gallery's form document (demo/gallery.form,
// FD6.2): Gallery subclasses of registered controls, named to the form
// compiler with --type. Each adds Gallery behaviour only; the registered
// base supplies the control.

#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/list_box.hpp"
#include "gui_forms/controls/user_control.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/event/event/event.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/painter/painter.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gui_forms::gallery {

// The lifecycle specimen on the basics page (`gallery-lifecycle-card`, a
// user-control): shows its first load, attachment and initialization
// batches in its `gallery.lifecycle-status` label, and lays out its title
// and status labels itself.
class LifecycleCard final : public UserControl {
public:
    explicit LifecycleCard(StableId stable_id);

    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;

protected:
    void on_attached_to_window() override;

private:
    void on_initialization_completed(Dirty dirty, bool layout_requested);
    void on_loaded();

    SubscriptionToken initialized_;
    SubscriptionToken load_;
    std::uint64_t initialization_batches_{};
};

// The retained-rows collection (`gallery-collection`, a list-box): a typed
// copy drop target. A drop adds a row for each item it carries, after the
// authored rows, so the drop shows where it lands: "File: <name>",
// "Text: <first line>" or "Data: <media type> · <n> bytes", with stable IDs
// gallery.collection.drop.<n>; past max_dropped_rows the oldest dropped rows
// go. It then raises dropped() with a summary of the payload ("Drop received
// · 1 file · 0 text · 0 data"). Hovering shows a drop cue.
class CollectionList final : public ListBox {
public:
    explicit CollectionList(StableId stable_id);

    static constexpr std::size_t max_dropped_rows = 8U;

    [[nodiscard]] Event<const std::string&>& dropped() noexcept { return dropped_; }
    // The rows drops added, oldest first.
    [[nodiscard]] std::size_t dropped_row_count() const noexcept { return dropped_ids_.size(); }
    // Removes the rows drops added (the Gallery's Reset).
    void clear_dropped_rows();

    void on_paint(Painter& painter, Rect local_damage) override;
    void on_drag(DragEvent& event) override;

private:
    void add_dropped_rows(std::vector<std::string> labels);
    void show_last_row();

    bool drag_hovered_{};
    Event<const std::string&> dropped_;
    std::vector<std::string> dropped_ids_;
    std::uint64_t drop_serial_{};
};

} // namespace gui_forms::gallery
