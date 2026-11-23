#pragma once

#include "gui_forms/binding/value/binding_value.hpp"

#include <memory>

namespace gui_forms::scaled_layout_detail {

// The `scaled_aspect` enumeration: stretch and uniform, numbered as
// ScaledAspect (stage VA, decision 10). ScaledPanel's and ScaledGroupBox's
// tables name it, and the shared PropertyValueTraits<ScaledAspect>
// (gui_forms/controls/shared_property_enums.hpp) read it. Defined in
// scaled_panel.cpp. Static storage; any thread may read it.
[[nodiscard]] std::shared_ptr<const PropertyEnumDescriptor> scaled_aspect_enum();

} // namespace gui_forms::scaled_layout_detail
