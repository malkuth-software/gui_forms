#pragma once

namespace gui_forms {

// A compile-time type name for a property read's conversion (study section
// 15): `(*c).visible.as_type(types::string)`. The tag is empty; its type
// carries the target, so overload resolution picks the conversion and no
// `<>` appears at a call site. The tags live in `namespace types`, the
// stock ones in property_type.hpp and each enumeration's beside its enum.
template <typename Target>
struct TypeTag final {};

} // namespace gui_forms
