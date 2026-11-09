#pragma once

// A window's chrome properties through the C ABI (since 1.1): the schema's
// "window" type lists them (schema property_at), and properties.get, set
// and reset take their names on a window from windows.create, before and
// during its run. Each is one of Window's chrome properties
// (gui_forms/window/chrome); a write while the window runs reaches the
// host at once, where the host supports the property
// (windows.window_properties reports which).

#include "../failure/abi_failure.hpp"

#include "gui_forms/binding.hpp"
#include "gui_forms/c/gf_abi.h"
#include "gui_forms/window.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace gui_forms::abi1 {

// One window property: its schema name, the chrome field it is, its value
// kind and category, and for an enumeration its type name and choices.
struct WindowPropertyChoice final {
    std::string_view name;
    std::int64_t value = 0;
};

struct WindowPropertyEntry final {
    std::string_view name;
    WindowChromeField field = WindowChromeField::text;
    gf_value_kind kind = GF_VALUE_KIND_NULL;
    std::string_view category;
    // Empty when the property is not an enumeration.
    std::string_view enumeration;
    std::span<const WindowPropertyChoice> choices;
};

// The window properties, in WindowChromeField order (static storage).
[[nodiscard]] std::span<const WindowPropertyEntry> window_properties() noexcept;

// The property of that schema name; null when a window has none.
[[nodiscard]] const WindowPropertyEntry* find_window_property(std::string_view name) noexcept;

// The property's current value on window.
[[nodiscard]] BindingValue window_property_value(const Window& window, const WindowPropertyEntry& property);

// Writes value to the property: GF_STATUS_INVALID_ARGUMENT, changing
// nothing, for a value of another kind or one the property refuses (the
// Window hooks' std::invalid_argument). Root thread.
[[nodiscard]] gf_status set_window_property(Window& window, const WindowPropertyEntry& property,
                                            const BindingValue& value, SlotIdentity slot);

// Writes the property's default value (the value a new window has).
void reset_window_property(Window& window, const WindowPropertyEntry& property);

} // namespace gui_forms::abi1
