#include "gui_forms/controls/list_control.hpp"

#include <utility>

namespace gui_forms {

ListControl::ListControl(StableId stable_id) : Panel(std::move(stable_id)) {}

ListControl::~ListControl() = default;

bool ListControl::record_selected_index(std::optional<std::size_t> index) noexcept {
    if (selected_index_value_ == index) return false;
    selected_index_value_ = index;
    return true;
}

} // namespace gui_forms
