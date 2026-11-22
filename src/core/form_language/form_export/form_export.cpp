#include "form_export.hpp"

#include "../form_xml/form_xml.hpp"
#include "../../theme_language/source_text/theme_source_text.hpp"

#include "gui_forms/binding/value/binding_value.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <utility>

namespace gui_forms::form_language {

namespace {

// The control under `root` (root included) with `stable_id`, breadth first
// in explicit storage; null when none. The tree is bounded by
// maximum_control_tree_depth and the window's own size.
[[nodiscard]] Control::Ptr find_by_stable_id(const Control::Ptr& root, std::string_view stable_id) {
    std::vector<Control::Ptr> pending{root};
    std::size_t next = 0U;
    while (next < pending.size()) {
        const Control::Ptr current = pending[next];
        next = next + 1U;
        if ((*current).stable_id().value() == stable_id) return current;
        for (const Control::Ptr& child : (*current).children()) pending.push_back(child);
    }
    return Control::Ptr();
}

// Moves `from`'s children into `into` (whose own are removed first) and
// copies `from`'s locally set properties onto `into`. Called only after
// the candidate loaded: the window then changes in one pass.
void transplant(Control& into, Control& from) {
    into.clear_children();
    std::vector<Control::Ptr> moved{};
    for (const Control::Ptr& child : from.children()) moved.push_back(child);
    for (const Control::Ptr& child : moved) {
        Control::Ptr taken = from.remove_child((*child).runtime_id());
        into.add_child(std::move(taken));
    }
    for (const PropertyDescriptor& descriptor : from.property_descriptors()) {
        if (!descriptor.writable || !from.should_serialize_property(descriptor.name)) continue;
        const std::optional<BindingValue> value = from.property_value(descriptor.name);
        if (value) into.set_property_value(descriptor.name, *value);
    }
}

void report(theme_language::ThemeDiagnosticList& diagnostics, std::string_view file, std::string detail) {
    theme_language::ThemeDiagnostic diagnostic{};
    diagnostic.code = theme_language::DiagnosticCode::form_binding_source_refused;
    diagnostic.file = std::string(file);
    diagnostic.rule = "forms apply";
    diagnostic.detail = std::move(detail);
    diagnostics.add(std::move(diagnostic));
}

} // namespace

std::string_view form_export_outcome_name(FormExportOutcome outcome) noexcept {
    switch (outcome) {
    case FormExportOutcome::done: return "done";
    case FormExportOutcome::not_registered: return "not_registered";
    case FormExportOutcome::refused: return "refused";
    case FormExportOutcome::target_not_found: return "target_not_found";
    }
    return "unknown";
}

void FormExportRegistry::prune() noexcept {
    std::size_t kept = 0U;
    for (std::size_t index = 0U; index < records_.size(); ++index) {
        if (records_[index].root.expired()) continue;
        if (kept != index) records_[kept] = std::move(records_[index]);
        kept = kept + 1U;
    }
    records_.resize(kept);
}

FormExportRegistry::Record* FormExportRegistry::find(const Control& root) {
    prune();
    for (Record& record : records_) {
        const Control::Ptr held = record.root.lock();
        if (held.get() == &root) return &record;
    }
    return nullptr;
}

bool FormExportRegistry::add(const Control::Ptr& root, std::string file, std::string document,
                             const FormSchema& schema, const FormSources& sources) {
    if (root == nullptr) return false;
    Record made{};
    made.root = root;
    made.file = std::move(file);
    made.current_document = document;
    made.registered_document = std::move(document);
    made.schema = &schema;
    made.sources = &sources;
    Record* const existing = find(*root);
    if (existing != nullptr) {
        *existing = std::move(made);
        return true;
    }
    if (records_.size() >= maximum_exported_windows) return false;
    records_.push_back(std::move(made));
    return true;
}

void FormExportRegistry::forget(const Control& root) noexcept {
    for (Record& record : records_) {
        const Control::Ptr held = record.root.lock();
        if (held.get() == &root) record.root.reset();
    }
    prune();
}

bool FormExportRegistry::registered(const Control& root) {
    const bool known = find(root) != nullptr;
    return known;
}

bool FormExportRegistry::replaced(const Control& root) {
    const Record* const record = find(root);
    const bool changed = record != nullptr && (*record).replaced;
    return changed;
}

FormExportOutcome FormExportRegistry::export_document(const Control& root, std::string& document) {
    const Record* const record = find(root);
    if (record == nullptr) return FormExportOutcome::not_registered;
    document = (*record).current_document;
    return FormExportOutcome::done;
}

FormExportOutcome FormExportRegistry::validate(const Control& root, std::string_view file, std::string_view candidate,
                                               theme_language::ThemeDiagnosticList& diagnostics) {
    const Record* const record = find(root);
    if (record == nullptr) return FormExportOutcome::not_registered;
    theme_language::ThemeSource source{};
    source.file = file;
    source.text = candidate;
    FormSyntax syntax{};
    FormModel model{};
    if (read_form_model(source, syntax, *(*record).schema, model, diagnostics) != FormModelOutcome::model_built) {
        return FormExportOutcome::refused;
    }
    bool sourced = true;
    for (const FormProperty& property : model.properties) {
        if (!property.binding) continue;
        if ((*(*record).sources).find((*property.binding).source) != nullptr) continue;
        report(diagnostics, file,
               "`" + (*property.binding).source + "` is not a source the program exposes to this window");
        sourced = false;
    }
    return sourced ? FormExportOutcome::done : FormExportOutcome::refused;
}

FormExportOutcome FormExportRegistry::apply(const Control& root, std::string_view file, std::string_view candidate,
                                            std::string_view target, theme_language::ThemeDiagnosticList& diagnostics) {
    // validate first: it prunes, so a record found before it could move.
    const FormExportOutcome checked = validate(root, file, candidate, diagnostics);
    if (checked != FormExportOutcome::done) return checked;
    Record* const record = find(root);
    if (record == nullptr) return FormExportOutcome::not_registered;
    const Control::Ptr window_root = (*record).root.lock();
    Control::Ptr into = window_root;
    if (!target.empty()) {
        into = find_by_stable_id(window_root, target);
        if (into == nullptr) return FormExportOutcome::target_not_found;
    }
    theme_language::ThemeSource source{};
    source.file = file;
    source.text = candidate;
    FormSyntax syntax{};
    FormModel model{};
    if (read_form_model(source, syntax, *(*record).schema, model, diagnostics) != FormModelOutcome::model_built) {
        return FormExportOutcome::refused;
    }
    LoadedForm loaded{};
    if (load_form(source, *(*record).schema, model, *(*record).sources, loaded, diagnostics) !=
        FormLoadOutcome::loaded) {
        return FormExportOutcome::refused;
    }
    // The candidate's root (a form's control, or a control root) keeps the
    // replaced control's identity.
    if (loaded.root == nullptr || (*loaded.root).stable_id().value() != (*into).stable_id().value()) {
        report(diagnostics, file,
               "the candidate's root is `" +
                   std::string(loaded.root == nullptr ? std::string_view() : (*loaded.root).stable_id().value()) +
                   "`, not `" + std::string((*into).stable_id().value()) + "`, whose place it would take");
        return FormExportOutcome::refused;
    }
    transplant(*into, *loaded.root);
    (*record).applied_objects = std::move(loaded.objects);
    (*record).replaced = (*record).registered_document != candidate || !target.empty();
    if (target.empty()) (*record).current_document = std::string(candidate);
    return FormExportOutcome::done;
}

FormExportOutcome FormExportRegistry::revert(const Control& root, theme_language::ThemeDiagnosticList& diagnostics) {
    const Record* const record = find(root);
    if (record == nullptr) return FormExportOutcome::not_registered;
    const std::string original = (*record).registered_document;
    const std::string file = (*record).file;
    const FormExportOutcome outcome = apply(root, file, original, std::string_view(), diagnostics);
    // Found again: apply prunes.
    Record* const reverted = find(root);
    if (outcome == FormExportOutcome::done && reverted != nullptr) (*reverted).replaced = false;
    return outcome;
}

} // namespace gui_forms::form_language
