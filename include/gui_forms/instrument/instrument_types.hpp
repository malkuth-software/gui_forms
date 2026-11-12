#pragma once

#include "gui_forms/export/application_export.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gui_forms {

enum class InstrumentFieldEditor : std::uint8_t { choice, text };

enum class InstrumentModuleState : std::uint8_t {
    live,
    staged,
    pending,
    invalid,
};

struct GUI_FORMS_API InstrumentFieldSpec final {
    std::string stable_id;
    std::string name;
    std::string value;
    InstrumentFieldEditor editor{InstrumentFieldEditor::choice};
    std::vector<std::string> choices;
    std::string validation_message;
    double width_weight{1.0};
    bool required{};
};

struct GUI_FORMS_API InstrumentModuleSpec final {
    std::string stable_id;
    std::string name;
    std::vector<InstrumentFieldSpec> fields;
    std::string status_text;
    InstrumentModuleState state{InstrumentModuleState::live};
    std::int32_t priority{};
    bool enabled{true};
    bool removable{true};
};

struct GUI_FORMS_API InstrumentFieldChange final {
    std::string module_id;
    std::string field_id;
    std::string previous_value;
    std::string current_value;
    bool committed{};
};

struct GUI_FORMS_API InstrumentModuleToggle final {
    std::string module_id;
    bool enabled{};
};

struct GUI_FORMS_API InstrumentModuleRequest final { std::string module_id; };

struct GUI_FORMS_API InstrumentModuleMoveRequest final {
    std::string module_id;
    std::size_t previous_index{};
    std::size_t requested_index{};
};

} // namespace gui_forms
