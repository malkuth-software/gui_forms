#pragma once

#include "gui_forms/binding/value/binding_value.hpp"

#include <memory>

namespace gui_forms::label_detail {

// The `horizontal_alignment` enumeration: near, center and far, numbered as
// HorizontalAlignment. Label's and FormLayout's tables name it, and the
// shared PropertyValueTraits<HorizontalAlignment>
// (gui_forms/controls/shared_property_enums.hpp) read it. Static storage;
// any thread may read it.
[[nodiscard]] std::shared_ptr<const PropertyEnumDescriptor> horizontal_alignment_enum();

} // namespace gui_forms::label_detail
