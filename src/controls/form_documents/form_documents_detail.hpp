#pragma once

// The form documents' implementation state (form_documents.cpp), shared
// with the mockup (form_mockup.cpp), which reports its checks in the same
// FormDocumentCheck. Internal.

#include "gui_forms/forms/form_documents/form_documents.hpp"

#include "../../core/form_language/form_model/form_model.hpp"
#include "../../core/theme_language/diagnostics/theme_diagnostics.hpp"

#include "gui_forms/component.hpp"
#include "gui_forms/window/window.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms {

namespace detail {

// A schema's types, borrowed by the schema object.
struct FormDocumentSchemaState final {
    const form_language::FormSchema* schema{};
};

// One diagnostic as the check keeps it: the record, and its code's label
// (made per record by the diagnostics' formatter).
struct FormDocumentCheckRecord final {
    theme_language::ThemeDiagnostic record{};
    std::string label{};
};

struct FormDocumentCheckState final {
    FormDocumentCheckState() noexcept = default;

    bool compiled{};
    std::size_t errors{};
    std::vector<FormDocumentCheckRecord> records{};
};

// A loaded window, with the components only the load owns, and its title.
struct FormDocumentWindowState final {
    FormDocumentWindowState() noexcept = default;

    // Declared before the window, which ends first.
    std::vector<Component::Ptr> components{};
    std::unique_ptr<Window> window{};
    std::string title{};
};

struct FormDocumentAccess final {
    [[nodiscard]] static const FormDocumentSchemaState* state(const FormDocumentSchema& schema) noexcept {
        return schema.state_;
    }
    [[nodiscard]] static FormDocumentSchema* make_schema(const FormDocumentSchemaState* state) {
        return new FormDocumentSchema(state);
    }
    [[nodiscard]] static FormDocumentCheck make_check(stxlib::Unique<FormDocumentCheckState> state) noexcept {
        FormDocumentCheck check(std::move(state));
        return check;
    }
    [[nodiscard]] static FormDocumentWindow make_window(stxlib::Unique<FormDocumentWindowState> state,
                                                        FormDocumentCheck check) noexcept {
        FormDocumentWindow window(std::move(state), std::move(check));
        return window;
    }
};

} // namespace detail

namespace detail {

// The check of `diagnostics`, in order, whether the document `compiled`;
// empty when the state could not be made (no memory).
[[nodiscard]] stxlib::Unique<FormDocumentCheckState> form_document_check_state(
    const theme_language::ThemeDiagnosticList& diagnostics, bool compiled);

// The types of the schema `state` describes; the registry's when it holds
// none.
[[nodiscard]] const form_language::FormSchema& form_document_schema(const FormDocumentSchemaState* state);

} // namespace detail

} // namespace gui_forms
