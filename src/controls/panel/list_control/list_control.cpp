#include "gui_forms/controls/list_control.hpp"

#include <utility>

namespace gui_forms {

ListControl::ListControl(StableId stable_id) : Panel(std::move(stable_id)) {}

ListControl::~ListControl() = default;

} // namespace gui_forms
