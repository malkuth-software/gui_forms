#pragma once

#include "gui_forms/export/application_export.hpp"
#include <cstdint>
#include <string>

namespace gui_forms {

struct GUI_FORMS_API CommandState final {
    std::string text;
    std::string description;
    std::string icon_id;
    std::string shortcut;
    std::string mnemonic;
    std::string availability_reason;
    bool enabled{true};
    bool visible{true};
    bool checked{};
    bool default_action{};
    bool destructive{};
    std::uint64_t generation{};
    bool checkable{};
};

struct GUI_FORMS_API CommandInvocation final {
    std::string command_id;
    std::string source_id;
    std::uint64_t sequence{};
};

struct GUI_FORMS_API CommandBindingOptions final {
    bool synchronize_text{true};
    bool synchronize_visibility{true};
    bool synchronize_accessible_description{true};
    // Appended to preserve the established three-boolean aggregate order.
    bool synchronize_enabled{true};
};

} // namespace gui_forms
