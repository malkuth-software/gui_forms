#include "gui_forms/controls/h_scroll_bar.hpp"

#include "../scroll_bar_anatomy.hpp"

#include <utility>

namespace gui_forms {

HScrollBar::HScrollBar(StableId stable_id)
    : ScrollBar(std::move(stable_id), Orientation::horizontal) {}

namespace {

constexpr ControlAnatomy h_scroll_bar_anatomy{
    .control_id = "h_scroll_bar",
    .parts = scroll_bar_detail::parts,
    .states = scroll_bar_detail::states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& HScrollBar::anatomy() const noexcept {
    return h_scroll_bar_anatomy;
}

} // namespace gui_forms
