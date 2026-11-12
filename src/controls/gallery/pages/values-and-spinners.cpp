#include "pages.hpp"
#include "gui_forms/controls/combo_box.hpp"
#include "gui_forms/controls/command_bar.hpp"
#include "gui_forms/controls/domain_up_down.hpp"
#include "gui_forms/controls/numeric_up_down.hpp"
#include "gui_forms/controls/property_grid.hpp"
#include "gui_forms/inspection/inspection_types.hpp"

#include "values-and-spinners_form.hpp"

#include <vector>

namespace gui_forms::gallery::pages {
namespace {

// Owner of the NumericUpDown and DomainUpDown specimens and the PropertyGrid
// inspector: the controls demo/pages/values-and-spinners.form describes, and
// their behaviour. The four inspector actions are application Commands shown
// through one CommandBar; the grid's commits and each spinner's state
// changes report to the status label through named members.
class ValuesPage final : public gui_forms::Component {
public:
    ValuesPage(const std::shared_ptr<Surface>& body,
               const std::shared_ptr<PageContext>& context) {
        document_.initialize_component(*this);
        mount_document(body);
        build_numerics();
        build_domain();
        build_inspector();
        hand_over_binding(*context);
        on(inspect_numeric_.invoked(), *this, &ValuesPage::inspect_numeric);
        on(inspect_items_.invoked(), *this, &ValuesPage::inspect_items);
        on(inspect_style_.invoked(), *this, &ValuesPage::inspect_style);
        on(add_item_.invoked(), *this, &ValuesPage::add_item);
        on((*inspector_).property_value_changed(), *this, &ValuesPage::property_changed);
    }

private:
    static constexpr int command_inspect_numeric = 1;
    static constexpr int command_inspect_items = 2;
    static constexpr int command_inspect_style = 3;
    static constexpr int command_add_item = 4;

    void mount_document(const std::shared_ptr<Surface>& body) {
        // The document's root is a declared boundary only: its children move
        // into the shell's page body, where each is placed by its bounds in
        // design coordinates. The copy of the child list keeps the iteration
        // valid while the root shrinks.
        Control& root = *document_.gallery_values_document;
        const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
        for (const Control::Ptr& child : children) {
            Control::Ptr moved = root.remove_child((*child).runtime_id());
            (*body).add_child(std::move(moved));
        }
        numerics_ = {document_.gallery_values_numeric_0, document_.gallery_values_numeric_1,
                     document_.gallery_values_numeric_2, document_.gallery_values_numeric_3};
        domain_ = document_.gallery_values_domain;
        inspector_ = document_.gallery_values_property_grid;
        items_target_ = document_.gallery_values_items_target;
        commands_ = document_.gallery_values_inspector_commands;
        status_ = document_.gallery_values_status;
    }

    // Each spinner's range is set before its value, so the value is never
    // clamped to the default range.
    void build_numerics() {
        NumericUpDown& integer = *numerics_[0];
        integer.set_range(0.0, 100.0);
        integer.value = 42.0;
        NumericUpDown& decimal = *numerics_[1];
        decimal.set_range(0.0, 10.0);
        decimal.increment = 0.25;
        decimal.decimal_places = 2U;
        decimal.value = 3.75;
        NumericUpDown& signed_range = *numerics_[2];
        signed_range.set_range(-120.0, 120.0);
        signed_range.increment = 5.0;
        signed_range.value = -15.0;
        NumericUpDown& hexadecimal = *numerics_[3];
        hexadecimal.set_range(0.0, 255.0);
        hexadecimal.hexadecimal = true;
        hexadecimal.value = 175.0;
        for (std::size_t index = 0; index < numerics_.size(); ++index) {
            on((*numerics_[index]).value_changed(), *this, &ValuesPage::numeric_changed,
               static_cast<int>(index));
        }
    }

    // The DomainUpDown below the numeric rows: demodulation modes, wrapping.
    void build_domain() {
        (*domain_).wrap = true;
        (*domain_).selected_index = 1U;
        on((*domain_).selected_index_changed(), *this, &ValuesPage::domain_changed);
    }

    void build_inspector() {
        (*inspector_).set_selected_object(numerics_[1]);
        static_cast<void>((*inspector_).set_property_expanded("bounds", true));

        inspect_numeric_.set_text("Numeric");
        inspect_numeric_.set_description("Inspect the decimal NumericUpDown");
        inspect_items_.set_text("Items");
        inspect_items_.set_description("Inspect the ComboBox Items collection");
        inspect_style_.set_text("Style");
        inspect_style_.set_description("Inspect the status label's color and flags");
        add_item_.set_text("Add CW");
        add_item_.set_description("Insert CW through the ComboBox.Items setter");
        (*commands_).set_items({
            {.id = command_inspect_numeric, .label = "Numeric",
             .accessible_name = "Inspect numeric", .command = &inspect_numeric_},
            {.id = command_inspect_items, .label = "Items",
             .accessible_name = "Inspect items", .command = &inspect_items_},
            {.id = command_inspect_style, .label = "Style",
             .accessible_name = "Inspect style", .command = &inspect_style_},
            {.id = command_add_item, .label = "Add CW",
             .accessible_name = "Add CW item", .command = &add_item_}});
        for (int index = 0; index < 4; ++index) {
            (*commands_).item(index).visual_style = ButtonVisualStyle::command;
        }
    }

    // The BindingSource specimen is wired by the shell once a Window exists
    // (initialize_gallery_runtime); the page hands it the controls.
    void hand_over_binding(PageContext& context) {
        context.binding_editor = document_.gallery_values_binding_name;
        context.binding_enabled = document_.gallery_values_binding_enabled;
        context.binding_gain = document_.gallery_values_binding_gain;
        context.binding_previous = document_.gallery_values_binding_previous;
        context.binding_next = document_.gallery_values_binding_next;
        context.binding_status = document_.gallery_values_binding_status;
        context.binding_navigator = document_.gallery_values_binding_navigator;
    }

    void inspect_numeric() {
        (*inspector_).set_selected_object(numerics_[1]);
        static_cast<void>((*inspector_).set_property_expanded("bounds", true));
        (*status_).text = "PropertyGrid · retained NumericUpDown factory and typed commit";
    }

    void inspect_items() {
        (*inspector_).set_selected_object(items_target_);
        static_cast<void>((*inspector_).set_property_expanded("items", true));
        focus_editor("Items[0]");
        (*status_).text = "PropertyGrid · ComboBox.Items immutable collection expanded";
    }

    void inspect_style() {
        (*inspector_).set_selected_object(status_);
        static_cast<void>((*inspector_).set_property_expanded("fore-color", true));
        (*status_).text = "PropertyGrid · flags overlay and canonical color editor services";
    }

    void add_item() {
        if ((*inspector_).selected_object() != items_target_) {
            (*inspector_).set_selected_object(items_target_);
        }
        static_cast<void>((*inspector_).set_property_expanded("items", true));
        const bool added = (*inspector_).insert_collection_item(
            "items", (*items_target_).items.read().size(), BindingValue{std::string("CW")});
        if (added) {
            focus_editor("Items[" + std::to_string((*items_target_).items.read().size() - 1U) + "]");
        }
        (*status_).text = added ? "PropertyGrid · inserted CW through ComboBox.Items setter"
                                  : "PropertyGrid · Items insertion rejected truthfully";
    }

    void focus_editor(const std::string& property_path) {
        if (Window* window = (*inspector_).attached_window()) {
            static_cast<void>((*window).request_focus((*inspector_).editor(property_path)));
        }
    }

    void numeric_changed(const int index, const double value) {
        (*status_).text = "Numeric " + std::to_string(index + 1) + " committed " +
                            std::to_string(value) +
                            " · event emitted after value and editor synchronization";
    }

    void domain_changed(const std::optional<std::size_t> index) {
        (*status_).text = index ? "Domain selected " + (*domain_).items[*index]
                                  : std::string("Domain text matches no item");
    }

    void property_changed(const PropertyGridValueChange& change) {
        (*status_).text = "PropertyGrid committed " + change.property_name + " · origin " +
                            std::string(property_value_origin_name(change.origin)) +
                            (change.reset ? " · reset" : "");
    }

    Command inspect_numeric_{};
    Command inspect_items_{};
    Command inspect_style_{};
    Command add_item_{};
    std::vector<std::shared_ptr<NumericUpDown>> numerics_;
    std::shared_ptr<DomainUpDown> domain_;
    std::shared_ptr<PropertyGrid> inspector_;
    std::shared_ptr<ComboBox> items_target_;
    std::shared_ptr<CommandBar> commands_;
    std::shared_ptr<Label> status_;
    // Declared after the Commands its bar borrows, so it is released first.
    ValuesDocument<ValuesPage> document_{};
};

} // namespace

void add_values(const std::shared_ptr<Surface>& page,
                const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<ValuesPage>(page, context));
}

} // namespace gui_forms::gallery::pages
