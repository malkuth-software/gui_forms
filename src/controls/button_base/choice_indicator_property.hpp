#pragma once

#include "gui_forms/binding/value/binding_value.hpp"

#include <memory>

namespace gui_forms::button_base_detail {

// The `indicator_style` enumeration CheckBox and RadioButton share:
// classic, modern and toggle, numbered as ChoiceIndicatorStyle. Static
// storage; any thread may read it.
[[nodiscard]] std::shared_ptr<const PropertyEnumDescriptor> choice_indicator_style_enum();

} // namespace gui_forms::button_base_detail
