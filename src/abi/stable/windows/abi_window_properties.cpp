// A window's chrome properties through the C ABI. See abi_window_properties.hpp.

#include "abi_window_properties.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <variant>

namespace gui_forms::abi1 {
namespace {

constexpr std::array<WindowPropertyChoice, 7> border_style_choices{{
    {"none", 0},
    {"fixed_single", 1},
    {"fixed_3d", 2},
    {"fixed_dialog", 3},
    {"sizable", 4},
    {"fixed_tool_window", 5},
    {"sizable_tool_window", 6},
}};

constexpr std::array<WindowPropertyChoice, 3> window_state_choices{{
    {"normal", 0},
    {"minimized", 1},
    {"maximized", 2},
}};

constexpr std::array<WindowPropertyChoice, 5> start_position_choices{{
    {"manual", 0},
    {"center_screen", 1},
    {"windows_default_location", 2},
    {"windows_default_bounds", 3},
    {"center_parent", 4},
}};

// Windows Forms' categories for the same Form properties.
constexpr std::array<WindowPropertyEntry, 12> entries{{
    {"text", WindowChromeField::text, GF_VALUE_KIND_TEXT, "appearance", {}, {}},
    {"icon", WindowChromeField::icon, GF_VALUE_KIND_IMAGE, "window_style", {}, {}},
    {"form_border_style", WindowChromeField::form_border_style, GF_VALUE_KIND_ENUMERATION, "appearance",
     "form_border_style", border_style_choices},
    {"minimize_box", WindowChromeField::minimize_box, GF_VALUE_KIND_BOOLEAN, "window_style", {}, {}},
    {"maximize_box", WindowChromeField::maximize_box, GF_VALUE_KIND_BOOLEAN, "window_style", {}, {}},
    {"control_box", WindowChromeField::control_box, GF_VALUE_KIND_BOOLEAN, "window_style", {}, {}},
    {"window_state", WindowChromeField::window_state, GF_VALUE_KIND_ENUMERATION, "layout", "form_window_state",
     window_state_choices},
    {"top_most", WindowChromeField::top_most, GF_VALUE_KIND_BOOLEAN, "window_style", {}, {}},
    {"opacity", WindowChromeField::opacity, GF_VALUE_KIND_NUMBER, "window_style", {}, {}},
    {"start_position", WindowChromeField::start_position, GF_VALUE_KIND_ENUMERATION, "layout",
     "form_start_position", start_position_choices},
    {"show_in_taskbar", WindowChromeField::show_in_taskbar, GF_VALUE_KIND_BOOLEAN, "window_style", {}, {}},
    {"minimum_size", WindowChromeField::minimum_size, GF_VALUE_KIND_SIZE, "layout", {}, {}},
}};

// The choice of that value; the first when none matches (the property's
// own value is always one of its choices).
const WindowPropertyChoice& choice_of(const WindowPropertyEntry& property, std::int64_t value) noexcept {
    for (const WindowPropertyChoice& choice : property.choices) {
        if (choice.value == value) return choice;
    }
    return property.choices.front();
}

PropertyEnumValue enumeration_value(const WindowPropertyEntry& property, std::int64_t value) {
    const WindowPropertyChoice& choice = choice_of(property, value);
    PropertyEnumValue held{};
    held.type_name = std::string(property.enumeration);
    held.name = std::string(choice.name);
    held.value = choice.value;
    return held;
}

// An enumeration written as a signed or unsigned integer or as an
// enumeration value; false for any other kind, or one past 255 (no window
// enumeration reaches that far; the hooks refuse the values in between).
bool read_enumeration(const BindingValue& value, std::uint8_t& read) noexcept {
    std::int64_t number = -1;
    if (std::holds_alternative<std::int64_t>(value)) {
        number = std::get<std::int64_t>(value);
    } else if (std::holds_alternative<std::uint64_t>(value)) {
        const std::uint64_t unsigned_number = std::get<std::uint64_t>(value);
        if (unsigned_number <= 255U) number = static_cast<std::int64_t>(unsigned_number);
    } else if (std::holds_alternative<PropertyEnumValue>(value)) {
        number = std::get<PropertyEnumValue>(value).value;
    }
    if (number < 0 || number > 255) return false;
    read = static_cast<std::uint8_t>(number);
    return true;
}

// A number written as a number or an integer.
bool read_number(const BindingValue& value, double& read) noexcept {
    if (std::holds_alternative<double>(value)) {
        read = std::get<double>(value);
        return true;
    }
    if (std::holds_alternative<std::int64_t>(value)) {
        read = static_cast<double>(std::get<std::int64_t>(value));
        return true;
    }
    if (std::holds_alternative<std::uint64_t>(value)) {
        read = static_cast<double>(std::get<std::uint64_t>(value));
        return true;
    }
    return false;
}

// Writes value to the property, or returns false for a value of another
// kind. The Window hooks throw std::invalid_argument for a refused value.
bool write_property(Window& window, const WindowPropertyEntry& property, const BindingValue& value) {
    std::uint8_t enumeration = 0U;
    double number = 0.0;
    switch (property.field) {
    case WindowChromeField::text:
        if (!std::holds_alternative<std::string>(value)) return false;
        window.text = std::get<std::string>(value);
        return true;
    case WindowChromeField::icon:
        if (std::holds_alternative<std::monostate>(value)) {
            window.icon = ImageId{};
            return true;
        }
        if (!std::holds_alternative<ImageId>(value)) return false;
        window.icon = std::get<ImageId>(value);
        return true;
    case WindowChromeField::form_border_style:
        if (!read_enumeration(value, enumeration)) return false;
        window.form_border_style = static_cast<FormBorderStyle>(enumeration);
        return true;
    case WindowChromeField::minimize_box:
        if (!std::holds_alternative<bool>(value)) return false;
        window.minimize_box = std::get<bool>(value);
        return true;
    case WindowChromeField::maximize_box:
        if (!std::holds_alternative<bool>(value)) return false;
        window.maximize_box = std::get<bool>(value);
        return true;
    case WindowChromeField::control_box:
        if (!std::holds_alternative<bool>(value)) return false;
        window.control_box = std::get<bool>(value);
        return true;
    case WindowChromeField::window_state:
        if (!read_enumeration(value, enumeration)) return false;
        window.window_state = static_cast<FormWindowState>(enumeration);
        return true;
    case WindowChromeField::top_most:
        if (!std::holds_alternative<bool>(value)) return false;
        window.top_most = std::get<bool>(value);
        return true;
    case WindowChromeField::opacity:
        if (!read_number(value, number)) return false;
        window.opacity = number;
        return true;
    case WindowChromeField::start_position:
        if (!read_enumeration(value, enumeration)) return false;
        window.start_position = static_cast<FormStartPosition>(enumeration);
        return true;
    case WindowChromeField::show_in_taskbar:
        if (!std::holds_alternative<bool>(value)) return false;
        window.show_in_taskbar = std::get<bool>(value);
        return true;
    case WindowChromeField::minimum_size:
        if (!std::holds_alternative<Size>(value)) return false;
        window.minimum_size = std::get<Size>(value);
        return true;
    }
    return false;
}

} // namespace

std::span<const WindowPropertyEntry> window_properties() noexcept {
    return entries;
}

const WindowPropertyEntry* find_window_property(std::string_view name) noexcept {
    for (const WindowPropertyEntry& entry : entries) {
        if (entry.name == name) return &entry;
    }
    return nullptr;
}

BindingValue window_property_value(const Window& window, const WindowPropertyEntry& property) {
    switch (property.field) {
    case WindowChromeField::text: return BindingValue{window.text.read()};
    case WindowChromeField::icon: return BindingValue{window.icon.read()};
    case WindowChromeField::form_border_style:
        return BindingValue{enumeration_value(property, static_cast<std::int64_t>(window.form_border_style.read()))};
    case WindowChromeField::minimize_box: return BindingValue{window.minimize_box.read()};
    case WindowChromeField::maximize_box: return BindingValue{window.maximize_box.read()};
    case WindowChromeField::control_box: return BindingValue{window.control_box.read()};
    case WindowChromeField::window_state:
        return BindingValue{enumeration_value(property, static_cast<std::int64_t>(window.window_state.read()))};
    case WindowChromeField::top_most: return BindingValue{window.top_most.read()};
    case WindowChromeField::opacity: return BindingValue{window.opacity.read()};
    case WindowChromeField::start_position:
        return BindingValue{enumeration_value(property, static_cast<std::int64_t>(window.start_position.read()))};
    case WindowChromeField::show_in_taskbar: return BindingValue{window.show_in_taskbar.read()};
    case WindowChromeField::minimum_size: return BindingValue{window.minimum_size.read()};
    }
    const BindingValue none{};
    return none;
}

gf_status set_window_property(Window& window, const WindowPropertyEntry& property, const BindingValue& value,
                              SlotIdentity slot) {
    try {
        const bool written = write_property(window, property, value);
        if (!written) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the value is not of the window property's kind");
        }
    } catch (const std::invalid_argument& refused) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, refused.what());
    }
    return GF_STATUS_OK;
}

void reset_window_property(Window& window, const WindowPropertyEntry& property) {
    // A new window's chrome: the defaults WindowChrome declares.
    const WindowChrome defaults{};
    switch (property.field) {
    case WindowChromeField::text: window.text = defaults.text; return;
    case WindowChromeField::icon: window.icon = ImageId{}; return;
    case WindowChromeField::form_border_style: window.form_border_style = defaults.form_border_style; return;
    case WindowChromeField::minimize_box: window.minimize_box = defaults.minimize_box; return;
    case WindowChromeField::maximize_box: window.maximize_box = defaults.maximize_box; return;
    case WindowChromeField::control_box: window.control_box = defaults.control_box; return;
    case WindowChromeField::window_state: window.window_state = defaults.window_state; return;
    case WindowChromeField::top_most: window.top_most = defaults.top_most; return;
    case WindowChromeField::opacity: window.opacity = defaults.opacity; return;
    case WindowChromeField::start_position: window.start_position = defaults.start_position; return;
    case WindowChromeField::show_in_taskbar: window.show_in_taskbar = defaults.show_in_taskbar; return;
    case WindowChromeField::minimum_size: window.minimum_size = defaults.minimum_size; return;
    default:
        // The entries come from this table's own list, which names only the
        // fields above: there is nothing else to reset.
        return;
    }
}

} // namespace gui_forms::abi1
