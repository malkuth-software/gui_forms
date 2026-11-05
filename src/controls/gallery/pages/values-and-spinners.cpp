#include "pages.hpp"

namespace gui_forms::gallery::pages {

// Owner of the NumericUpDown and DomainUpDown specimens and the PropertyGrid inspector. The
// four inspector actions are application Commands shown through one
// CommandBar; the grid's commits and each spinner's state changes report to
// the status label through named members.
class ValuesPage final : public gui_forms::Component {
public:
    ValuesPage(const std::shared_ptr<Surface>& body,
               const std::shared_ptr<PageContext>& context) {
        (*body).add_at(label("gallery.values.heading", "VALUES AND SPINNERS", 22.0, 700,
                             shell_blue_dark), {24.0, 18.0, 600.0, 34.0});
        (*body).add_at(label("gallery.values.lead",
                             "Compound TextBox editing, bounded numeric commit, decimal and hexadecimal formats, keys, wheel, and spinner buttons.",
                             12.0, 400, muted), {26.0, 52.0, 900.0, 24.0});
        std::shared_ptr<LayoutGroup> group_box = group(
            "gallery.values.numeric", "NumericUpDown · public compound control", {930.0, 360.0});
        (*body).add_at(group_box, {24.0, 92.0, 930.0, 360.0});
        build_numerics(group_box);
        build_domain(group_box);
        build_inspector(group_box);
        build_binding(body, context);
        on(inspect_numeric_.invoked(), *this, &ValuesPage::inspect_numeric);
        on(inspect_items_.invoked(), *this, &ValuesPage::inspect_items);
        on(inspect_style_.invoked(), *this, &ValuesPage::inspect_style);
        on(add_item_.invoked(), *this, &ValuesPage::add_item);
        on((*inspector_).property_value_changed(), *this, &ValuesPage::property_changed);
    }

private:
    static constexpr std::array<std::string_view, 4> captions{
        "Integer range", "Decimal increment", "Signed range", "Hexadecimal"};
    static constexpr int command_inspect_numeric = 1;
    static constexpr int command_inspect_items = 2;
    static constexpr int command_inspect_style = 3;
    static constexpr int command_add_item = 4;

    void build_numerics(const std::shared_ptr<LayoutGroup>& group_box) {
        for (std::size_t index = 0; index < captions.size(); ++index) {
            const double row = static_cast<double>(index) * 62.0;
            (*group_box).add_at(label("gallery.values.caption." + std::to_string(index),
                                      std::string(captions[index]), 11.0, 600, muted),
                                {24.0, 44.0 + row, 170.0, 26.0});
            std::shared_ptr<NumericUpDown> numeric = make_control<NumericUpDown>(
                StableId("gallery.values.numeric." + std::to_string(index)));
            (*numeric).accessible_name = std::string(captions[index]);
            if (index == 0U) {
                (*numeric).set_range(0.0, 100.0);
                (*numeric).set_value(42.0);
            } else if (index == 1U) {
                (*numeric).set_range(0.0, 10.0);
                (*numeric).set_increment(0.25);
                (*numeric).set_decimal_places(2U);
                (*numeric).set_value(3.75);
            } else if (index == 2U) {
                (*numeric).set_range(-120.0, 120.0);
                (*numeric).set_increment(5.0);
                (*numeric).set_value(-15.0);
            } else {
                (*numeric).set_range(0.0, 255.0);
                (*numeric).set_hexadecimal(true);
                (*numeric).set_value(175.0);
            }
            (*group_box).add_at(numeric, {194.0, 38.0 + row, 260.0, 36.0});
            on((*numeric).value_changed(), *this, &ValuesPage::numeric_changed,
               static_cast<int>(index));
            numerics_.push_back(std::move(numeric));
        }
    }

    // A DomainUpDown below the numeric rows: demodulation modes, wrapping.
    void build_domain(const std::shared_ptr<LayoutGroup>& group_box) {
        const double row = static_cast<double>(captions.size()) * 62.0;
        (*group_box).add_at(label("gallery.values.caption.domain", "Domain (wraps)", 11.0, 600, muted),
                            {24.0, 44.0 + row, 170.0, 26.0});
        domain_ = make_control<DomainUpDown>(StableId("gallery.values.domain"));
        (*domain_).accessible_name = "Demodulation mode";
        (*domain_).set_items({"AM", "FM", "WFM", "USB", "LSB", "CW"});
        (*domain_).set_wrap(true);
        (*domain_).set_selected_index(1U);
        (*group_box).add_at(domain_, {194.0, 38.0 + row, 260.0, 36.0});
        on((*domain_).selected_index_changed(), *this, &ValuesPage::domain_changed);
    }

    void build_inspector(const std::shared_ptr<LayoutGroup>& group_box) {
        inspector_ = make_control<PropertyGrid>(StableId("gallery.values.property-grid"));
        (*inspector_).accessible_name = "Decimal control property grid";
        (*inspector_).set_selected_object(numerics_[1]);
        static_cast<void>((*inspector_).set_property_expanded("bounds", true));
        items_target_ = make_control<ComboBox>(StableId("gallery.values.items-target"));
        (*items_target_).accessible_name = "Inspectable mode collection";
        (*items_target_).set_items({"AM", "FM", "WFM"});
        (*items_target_).set_selected_index(1U);
        (*group_box).add_at(items_target_, {486.0, 34.0, 174.0, 30.0});

        inspect_numeric_.set_text("Numeric");
        inspect_numeric_.set_description("Inspect the decimal NumericUpDown");
        inspect_items_.set_text("Items");
        inspect_items_.set_description("Inspect the ComboBox Items collection");
        inspect_style_.set_text("Style");
        inspect_style_.set_description("Inspect the status label's color and flags");
        add_item_.set_text("Add CW");
        add_item_.set_description("Insert CW through the ComboBox.Items setter");
        commands_ = make_control<CommandBar>(StableId("gallery.values.inspector-commands"));
        (*commands_).accessible_name = "Inspector commands";
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
            (*commands_).item(index).set_visual_style(ButtonVisualStyle::command);
        }
        (*group_box).add_at(commands_, {668.0, 34.0, 236.0, 30.0});
        (*group_box).add_at(inspector_, {486.0, 72.0, 420.0, 222.0});
        status_ = label("gallery.values.status",
                        "PropertyGrid uses instance-owned converter/editor services.",
                        10.5, 600, green);
        (*status_).set_text_wrapping(TextWrapping::word);
        (*status_).set_vertical_alignment(VerticalAlignment::near);
        (*group_box).add_at(status_, {494.0, 306.0, 404.0, 34.0});
    }

    // The BindingSource specimen is wired by the shell once a Window exists
    // (initialize_gallery_runtime); the page only places the controls.
    static void build_binding(const std::shared_ptr<Surface>& body,
                              const std::shared_ptr<PageContext>& context) {
        std::shared_ptr<LayoutGroup> binding = group(
            "gallery.values.binding",
            "BindingSource · currency, edit, conversion, two-way controls · BindingNavigator", {930.0, 196.0});
        (*body).add_at(binding, {24.0, 474.0, 930.0, 196.0});
        (*context).binding_editor = make_control<TextBox>(StableId("gallery.values.binding.name"));
        (*(*context).binding_editor).accessible_name = "Bound profile name";
        (*context).binding_enabled = make_control<CheckBox>(
            StableId("gallery.values.binding.enabled"), "Enabled");
        (*context).binding_gain = make_control<TrackBar>(StableId("gallery.values.binding.gain"));
        (*(*context).binding_gain).set_visual_style(TrackBarVisualStyle::filled);
        (*context).binding_previous = make_control<Button>(
            StableId("gallery.values.binding.previous"), "‹ Previous");
        (*context).binding_next = make_control<Button>(
            StableId("gallery.values.binding.next"), "Next ›");
        (*(*context).binding_previous).set_visual_style(ButtonVisualStyle::command);
        (*(*context).binding_next).set_visual_style(ButtonVisualStyle::command);
        (*context).binding_status = label(
            "gallery.values.binding.status",
            "Runtime binding initializes after the retained Window attaches", 10.0, 600, green);
        (*binding).add_at((*context).binding_editor, {20.0, 42.0, 190.0, 32.0});
        (*binding).add_at((*context).binding_enabled, {228.0, 44.0, 120.0, 28.0});
        (*binding).add_at((*context).binding_gain, {360.0, 42.0, 250.0, 32.0});
        (*binding).add_at((*context).binding_previous, {630.0, 42.0, 128.0, 32.0});
        (*binding).add_at((*context).binding_next, {772.0, 42.0, 128.0, 32.0});
        (*binding).add_at((*context).binding_status, {20.0, 94.0, 880.0, 26.0});
        // A BindingNavigator over the same source; the shell binds it.
        (*context).binding_navigator = make_control<BindingNavigator>(StableId("gallery.values.binding.navigator"));
        (*(*context).binding_navigator).accessible_name = "Profile navigator";
        (*binding).add_at((*context).binding_navigator, {20.0, 128.0, 520.0, 27.0});
    }

    void inspect_numeric() {
        (*inspector_).set_selected_object(numerics_[1]);
        static_cast<void>((*inspector_).set_property_expanded("bounds", true));
        (*status_).set_text("PropertyGrid · retained NumericUpDown factory and typed commit");
    }

    void inspect_items() {
        (*inspector_).set_selected_object(items_target_);
        static_cast<void>((*inspector_).set_property_expanded("items", true));
        focus_editor("Items[0]");
        (*status_).set_text("PropertyGrid · ComboBox.Items immutable collection expanded");
    }

    void inspect_style() {
        (*inspector_).set_selected_object(status_);
        static_cast<void>((*inspector_).set_property_expanded("fore-color", true));
        (*status_).set_text("PropertyGrid · flags popup and canonical color editor services");
    }

    void add_item() {
        if ((*inspector_).selected_object() != items_target_) {
            (*inspector_).set_selected_object(items_target_);
        }
        static_cast<void>((*inspector_).set_property_expanded("items", true));
        const bool added = (*inspector_).insert_collection_item(
            "items", (*items_target_).items().size(), BindingValue{std::string("CW")});
        if (added) {
            focus_editor("Items[" + std::to_string((*items_target_).items().size() - 1U) + "]");
        }
        (*status_).set_text(added ? "PropertyGrid · inserted CW through ComboBox.Items setter"
                                  : "PropertyGrid · Items insertion rejected truthfully");
    }

    void focus_editor(const std::string& property_path) {
        if (Window* window = (*inspector_).attached_window()) {
            static_cast<void>((*window).request_focus((*inspector_).editor(property_path)));
        }
    }

    void numeric_changed(const int index, const double value) {
        (*status_).set_text("Numeric " + std::to_string(index + 1) + " committed " +
                            std::to_string(value) +
                            " · event emitted after value and editor synchronization");
    }

    void domain_changed(const std::optional<std::size_t> index) {
        (*status_).set_text(index ? "Domain selected " + (*domain_).items()[*index]
                                  : std::string("Domain text matches no item"));
    }

    void property_changed(const PropertyGridValueChange& change) {
        (*status_).set_text("PropertyGrid committed " + change.property_name + " · origin " +
                            std::string(property_value_origin_name(change.origin)) +
                            (change.reset ? " · reset" : ""));
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
};

void add_values(const std::shared_ptr<Surface>& page,
                const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<ValuesPage>(page, context));
}

} // namespace gui_forms::gallery::pages
