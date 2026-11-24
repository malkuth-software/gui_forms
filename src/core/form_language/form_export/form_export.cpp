#include "form_export.hpp"

#include "../form_xml/form_xml.hpp"
#include "../../theme_language/source_text/theme_source_text.hpp"

#include "gui_forms/binding/value/binding_value.hpp"

#include <algorithm>
#include <exception>
#include <string>
#include <vector>
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

// A writable property of the control being replaced, as it was: its local
// value, or none when it was not locally set (put back by a reset).
struct PropertySnapshot final {
    std::string name{};
    std::optional<BindingValue> local{};
};

// What transplant replaces on `into`, taken before it changes anything.
struct ControlSnapshot final {
    std::vector<Control::Ptr> children{};
    std::vector<PropertySnapshot> properties{};
};

[[nodiscard]] ControlSnapshot snapshot_of(const Control& into) {
    ControlSnapshot taken{};
    for (const Control::Ptr& child : into.children()) taken.children.push_back(child);
    for (const PropertyDescriptor& descriptor : into.property_descriptors()) {
        if (!descriptor.writable) continue;
        PropertySnapshot property{};
        property.name = descriptor.name;
        if (into.should_serialize_property(descriptor.name)) property.local = into.property_value(descriptor.name);
        taken.properties.push_back(std::move(property));
    }
    return taken;
}

// Puts `into` back as `snapshot` holds it; false when the window refused
// that too.
[[nodiscard]] bool restore(Control& into, const ControlSnapshot& snapshot) noexcept {
    try {
        into.clear_children();
        for (const Control::Ptr& child : snapshot.children) into.add_child(child);
        for (const PropertySnapshot& property : snapshot.properties) {
            if (property.local) {
                into.set_property_value(property.name, *property.local);
            } else {
                static_cast<void>(into.reset_property(property.name));
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

// Moves `from`'s children into `into` (whose own are removed first) and
// copies `from`'s locally set properties onto `into`. Called only after
// the candidate loaded. A step the window refuses throws; `into` is then
// put back from a snapshot taken first, and the exception goes on with
// `restored` saying whether that worked (hunt r06 C10).
void transplant(Control& into, Control& from, bool& restored) {
    const ControlSnapshot before = snapshot_of(into);
    std::vector<Control::Ptr> moved{};
    for (const Control::Ptr& child : from.children()) moved.push_back(child);
    try {
        into.clear_children();
        for (const Control::Ptr& child : moved) {
            Control::Ptr taken = from.remove_child((*child).runtime_id());
            into.add_child(std::move(taken));
        }
        for (const PropertyDescriptor& descriptor : from.property_descriptors()) {
            if (!descriptor.writable || !from.should_serialize_property(descriptor.name)) continue;
            const std::optional<BindingValue> value = from.property_value(descriptor.name);
            if (value) into.set_property_value(descriptor.name, *value);
        }
    } catch (...) {
        restored = restore(into, before);
        throw;
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

std::string_view form_export_state_name(FormExportState state) noexcept {
    switch (state) {
    case FormExportState::registered: return "registered";
    case FormExportState::applied: return "applied";
    case FormExportState::patched: return "patched";
    }
    return "unknown";
}

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
                             const FormSources& sources) {
    if (root == nullptr) return false;
    Record made{};
    made.root = root;
    made.file = std::move(file);
    made.current_document = document;
    made.registered_document = std::move(document);
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
    const bool changed = state(root) != FormExportState::registered;
    return changed;
}

FormExportState FormExportRegistry::state(const Control& root) {
    const Record* const record = find(root);
    if (record == nullptr) return FormExportState::registered;
    return (*record).state;
}

FormExportOutcome FormExportRegistry::export_document(const Control& root, std::string& document) {
    const Record* const record = find(root);
    if (record == nullptr) return FormExportOutcome::not_registered;
    document = (*record).current_document;
    return FormExportOutcome::done;
}

FormExportOutcome FormExportRegistry::validate(const Control& root, const FormSchema& schema, std::string_view file,
                                               std::string_view candidate,
                                               theme_language::ThemeDiagnosticList& diagnostics) {
    const Record* const record = find(root);
    if (record == nullptr) return FormExportOutcome::not_registered;
    theme_language::ThemeSource source{};
    source.file = file;
    source.text = candidate;
    FormSyntax syntax{};
    FormModel model{};
    if (read_form_model(source, syntax, schema, model, diagnostics) != FormModelOutcome::model_built) {
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

FormExportOutcome FormExportRegistry::apply(const Control& root, const FormSchema& schema, std::string_view file,
                                            std::string_view candidate, std::string_view target,
                                            theme_language::ThemeDiagnosticList& diagnostics) {
    // validate first: it prunes, so a record found before it could move.
    const FormExportOutcome checked = validate(root, schema, file, candidate, diagnostics);
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
    if (read_form_model(source, syntax, schema, model, diagnostics) != FormModelOutcome::model_built) {
        return FormExportOutcome::refused;
    }
    LoadedForm loaded{};
    if (load_form(source, schema, model, *(*record).sources, loaded, diagnostics) !=
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
    bool restored = true;
    try {
        transplant(*into, *loaded.root, restored);
    } catch (const std::exception& error) {
        // A handler run by the window may have ended it; the record is found
        // again (find prunes).
        Record* const refused = find(root);
        if (!restored && refused != nullptr) (*refused).state = FormExportState::patched;
        report(diagnostics, file,
               std::string(restored ? "the window refused the candidate and is as it was: "
                                    : "the window refused the candidate and could not be put back: ") +
                   error.what());
        return FormExportOutcome::refused;
    }
    // Found again: the transplant ran the window's handlers.
    Record* const applied = find(root);
    if (applied == nullptr) return FormExportOutcome::not_registered;
    (*applied).applied_objects = std::move(loaded.objects);
    if (!target.empty()) {
        (*applied).state = FormExportState::patched;
    } else {
        (*applied).current_document = std::string(candidate);
        (*applied).state = (*applied).registered_document == candidate ? FormExportState::registered
                                                                       : FormExportState::applied;
    }
    return FormExportOutcome::done;
}

FormExportOutcome FormExportRegistry::revert(const Control& root, const FormSchema& schema,
                                             theme_language::ThemeDiagnosticList& diagnostics) {
    const Record* const record = find(root);
    if (record == nullptr) return FormExportOutcome::not_registered;
    const std::string original = (*record).registered_document;
    const std::string file = (*record).file;
    const FormExportOutcome outcome = apply(root, schema, file, original, std::string_view(), diagnostics);
    // Found again: apply prunes.
    Record* const reverted = find(root);
    if (outcome == FormExportOutcome::done && reverted != nullptr) (*reverted).state = FormExportState::registered;
    return outcome;
}

} // namespace gui_forms::form_language
