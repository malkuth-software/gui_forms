#include "gui_forms/controls/scrollable_control/container_control/user_control/user_control.hpp"
#include "../../scrollable_anatomy.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace gui_forms {

UserControl::UserControl(StableId stable_id)
    : ContainerControl(std::move(stable_id)) {
    // An application's own paint may read the size: the Control default
    // (ScrollableControl clears resize_redraw for its bar-only paint).
    Control::set_style(ControlStyles::resize_redraw, true);
}

void UserControl::on_attached_to_window() {
    attached_ = true;
    if (!loaded_) {
        loaded_ = true;
        publish_change(loaded_event_);
    }
}

void UserControl::on_attachment_committed() noexcept {
    ++attachment_count_;
}

void UserControl::on_detached_from_window() noexcept {
    attached_ = false;
}


namespace {

constexpr ControlAnatomy user_control_anatomy{
    .control_id = "user_control",
    .parts = scrollable_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& UserControl::anatomy() const noexcept {
    return user_control_anatomy;
}

namespace {

// The events UserControl adds to those of ContainerControl, in declaration order.
constexpr std::array<InteractionDeclaration, 1> user_control_interactions{{
    {.name = "loaded", .payload = "", .mutable_payload = false},
}};

} // namespace

void UserControl::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    ContainerControl::append_interactions(interactions);
    interactions.insert(interactions.end(), user_control_interactions.begin(), user_control_interactions.end());
}

} // namespace gui_forms
