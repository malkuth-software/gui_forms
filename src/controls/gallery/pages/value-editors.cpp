#include "pages.hpp"

namespace gui_forms::gallery::pages {

// One fixture record (a signal-lamp profile) is edited through four views:
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
        (*body).add_at(label("gallery.value-editors.heading", "VALUE EDITORS", 22.0, 700,
                             shell_blue_dark), {24.0, 18.0, 620.0, 34.0});
        (*body).add_at(label("gallery.value-editors.lead",
                             "One record, four views: typed color and flags editors, a grouped PropertyList, and an InstrumentRack module, all kept equal by the owner.",
                             12.0, 400, muted), {26.0, 52.0, 920.0, 24.0});
        build_editors(body);
        build_binding(body);
        build_properties(body);
        build_rack(body);
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

    [[nodiscard]] static PropertyEnumDescriptor edges_descriptor() {
        return {"Edges",
                {{"None", 0}, {"Top", 1}, {"Left", 2}, {"Right", 4}, {"Bottom", 8}},
                true};
    }

    void build_editors(const std::shared_ptr<Surface>& body) {
        std::shared_ptr<LayoutGroup> editors = group(
            "gallery.value-editors.editors",
            "ColorValueEditor · FlagsValueEditor · typed fields of the record", {450.0, 170.0});
        (*body).add_at(editors, {24.0, 92.0, 450.0, 170.0});
        (*editors).add_at(label("gallery.value-editors.color.caption", "Tint", 10.0, 700,
                                shell_blue_dark), {20.0, 42.0, 60.0, 22.0});
        color_ = make_control<ColorValueEditor>(StableId("gallery.value-editors.color"),
                                                fixture_tint);
        (*color_).accessible_name = "Lamp tint";
        (*editors).add_at(color_, {84.0, 37.0, 236.0, 30.0});
        preview_ = make_control<Panel>(StableId("gallery.value-editors.preview"));
        (*preview_).set_background(fixture_tint);
        (*preview_).accessible_name = "Tint preview";
        (*editors).add_at(preview_, {336.0, 37.0, 92.0, 30.0});

        (*editors).add_at(label("gallery.value-editors.flags.caption", "Edges", 10.0, 700,
                                shell_blue_dark), {20.0, 96.0, 60.0, 22.0});
        flags_ = make_control<FlagsValueEditor>(StableId("gallery.value-editors.flags"),
                                                edges_descriptor(),
                                                PropertyEnumValue{"Edges", "Top, Left", 3});
        (*flags_).accessible_name = "Lamp edges";
        (*flags_).set_popup_width(236.0);
        (*editors).add_at(flags_, {84.0, 91.0, 236.0, 30.0});
        editor_status_ = label("gallery.value-editors.editor-status",
                               "Enter commits · Esc restores · F4 drops the flags list down",
                               10.0, 600, green);
        (*editor_status_).set_text_wrapping(TextWrapping::word);
        (*editor_status_).set_vertical_alignment(VerticalAlignment::near);
        (*editors).add_at(editor_status_, {20.0, 130.0, 410.0, 34.0});
    }

    void build_binding(const std::shared_ptr<Surface>& body) {
        std::shared_ptr<LayoutGroup> binding = group(
            "gallery.value-editors.binding",
            "BindingContext · Window-scoped membership of one BindingSource", {450.0, 142.0});
        (*body).add_at(binding, {24.0, 280.0, 450.0, 142.0});
        register_.set_text("Register source");
        register_.set_description("Add the record's BindingSource to the Window's BindingContext");
        release_.set_text("Release source");
        release_.set_description("Remove the BindingSource from the BindingContext");
        binding_commands_ = make_control<CommandBar>(
            StableId("gallery.value-editors.binding-commands"));
        (*binding_commands_).accessible_name = "Binding context commands";
        (*binding_commands_).set_items({
            {.id = command_register, .label = "Register source",
             .accessible_name = "Register source", .command = &register_},
            {.id = command_release, .label = "Release source",
             .accessible_name = "Release source", .command = &release_}});
        (*binding).add_at(binding_commands_, {20.0, 38.0, 410.0, 32.0});
        binding_status_ = label("gallery.value-editors.binding-status", "", 10.0, 600, green);
        (*binding_status_).set_text_wrapping(TextWrapping::word);
        (*binding_status_).set_vertical_alignment(VerticalAlignment::near);
        (*binding).add_at(binding_status_, {20.0, 84.0, 410.0, 48.0});
    }

    void build_properties(const std::shared_ptr<Surface>& body) {
        std::shared_ptr<LayoutGroup> record = group(
            "gallery.value-editors.record", "PropertyList · the record as grouped rows",
            {460.0, 330.0});
        (*body).add_at(record, {494.0, 92.0, 460.0, 330.0});
        properties_ = make_control<PropertyList>(StableId("gallery.value-editors.properties"));
        (*properties_).accessible_name = "Lamp profile properties";
        (*properties_).set_label_width(96.0);
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
        (*record).add_at(properties_, {20.0, 36.0, 420.0, 274.0});
        if (const std::shared_ptr<CheckBox> enabled =
                std::dynamic_pointer_cast<CheckBox>((*properties_).editor(enabled_row))) {
            (*enabled).bind(enabled_);
        }
    }

    void build_rack(const std::shared_ptr<Surface>& body) {
        std::shared_ptr<LayoutGroup> rack_group = group(
            "gallery.value-editors.rack-group",
            "InstrumentRack · the same record as one compact module", {930.0, 190.0});
        (*body).add_at(rack_group, {24.0, 440.0, 930.0, 190.0});
        rack_ = make_control<InstrumentRack>(StableId("gallery.value-editors.rack"));
        (*rack_).accessible_name = "Lamp profile rack";
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
        (*rack_group).add_at(rack_, {20.0, 36.0, 890.0, 140.0});
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
};

void add_value_editors(const std::shared_ptr<Surface>& page,
                       const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<ValueEditorsPage>(page));
}

}  // namespace gui_forms::gallery::pages
