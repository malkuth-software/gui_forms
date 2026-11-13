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

#include <cstdint>
#include <string>

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
// copy drop target. A drop raises dropped() with a summary of the payload
// ("Drop received · 1 file · 0 text · 0 data"); hovering shows a drop cue.
class CollectionList final : public ListBox {
public:
    explicit CollectionList(StableId stable_id);

    [[nodiscard]] Event<const std::string&>& dropped() noexcept { return dropped_; }

    void on_paint(Painter& painter, Rect local_damage) override;
    void on_drag(DragEvent& event) override;

private:
    bool drag_hovered_{};
    Event<const std::string&> dropped_;
};

} // namespace gui_forms::gallery
