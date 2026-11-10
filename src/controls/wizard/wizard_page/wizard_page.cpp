#include "gui_forms/controls/wizard/wizard_page/wizard_page.hpp"

#include "gui_forms/controls/wizard/wizard.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <stdexcept>
#include <utility>

namespace gui_forms {

WizardPage::WizardPage(StableId stable_id) : Panel(std::move(stable_id)) {}

void WizardPage::notify_wizard() {
    const Control::Ptr owner = parent();
    Wizard* const wizard = dynamic_cast<Wizard*>(owner.get());
    if (wizard != nullptr) (*wizard).on_page_updated(*this);
}

void WizardPage::prepare_write(const decltype(title)&, std::string& incoming) {
    require_mutable();
    if (!validate_utf8(incoming).valid()) throw std::invalid_argument("WizardPage title must be valid UTF-8");
}

void WizardPage::written(const decltype(title)&) {
    invalidate(Dirty::paint | Dirty::semantics);
    notify_wizard();
}

void WizardPage::prepare_write(const decltype(description)&, std::string& incoming) {
    require_mutable();
    if (!validate_utf8(incoming).valid()) throw std::invalid_argument("WizardPage description must be valid UTF-8");
}

void WizardPage::written(const decltype(description)&) {
    invalidate(Dirty::paint | Dirty::semantics);
    notify_wizard();
}

void WizardPage::prepare_write(const decltype(can_advance)&, bool&) {
    require_mutable();
}

void WizardPage::written(const decltype(can_advance)&) {
    invalidate(Dirty::semantics);
    notify_wizard();
}

// property_codemod begin WizardPage (tools/property_families/wizard_page.json; generated, do not edit)

namespace {

PropertyDescriptor wizard_page_title_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "title";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The page's title.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_wizard_page_title() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        wizard_page_title_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor wizard_page_description_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "description";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The line under the title.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_wizard_page_description() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        wizard_page_description_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor wizard_page_can_advance_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "can_advance";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Behavior";
    descriptor.description = "Whether Next or Finish is available on this page.";
    descriptor.default_value = BindingValue{true};
    descriptor.invalidation_effects = Dirty::semantics;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_wizard_page_can_advance() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        wizard_page_can_advance_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, WizardPage::wizard_page_property_count>& WizardPage::wizard_page_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 3U> own{{
        {"title", &describe_wizard_page_title,
         &read_cell_entry<WizardPage, decltype(&WizardPage::title), &WizardPage::title>,
         &write_cell_entry<WizardPage, decltype(&WizardPage::title), &WizardPage::title>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"description", &describe_wizard_page_description,
         &read_cell_entry<WizardPage, decltype(&WizardPage::description), &WizardPage::description>,
         &write_cell_entry<WizardPage, decltype(&WizardPage::description), &WizardPage::description>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"can_advance", &describe_wizard_page_can_advance,
         &read_cell_entry<WizardPage, decltype(&WizardPage::can_advance), &WizardPage::can_advance>,
         &write_cell_entry<WizardPage, decltype(&WizardPage::can_advance), &WizardPage::can_advance>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "WizardPage's property table names a property twice");
    static const std::array<PropertyEntry, wizard_page_property_count> entries =
        join_property_tables(Panel::panel_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> WizardPage::property_table() const noexcept {
    const std::array<PropertyEntry, wizard_page_property_count>& entries = wizard_page_property_entries();
    return std::span<const PropertyEntry>(entries);
}

// property_codemod end WizardPage

} // namespace gui_forms
