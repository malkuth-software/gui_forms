#include "pages.hpp"

#include "value-editors_form.hpp"

#include <array>
#include <vector>

namespace gui_forms::gallery::pages {
namespace {

// The controls demo/pages/value-editors.form describes, and their
// behaviour. One fixture record (a signal-lamp profile) is edited through four views:
// a ColorValueEditor and a FlagsValueEditor for its typed fields, a
// PropertyList of grouped rows, and an InstrumentRack module. The owner
// holds the record; each view reports a user commit through a named member
// and the owner projects the change into the other views with programmatic
// setters, which emit no user commits. The record's enabled flag is a
// Value<bool> that the PropertyList's own CheckBox binds. A BindingContext
// is registered on demand, because it needs the attached Window.
class ValueEditorsPage final : public gui_forms::Component {
public:
    explicit ValueEditorsPage(const std::shared_ptr<Surface>& body) {
        document_.initialize_component(*this);
        mount_document(body);
        build_editors();
        build_binding();
        build_properties();
        build_rack();
        on((*color_).value_changed(), *this, &ValueEditorsPage::color_changed);
        on((*color_).edit_failed(), *this, &ValueEditorsPage::color_failed);
        on((*flags_).value_changed(), *this, &ValueEditorsPage::flags_changed);
        on((*flags_).drop_down_changed(), *this, &ValueEditorsPage::flags_drop_down);
        on((*properties_).value_committed(), *this, &ValueEditorsPage::property_committed);
        on((*properties_).reset_requested(), *this, &ValueEditorsPage::property_reset);
        on((*rack_).field_committed(), *this, &ValueEditorsPage::field_committed);
        on((*rack_).module_toggled(), *this, &ValueEditorsPage::module_toggled);
        on(enabled_.changed(), *this, &ValueEditorsPage::enabled_changed);
        on(register_.invoked(), *this, &ValueEditorsPage::register_source);
        on(release_.invoked(), *this, &ValueEditorsPage::release_source);
        on(reset_.invoked(), *this, &ValueEditorsPage::reset_record);
        project_binding();
    }

private:
    static constexpr std::string_view fixture_name = "Signal lamp";
    static constexpr std::string_view fixture_channel = "FM";
    static constexpr Color fixture_tint = Color::rgba(39, 116, 184, 255);
    static constexpr std::string_view module_id = "gallery.value-editors.rack.lamp";
    static constexpr std::string_view name_row = "gallery.value-editors.properties.name";
    static constexpr std::string_view channel_row = "gallery.value-editors.properties.channel";
    static constexpr std::string_view enabled_row = "gallery.value-editors.properties.enabled";
    static constexpr std::string_view tint_row = "gallery.value-editors.properties.tint";
    static constexpr std::string_view edges_row = "gallery.value-editors.properties.edges";
    static constexpr int command_register = 1;
    static constexpr int command_release = 2;

    void mount_document(const std::shared_ptr<Surface>& body) {
        // The document's root is a declared boundary only: its children move
        // into the shell's page body, where each is placed by its bounds in
        // design coordinates. The copy of the child list keeps the iteration
        // valid while the root shrinks.
        Control& root = *document_.gallery_value_editors_document;
        const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
        for (const Control::Ptr& child : children) {
            Control::Ptr moved = root.remove_child((*child).runtime_id());
            (*body).add_child(std::move(moved));
        }
        const std::array<std::shared_ptr<ScaledGroupBox>, 4> groups{
            document_.gallery_value_editors_editors, document_.gallery_value_editors_binding,
            document_.gallery_value_editors_record, document_.gallery_value_editors_rack_group};
        for (const std::shared_ptr<ScaledGroupBox>& group : groups) {
            (*group).set_style(page_style());
        }
        color_ = document_.gallery_value_editors_color;
        preview_ = document_.gallery_value_editors_preview;
        flags_ = document_.gallery_value_editors_flags;
        editor_status_ = document_.gallery_value_editors_editor_status;
        binding_commands_ = document_.gallery_value_editors_binding_commands;
        binding_status_ = document_.gallery_value_editors_binding_status;
        properties_ = document_.gallery_value_editors_properties;
        rack_ = document_.gallery_value_editors_rack;
    }

    void build_editors() {
        (*color_).set_value(fixture_tint);
        (*preview_).set_background(fixture_tint);
        (*flags_).set_overlay_width(236.0);
    }

    void build_binding() {
        register_.set_text("Register source");
        register_.set_description("Add the record's BindingSource to the Window's BindingContext");
        release_.set_text("Release source");
        release_.set_description("Remove the BindingSource from the BindingContext");
        (*binding_commands_).set_items({
            {.id = command_register, .label = "Register source",
             .accessible_name = "Register source", .command = &register_},
            {.id = command_release, .label = "Release source",
             .accessible_name = "Release source", .command = &release_}});
    }

    void build_properties() {
        (*properties_).label_width = 96.0;
        (*properties_).set_groups({
            {"gallery.value-editors.properties.identity", "IDENTITY", {
                {std::string(name_row), "Name", std::string(fixture_name),
                 "Profile name · Enter commits · Reset restores the fixture",
                 PropertyEditorKind::text, {}, {}, true, true, {}, 0, false, false, true, false},
                {std::string(channel_row), "Channel", std::string(fixture_channel),
                 "Demodulation mode", PropertyEditorKind::choice, {"AM", "FM", "WFM"}},
                {std::string(enabled_row), "Enabled", "True",
                 "Bound Value<bool>: also the rack module's enable switch",
                 PropertyEditorKind::boolean},
            }},
            {"gallery.value-editors.properties.appearance", "APPEARANCE", {
                {std::string(tint_row), "Tint", ColorValueEditor::format_value(fixture_tint),
                 "Projected from the ColorValueEditor"},
                {std::string(edges_row), "Edges", "Top, Left",
                 "Projected from the FlagsValueEditor"},
            }},
        });
        if (const std::shared_ptr<CheckBox> enabled =
                std::dynamic_pointer_cast<CheckBox>((*properties_).editor(enabled_row))) {
            (*enabled).bind(enabled_);
        }
    }

    void build_rack() {
        (*rack_).set_module_width(640.0);
        (*rack_).set_modules({{
            std::string(module_id), "Lamp profile",
            {{"name", "Name", std::string(fixture_name), InstrumentFieldEditor::text, {}, {},
              1.4, true},
             {"channel", "Channel", std::string(fixture_channel),
              InstrumentFieldEditor::choice, {"AM", "FM", "WFM"}, {}, 0.8, false},
             {"tint", "Tint", ColorValueEditor::format_value(fixture_tint),
              InstrumentFieldEditor::text, {}, {}, 1.0, true}},
            "live · same record", InstrumentModuleState::live, 0, true, false}});
        reset_.set_text("Reset record");
        reset_.set_description("Restore the fixture values in every view");
        std::shared_ptr<Button> reset = make_control<Button>(
            StableId("gallery.value-editors.reset"), "Reset record");
        (*reset).set_visual_style(ButtonVisualStyle::command);
        (*reset).bind(reset_);
        (*rack_).set_action_content(reset, 170.0);
    }

    // ColorValueEditor commit: project the typed color into the list row, the
    // rack field and the preview; clear any stale rack validation.
    void color_changed(const Color value) {
        tint_ = value;
        const std::string text = ColorValueEditor::format_value(value);
        static_cast<void>((*properties_).set_value(tint_row, text));
        static_cast<void>((*rack_).set_field_value(module_id, "tint", text));
        static_cast<void>((*rack_).set_field_validation(module_id, "tint", {}));
        (*preview_).set_background(value);
        (*editor_status_).set_text("Tint committed " + text + " · projected to list, rack and preview");
    }

    void color_failed(const PropertyEditorInputError& error) {
        (*editor_status_).set_text("Tint rejected \"" + error.attempted_value + "\" · " +
                                   error.message);
    }

    void flags_changed(const PropertyEnumValue& value) {
        edges_ = value;
        static_cast<void>((*properties_).set_value(edges_row, value.name));
        (*editor_status_).set_text("Edges committed " + value.name + " (" +
                                   std::to_string(value.value) + ") · projected to the list");
    }

    void flags_drop_down(const bool expanded) {
        if (expanded) {
            (*editor_status_).set_text("Edges list open · Space toggles a bit · Esc closes");
        }
    }

    void property_committed(const PropertyValueChange& change) {
        if (change.row_id == name_row) {
            name_ = change.current_value;
            static_cast<void>((*rack_).set_field_value(module_id, "name", name_));
            static_cast<void>((*properties_).set_reset_enabled(name_row, name_ != fixture_name));
        } else if (change.row_id == channel_row) {
            channel_ = change.current_value;
            static_cast<void>((*rack_).set_field_value(module_id, "channel", channel_));
        } else if (change.row_id == enabled_row) {
            enabled_.set(change.current_value == "True");
        }
    }

    void property_reset(const PropertyResetRequest& request) {
        if (request.row_id != name_row) return;
        name_ = std::string(fixture_name);
        static_cast<void>((*properties_).set_value(name_row, name_));
        static_cast<void>((*properties_).set_reset_enabled(name_row, false));
        static_cast<void>((*rack_).set_field_value(module_id, "name", name_));
    }

    // Rack commit: the tint field re-enters through the ColorValueEditor's
    // parser and the owner's one projection serves every view.
    void field_committed(const InstrumentFieldChange& change) {
        if (change.field_id == "name") {
            name_ = change.current_value;
            static_cast<void>((*properties_).set_value(name_row, name_));
            static_cast<void>((*properties_).set_reset_enabled(name_row, name_ != fixture_name));
        } else if (change.field_id == "channel") {
            channel_ = change.current_value;
            static_cast<void>((*properties_).set_value(channel_row, channel_));
        } else if (change.field_id == "tint") {
            const std::optional<Color> parsed = ColorValueEditor::parse_value(change.current_value);
            if (parsed) {
                // A programmatic set_value publishes no value_changed (only a
                // user commit does), so the owner projects the colour itself.
                (*color_).set_value(*parsed);
                color_changed(*parsed);
            } else {
                static_cast<void>((*rack_).set_field_validation(
                    module_id, "tint", "Use #RRGGBB or #RRGGBBAA"));
            }
        }
    }

    void module_toggled(const InstrumentModuleToggle& toggle) { enabled_.set(toggle.enabled); }

    void enabled_changed(const bool enabled) {
        static_cast<void>((*rack_).set_module_enabled(module_id, enabled));
        static_cast<void>((*rack_).set_module_state(
            module_id, enabled ? InstrumentModuleState::live : InstrumentModuleState::staged,
            enabled ? "live · same record" : "staged · record disabled"));
    }

    void reset_record() {
        name_ = std::string(fixture_name);
        channel_ = std::string(fixture_channel);
        static_cast<void>((*properties_).set_value(name_row, name_));
        static_cast<void>((*properties_).set_value(channel_row, channel_));
        static_cast<void>((*properties_).set_reset_enabled(name_row, false));
        static_cast<void>((*rack_).set_field_value(module_id, "name", name_));
        static_cast<void>((*rack_).set_field_value(module_id, "channel", channel_));
        (*color_).set_value(fixture_tint);
        color_changed(fixture_tint);
        (*flags_).set_value(PropertyEnumValue{"Edges", "Top, Left", 3});
        flags_changed((*flags_).value());
        enabled_.set(true);
        (*editor_status_).set_text("Record reset · programmatic setters emitted no user commits");
    }

    void register_source() {
        Window* window = (*properties_).attached_window();
        if (window == nullptr) {
            (*binding_status_).set_text("BindingContext needs the attached Window");
            return;
        }
        if (!binding_context_) {
            binding_context_ = std::make_shared<BindingContext>(*window);
            on((*binding_context_).collection_changed(), *this,
               &ValueEditorsPage::context_changed);
        }
        if (!source_) {
            source_ = std::make_shared<BindingSource>(*window);
            (*source_).set_records({BindingRecord{
                "lamp.profile",
                {{"name", BindingValue{name_}}, {"channel", BindingValue{channel_}},
                 {"tint", BindingValue{tint_}}, {"enabled", BindingValue{enabled_.get()}}}}});
        }
        (*binding_context_).add(source_);
        project_binding();
    }

    void release_source() {
        if (binding_context_ && source_) static_cast<void>((*binding_context_).remove(*source_));
        project_binding();
    }

    void context_changed(const BindingContextChange& change) {
        trace_ = change.added ? " · last change: added" : " · last change: removed";
    }

    void project_binding() {
        const bool registered = binding_context_ && source_ && (*binding_context_).contains(*source_);
        register_.set_enabled(!registered);
        release_.set_enabled(registered);
        const std::size_t count = binding_context_ ? (*binding_context_).size() : 0U;
        std::string text = "BindingContext · " + std::to_string(count) +
                           (count == 1U ? " source" : " sources");
        if (registered) {
            const BindingSourceSnapshot snapshot = (*source_).snapshot();
            text += " · current " + snapshot.current_stable_id + " · position " +
                    std::to_string(snapshot.position);
        }
        (*binding_status_).set_text(text + trace_);
    }

    std::string name_{fixture_name};
    std::string channel_{fixture_channel};
    Color tint_{fixture_tint};
    PropertyEnumValue edges_{"Edges", "Top, Left", 3};
    Value<bool> enabled_{true};
    Command register_{};
    Command release_{};
    Command reset_{};
    std::shared_ptr<ColorValueEditor> color_;
    std::shared_ptr<FlagsValueEditor> flags_;
    std::shared_ptr<Panel> preview_;
    std::shared_ptr<Label> editor_status_;
    std::shared_ptr<CommandBar> binding_commands_;
    std::shared_ptr<Label> binding_status_;
    std::shared_ptr<PropertyList> properties_;
    std::shared_ptr<InstrumentRack> rack_;
    std::shared_ptr<BindingContext> binding_context_;
    std::shared_ptr<BindingSource> source_;
    std::string trace_;
    // Declared after the Value and Commands its controls bind, so it is
    // released first.
    ValueEditorsDocument<ValueEditorsPage> document_{};
};

} // namespace

void add_value_editors(const std::shared_ptr<Surface>& page,
                       const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<ValueEditorsPage>(page));
}

}  // namespace gui_forms::gallery::pages
