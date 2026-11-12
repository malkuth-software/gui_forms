#include "gui_forms/controls/v_scroll_bar.hpp"

#include "../scroll_bar_anatomy.hpp"

#include <utility>

namespace gui_forms {

VScrollBar::VScrollBar(StableId stable_id)
    : ScrollBar(std::move(stable_id), Orientation::vertical) {}

namespace {

constexpr ControlAnatomy v_scroll_bar_anatomy{
    .control_id = "v_scroll_bar",
    .parts = scroll_bar_detail::parts,
    .states = scroll_bar_detail::states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& VScrollBar::anatomy() const noexcept {
    return v_scroll_bar_anatomy;
}

} // namespace gui_forms
