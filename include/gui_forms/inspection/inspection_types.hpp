#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/input_controls.hpp"
#include "gui_forms/inspection/property_row_spec/property_row_spec.hpp"
#include "gui_forms/inspection/types/property_editor_kind/property_editor_kind.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms {

struct GUI_FORMS_API PropertyEditorRequest final {
    std::string stable_id;
    std::string property_path;
    PropertyDescriptor descriptor;
    BindingValue value;
    bool writable{};
    bool top_level{};
};

struct GUI_FORMS_API PropertyEditorInputError final {
    std::string attempted_value;
    std::string message;
};

struct GUI_FORMS_API PropertyGroupSpec final {
    std::string stable_id;
    std::string title;
    std::vector<PropertyRowSpec> rows;
    bool expanded{true};
};

struct GUI_FORMS_API PropertyValueChange final {
    std::string row_id;
    std::string previous_value;
    std::string current_value;
    bool committed{};
};

struct GUI_FORMS_API PropertyGroupChange final {
    std::string group_id;
    bool expanded{};
};

struct GUI_FORMS_API PropertyRowExpansionChange final {
    std::string row_id;
    bool expanded{};
};

struct GUI_FORMS_API PropertyResetRequest final { std::string row_id; };

enum class PropertySort : std::uint8_t { categorized, alphabetical };

struct GUI_FORMS_API PropertyGridValueChange final {
    std::string property_name;
    BindingValue previous_value;
    BindingValue current_value;
    PropertyValueOrigin origin{PropertyValueOrigin::computed};
    bool reset{};
};

struct GUI_FORMS_API PropertyGridEditError final {
    std::string property_name;
    std::string attempted_value;
    std::string message;
};

} // namespace gui_forms
