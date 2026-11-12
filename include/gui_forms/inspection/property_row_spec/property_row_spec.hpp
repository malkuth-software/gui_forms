#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/inspection/types/property_editor_kind/property_editor_kind.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace gui_forms {

// Authored settings-row state consumed by PropertyList and PropertyGrid.
struct GUI_FORMS_API PropertyRowSpec final {
    PropertyRowSpec() = default;
    PropertyRowSpec(std::string authored_stable_id,
                    std::string authored_name,
                    std::string authored_value,
                    std::string authored_description = {},
                    PropertyEditorKind authored_editor =
                        PropertyEditorKind::read_only,
                    std::vector<std::string> authored_choices = {},
                    std::string authored_validation = {},
                    bool authored_enabled = true,
                    bool authored_required = false,
                    std::string authored_parent_id = {},
                    std::uint8_t authored_depth = 0,
                    bool authored_expandable = false,
                    bool authored_expanded = false,
                    bool authored_resettable = false,
                    bool authored_reset_enabled = false);

    std::string stable_id;
    std::string name;
    std::string value;
    std::string description;
    PropertyEditorKind editor{PropertyEditorKind::read_only};
    std::vector<std::string> choices;
    std::string validation_message;
    std::string parent_id;
    std::uint8_t depth{};
    bool enabled{true};
    bool required{};
    bool expandable{};
    bool expanded{};
    bool resettable{};
    bool reset_enabled{};
};

} // namespace gui_forms
