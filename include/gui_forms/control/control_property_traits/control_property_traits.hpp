#pragma once

#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/control/control_layout_styles/control_layout_styles.hpp"
#include "gui_forms/layout/types/layout_types.hpp"
#include "gui_forms/types.hpp"

#include <optional>

namespace gui_forms {

// Control's enumerations as property values: their choice sets are
// Control's descriptors, so the definitions live in control.cpp. The cursor
// travels as cursor_kind with inherit (empty) at 0 and each kind at its
// value plus one.
template <>
struct PropertyValueTraits<DockStyle> {
    static PropertyType type();
    static BindingValue to_binding(DockStyle value);
    static DockStyle from_binding(const BindingValue& value);
};

template <>
struct PropertyValueTraits<AnchorStyles> {
    static PropertyType type();
    static BindingValue to_binding(AnchorStyles value);
    static AnchorStyles from_binding(const BindingValue& value);
};

template <>
struct PropertyValueTraits<AutoSizeMode> {
    static PropertyType type();
    static BindingValue to_binding(AutoSizeMode value);
    static AutoSizeMode from_binding(const BindingValue& value);
};

template <>
struct PropertyValueTraits<LayoutDirection> {
    static PropertyType type();
    static BindingValue to_binding(LayoutDirection value);
    static LayoutDirection from_binding(const BindingValue& value);
};

template <>
struct PropertyValueTraits<std::optional<CursorKind>> {
    static PropertyType type();
    static BindingValue to_binding(std::optional<CursorKind> value);
    static std::optional<CursorKind> from_binding(const BindingValue& value);
};

namespace types {

inline constexpr TypeTag<std::optional<CursorKind>> optional_cursor_kind{};

} // namespace types

} // namespace gui_forms
