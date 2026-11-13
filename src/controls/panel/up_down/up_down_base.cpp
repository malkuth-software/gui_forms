#include "gui_forms/controls/up_down_base.hpp"

#include <utility>

namespace gui_forms {

UpDownBase::UpDownBase(StableId stable_id) : Panel(std::move(stable_id)) {}

UpDownBase::~UpDownBase() = default;

} // namespace gui_forms
