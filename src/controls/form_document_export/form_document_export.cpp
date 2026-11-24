#include "gui_forms/binding/form_document_export/form_document_export.hpp"

#include "process_form_exports.hpp"

#if defined(GUI_FORMS_MCP)
#include "../automation/model_extensions/model_form_tools.hpp"
#include "../automation/tools/automation_tools.hpp"
#endif

#include "../../core/form_language/form_loader/form_loader.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms {

namespace {

// The sources of each registered form, owned here while its root lives;
// FormExportRegistry borrows them.
struct OwnedSources final {
    std::weak_ptr<Control> root{};
    std::unique_ptr<form_language::FormSources> sources{};
};

struct ProcessExports final {
    form_language::FormExportRegistry registry{};
    std::vector<OwnedSources> sources{};
};

[[nodiscard]] ProcessExports& process_exports() {
    static ProcessExports* const instance = new ProcessExports();
    return *instance;
}

[[nodiscard]] bool valid_utf8(std::string_view text) noexcept {
    std::size_t index = 0U;
    while (index < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        std::size_t length = 0U;
        if (lead < 0x80U) {
            length = 1U;
        } else if ((lead & 0xE0U) == 0xC0U && lead >= 0xC2U) {
            length = 2U;
        } else if ((lead & 0xF0U) == 0xE0U) {
            length = 3U;
        } else if ((lead & 0xF8U) == 0xF0U && lead <= 0xF4U) {
            length = 4U;
        } else {
            return false;
        }
        if (index + length > text.size()) return false;
        for (std::size_t next = 1U; next < length; ++next) {
            if ((static_cast<unsigned char>(text[index + next]) & 0xC0U) != 0x80U) return false;
        }
        index = index + length;
    }
    return true;
}

[[nodiscard]] FormDocumentSource make_source(const char* name, std::size_t name_bytes, FormDocumentSourceKind kind,
                                             void* source) noexcept {
    FormDocumentSource made{};
    made.name = name;
    made.name_bytes = name_bytes;
    made.kind = kind;
    made.source = source;
    return made;
}

// Adds `source` to `into`; false when FormSources refuses it (a repeated or
// invalid name).
[[nodiscard]] bool add_source(form_language::FormSources& into, const FormDocumentSource& source) {
    std::string name(source.name, source.name_bytes);
    try {
        switch (source.kind) {
        case FormDocumentSourceKind::boolean_value:
            into.add(std::move(name), *static_cast<Value<bool>*>(source.source));
            return true;
        case FormDocumentSourceKind::integer_value:
            into.add(std::move(name), *static_cast<Value<int>*>(source.source));
            return true;
        case FormDocumentSourceKind::number_value:
            into.add(std::move(name), *static_cast<Value<double>*>(source.source));
            return true;
        case FormDocumentSourceKind::command:
            into.add(std::move(name), *static_cast<Command*>(source.source));
            return true;
        case FormDocumentSourceKind::binding_source:
            into.add(std::move(name), (*static_cast<BindingSource*>(source.source)).shared_from_this());
            return true;
        }
    } catch (const std::invalid_argument&) {
        return false;
    } catch (const std::bad_weak_ptr&) {
        return false;
    }
    return false;
}

} // namespace

FormDocumentSource form_document_source(const char* name, std::size_t name_bytes, Value<bool>& source) noexcept {
    return make_source(name, name_bytes, FormDocumentSourceKind::boolean_value, &source);
}

FormDocumentSource form_document_source(const char* name, std::size_t name_bytes, Value<int>& source) noexcept {
    return make_source(name, name_bytes, FormDocumentSourceKind::integer_value, &source);
}

FormDocumentSource form_document_source(const char* name, std::size_t name_bytes, Value<double>& source) noexcept {
    return make_source(name, name_bytes, FormDocumentSourceKind::number_value, &source);
}

FormDocumentSource form_document_source(const char* name, std::size_t name_bytes, Command& source) noexcept {
    return make_source(name, name_bytes, FormDocumentSourceKind::command, &source);
}

FormDocumentSource form_document_source(const char* name, std::size_t name_bytes, BindingSource& source) noexcept {
    return make_source(name, name_bytes, FormDocumentSourceKind::binding_source, &source);
}

FormDocumentRegistration register_form_document(const Control::Ptr& root, const char* file, std::size_t file_bytes,
                                                const char* document, std::size_t document_bytes,
                                                const FormDocumentSource* sources, std::size_t source_count) {
    if (root == nullptr || file == nullptr || document == nullptr || (source_count != 0U && sources == nullptr)) {
        return FormDocumentRegistration::refused;
    }
    const std::string_view file_text(file, file_bytes);
    const std::string_view document_text(document, document_bytes);
    if (!valid_utf8(file_text) || !valid_utf8(document_text)) return FormDocumentRegistration::refused;
    // Validate and build first; the registry changes only after.
    std::unique_ptr<form_language::FormSources> made = std::make_unique<form_language::FormSources>();
    for (std::size_t index = 0U; index < source_count; ++index) {
        const FormDocumentSource& source = sources[index];
        if (source.name == nullptr || source.source == nullptr) return FormDocumentRegistration::refused;
        if (!valid_utf8(std::string_view(source.name, source.name_bytes))) return FormDocumentRegistration::refused;
        if (!add_source(*made, source)) return FormDocumentRegistration::refused;
    }
    ProcessExports& exports = process_exports();
    // Forget the sources of forms that ended or were disposed (whose records
    // the registry drops), and any earlier record of root.
    std::vector<OwnedSources> kept{};
    for (OwnedSources& owned : exports.sources) {
        const Control::Ptr held = owned.root.lock();
        if (held == nullptr || !(*held).is_alive() || held == root) continue;
        kept.push_back(std::move(owned));
    }
    const form_language::FormSources& borrowed = *made;
    if (!exports.registry.add(root, std::string(file_text), std::string(document_text), borrowed)) {
        exports.sources = std::move(kept);
        return FormDocumentRegistration::full;
    }
    OwnedSources owned{};
    owned.root = root;
    owned.sources = std::move(made);
    kept.push_back(std::move(owned));
    exports.sources = std::move(kept);
    // The forms.* tools serve registered documents only, so they join the
    // process's model tools with the first one: a program built without
    // GUI_FORMS_FORM_EXPORT carries neither them nor the registry catalog.
    // A later call finds the names taken and adds nothing. A build without
    // the automation (GUI_FORMS_MCP off) has no tools to add.
#if defined(GUI_FORMS_MCP)
    static_cast<void>(automation::register_model_form_tools(automation::model_tool_registry()));
#endif
    return FormDocumentRegistration::registered;
}

namespace form_language {

FormExportRegistry& process_form_exports() {
    FormExportRegistry& registry = process_exports().registry;
    return registry;
}

} // namespace form_language

} // namespace gui_forms
