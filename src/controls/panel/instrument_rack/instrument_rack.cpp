#include "gui_forms/controls/panel/instrument_rack/instrument_rack.hpp"

#include "rack_module_panel/rack_module_panel.hpp"

#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"
#include "../panel_anatomy.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace gui_forms {
namespace {

constexpr double default_module_width = 210.0;
constexpr double default_module_height = 66.0;
constexpr double default_gap = 5.0;
constexpr double compact_field_width = 190.0;

void require_instrument_text(std::string_view value, const char* field) {
    if (!validate_utf8(value).valid()) {
        throw std::invalid_argument(std::string("InstrumentRack ") + field +
                                    " must be valid UTF-8");
    }
}

void require_finite_positive(double value, const char* field) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument(std::string("InstrumentRack ") + field +
                                    " must be finite and positive");
    }
}

BasicControlStyle module_style(InstrumentModuleState state) {
    BasicControlStyle result;
    result.face = Color::rgba(226, 235, 241);
    result.face_light = Color::rgba(252, 254, 255);
    result.paper = Color::rgba(255, 255, 255);
    result.border = Color::rgba(105, 126, 142);
    result.dark_border = Color::rgba(72, 97, 116);
    result.text = Color::rgba(43, 65, 82);
    result.disabled_text = Color::rgba(111, 126, 137);
    result.accent = Color::rgba(61, 96, 138);
    result.accent_light = Color::rgba(219, 232, 242);
    // A live module keeps these colours.
    if (state == InstrumentModuleState::staged) {
        result.face = Color::rgba(238, 229, 241);
        result.border = Color::rgba(138, 106, 152);
        result.dark_border = Color::rgba(108, 79, 121);
        result.accent = Color::rgba(113, 78, 133);
        result.accent_light = Color::rgba(239, 226, 245);
    } else if (state == InstrumentModuleState::pending) {
        result.face = Color::rgba(225, 235, 247);
        result.border = Color::rgba(83, 116, 157);
        result.accent = Color::rgba(56, 91, 139);
    } else if (state == InstrumentModuleState::invalid) {
        result.face = Color::rgba(247, 231, 229);
        result.border = Color::rgba(157, 91, 86);
        result.dark_border = Color::rgba(121, 65, 62);
        result.accent = Color::rgba(139, 69, 66);
        result.accent_light = Color::rgba(250, 224, 222);
    }
    return result;
}

Control::Ptr first_focusable_descendant(const Control::Ptr& root) {
    if (!root) return {};
    if ((*root).focusable() && (*root).effectively_visible() &&
        (*root).effectively_enabled()) return root;
    for (const Control::Ptr& child : (*root).children()) {
        if (Control::Ptr result = first_focusable_descendant(child)) return result;
    }
    return {};
}

bool contains_control(const Control::Ptr& root, const Control::Ptr& candidate) {
    if (!root || !candidate) return false;
    if (root == candidate) return true;
    for (const Control::Ptr& child : (*root).children()) {
        if (contains_control(child, candidate)) return true;
    }
    return false;
}

[[nodiscard]] bool same_instrument_field_shape(
    const InstrumentFieldSpec& left,
    const InstrumentFieldSpec& right) noexcept {
    return left.stable_id == right.stable_id &&
           left.editor == right.editor;
}

} // namespace

using detail::RackModulePanel;

struct InstrumentRack::Impl final {
    struct FieldState final {
        Control::Ptr editor;
        SubscriptionToken changed;
        SubscriptionToken committed;
        SubscriptionToken cancelled;
        SubscriptionToken focused;
        std::string committed_value;
        InstrumentFieldEditor kind{InstrumentFieldEditor::choice};
    };

    struct ModuleState final {
        InstrumentModuleSpec spec;
        std::shared_ptr<RackModulePanel> panel;
        std::shared_ptr<CheckBox> enable;
        std::vector<FieldState> fields;
        std::shared_ptr<Label> status;
        std::shared_ptr<Button> remove;
        SubscriptionToken toggled;
        SubscriptionToken remove_clicked;
        SubscriptionToken enable_focused;
        SubscriptionToken remove_focused;
        Rect bounds{};
    };
    using ModuleList = std::vector<ModuleState>;

    struct EnsureModuleVisible final {
        Impl* implementation{};
        std::string module_id;
        void operator()(bool focused) const {
            if (focused) (*implementation).ensure_visible(module_id);
        }
    };

    struct ChoiceFieldChanged final {
        Impl* implementation{};
        std::string module_id;
        std::string field_id;
        std::weak_ptr<ComboBox> control;
        void operator()(std::optional<std::size_t>) const {
            (*implementation).choice_field_changed(
                module_id, field_id, control);
        }
    };

    struct TextFieldChanged final {
        Impl* implementation{};
        std::string module_id;
        std::string field_id;
        void operator()(const std::string& value) const {
            (*implementation).text_field_changed(module_id, field_id, value);
        }
    };

    struct TextFieldCommitted final {
        Impl* implementation{};
        std::string module_id;
        std::string field_id;
        void operator()(const std::string& value) const {
            (*implementation).text_field_committed(module_id, field_id, value);
        }
    };

    struct TextFieldCancelled final {
        Impl* implementation{};
        std::string module_id;
        std::string field_id;
        std::weak_ptr<TextBox> control;
        void operator()() const {
            (*implementation).text_field_cancelled(
                module_id, field_id, control);
        }
    };

    struct MoveRequest final {
        Impl* implementation{};
        std::string module_id;
        void operator()(bool forward) const {
            (*implementation).emit_move(module_id, forward);
        }
    };

    struct ModuleToggled final {
        Impl* implementation{};
        std::string module_id;
        void operator()(bool enabled) const {
            (*implementation).module_toggled(module_id, enabled);
        }
    };

    struct RemoveClicked final {
        Impl* implementation{};
        std::string module_id;
        void operator()(ButtonBase&) const {
            (*implementation).owner.remove_requested_.emit({module_id});
        }
    };

    explicit Impl(InstrumentRack& public_owner) : owner(public_owner) {}

    ModuleState* find_module(std::string_view id) noexcept {
        for (ModuleState& state : states) {
            if (state.spec.stable_id == id) return &state;
        }
        return nullptr;
    }
    const ModuleState* find_module(std::string_view id) const noexcept {
        for (const ModuleState& state : states) {
            if (state.spec.stable_id == id) return &state;
        }
        return nullptr;
    }
    std::optional<std::size_t> module_index(std::string_view id) const noexcept {
        for (std::size_t index = 0U; index < states.size(); ++index) {
            if (states[index].spec.stable_id == id) return index;
        }
        return {};
    }

    InstrumentFieldSpec* find_field(ModuleState& module,
                                    std::string_view id) noexcept {
        for (InstrumentFieldSpec& field : module.spec.fields) {
            if (field.stable_id == id) return &field;
        }
        return nullptr;
    }

    void emit_move(std::string_view module_id, bool forward) {
        const std::optional<std::size_t> index = module_index(module_id);
        if (!index) return;
        const std::size_t requested = forward
            ? std::min(*index + 1U, states.size() - 1U)
            : (*index == 0U ? 0U : *index - 1U);
        if (requested == *index) return;
        owner.move_requested_.emit(
            {std::string(module_id), *index, requested});
    }

    void ensure_visible(std::string_view module_id) {
        ModuleState* module = find_module(module_id);
        if (!module) return;
        const double viewport = owner.committed_arranged_bounds().height;
        if ((*module).bounds.y < scroll_offset) {
            scroll_offset = (*module).bounds.y;
        } else if ((*module).bounds.y + (*module).bounds.height >
                   scroll_offset + viewport) {
            scroll_offset = (*module).bounds.y + (*module).bounds.height - viewport;
        }
        clamp_scroll();
        arrange_children();
        owner.invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
    }

    void connect_focus(ModuleState& module, Control::Ptr control,
                       SubscriptionToken& token) {
        const std::string module_id = module.spec.stable_id;
        token = (*control).focus_observed().subscribe(
            owner, EnsureModuleVisible{this, module_id});
    }

    void choice_field_changed(
        std::string_view module_id, std::string_view field_id,
        const std::weak_ptr<ComboBox>& weak_control) {
        if (synchronizing) return;
        ModuleState* module = find_module(module_id);
        const std::shared_ptr<ComboBox> control = weak_control.lock();
        if (module == nullptr || !control) return;
        InstrumentFieldSpec* field = find_field(*module, field_id);
        if (field == nullptr) return;
        FieldState& state = (*module).fields[static_cast<std::size_t>(
            field - (*module).spec.fields.data())];
        const std::string previous = state.committed_value;
        (*field).value = std::string((*control).selected_text());
        state.committed_value = (*field).value;
        for (InstrumentModuleSpec& public_module : public_modules) {
            if (public_module.stable_id != module_id) continue;
            for (InstrumentFieldSpec& public_field : public_module.fields) {
                if (public_field.stable_id == field_id) {
                    public_field.value = (*field).value;
                }
            }
        }
        const InstrumentFieldChange change{
            std::string(module_id), std::string(field_id), previous,
            (*field).value, true};
        owner.publish_change(owner.field_changed_, change);
        owner.field_committed_.emit(change);
    }

    void text_field_changed(std::string_view module_id,
                            std::string_view field_id,
                            const std::string& value) {
        if (synchronizing) return;
        ModuleState* module = find_module(module_id);
        if (module == nullptr) return;
        InstrumentFieldSpec* field = find_field(*module, field_id);
        if (field == nullptr) return;
        const std::string previous = (*field).value;
        (*field).value = value;
        for (InstrumentModuleSpec& public_module : public_modules) {
            if (public_module.stable_id != module_id) continue;
            for (InstrumentFieldSpec& public_field : public_module.fields) {
                if (public_field.stable_id == field_id) {
                    public_field.value = value;
                }
            }
        }
        owner.publish_change(owner.field_changed_, InstrumentFieldChange{
            std::string(module_id), std::string(field_id), previous,
            value, false});
    }

    void text_field_committed(std::string_view module_id,
                              std::string_view field_id,
                              const std::string& value) {
        ModuleState* module = find_module(module_id);
        if (module == nullptr) return;
        InstrumentFieldSpec* field = find_field(*module, field_id);
        if (field == nullptr) return;
        FieldState& state = (*module).fields[static_cast<std::size_t>(
            field - (*module).spec.fields.data())];
        const std::string previous = state.committed_value;
        state.committed_value = value;
        (*field).value = value;
        if ((*field).required && value.empty()) {
            (*field).validation_message = (*field).name + " is required";
            (*module).spec.state = InstrumentModuleState::invalid;
        }
        for (InstrumentModuleSpec& public_module : public_modules) {
            if (public_module.stable_id != module_id) continue;
            public_module.state = (*module).spec.state;
            for (InstrumentFieldSpec& public_field : public_module.fields) {
                if (public_field.stable_id == field_id) {
                    public_field.value = value;
                    public_field.validation_message =
                        (*field).validation_message;
                }
            }
        }
        update_module_presentation(*module);
        owner.field_committed_.emit({
            std::string(module_id), std::string(field_id), previous,
            value, true});
    }

    void text_field_cancelled(
        std::string_view module_id, std::string_view field_id,
        const std::weak_ptr<TextBox>& weak_control) {
        ModuleState* module = find_module(module_id);
        const std::shared_ptr<TextBox> control = weak_control.lock();
        if (module == nullptr || !control) return;
        InstrumentFieldSpec* field = find_field(*module, field_id);
        if (field == nullptr) return;
        FieldState& state = (*module).fields[static_cast<std::size_t>(
            field - (*module).spec.fields.data())];
        synchronizing = true;
        (*control).text = state.committed_value;
        (*field).value = state.committed_value;
        for (InstrumentModuleSpec& public_module : public_modules) {
            if (public_module.stable_id != module_id) continue;
            for (InstrumentFieldSpec& public_field : public_module.fields) {
                if (public_field.stable_id == field_id) {
                    public_field.value = state.committed_value;
                }
            }
        }
        synchronizing = false;
    }

    void module_toggled(std::string_view module_id, bool enabled) {
        if (synchronizing) return;
        ModuleState* module = find_module(module_id);
        if (module == nullptr) return;
        (*module).spec.enabled = enabled;
        update_module_presentation(*module);
        for (InstrumentModuleSpec& public_module : public_modules) {
            if (public_module.stable_id == module_id) {
                public_module.enabled = enabled;
            }
        }
        owner.module_toggled_.emit({std::string(module_id), enabled});
    }

    Control::Ptr create_field_editor(ModuleState& module,
                                     std::size_t field_index) {
        InstrumentFieldSpec& field = module.spec.fields[field_index];
        const std::string id = module.spec.stable_id + "." + field.stable_id;
        Control::Ptr editor;
        // The content face at 11 units, as other compact editors: the 9-unit
        // fields read at about 8 px (sweep defect D19).
        const FontSpec field_font{FontRole::content, 11.0, 400, false};
        if (field.editor == InstrumentFieldEditor::choice) {
            std::shared_ptr<gui_forms::ComboBox> choice = make_control<ComboBox>(StableId(id));
            (*choice).items = field.choices;
            const std::vector<std::string>::iterator selected =
                std::find(field.choices.begin(),
                                            field.choices.end(), field.value);
            if (selected != field.choices.end()) {
                (*choice).selected_index = static_cast<std::size_t>(
                    std::distance(field.choices.begin(), selected));
            }
            (*choice).font = field_font;
            editor = choice;
        } else {
            std::shared_ptr<gui_forms::TextBox> text = make_control<TextBox>(StableId(id), field.value);
            (*text).font = field_font;
            editor = text;
        }
        (*editor).margin = Insets{};
        (*editor).accessible_name = field.name;
        (*editor).accessible_description = field.validation_message.empty()
            ? module.spec.name + " criterion field"
            : field.validation_message;
        (*module.panel).add_child(editor);
        return editor;
    }

    void connect_field(ModuleState& module, std::size_t field_index) {
        FieldState& state = module.fields[field_index];
        const std::string module_id = module.spec.stable_id;
        const std::string field_id = module.spec.fields[field_index].stable_id;
        state.committed_value = module.spec.fields[field_index].value;
        state.kind = module.spec.fields[field_index].editor;
        if (const std::shared_ptr<gui_forms::ComboBox> choice = std::dynamic_pointer_cast<ComboBox>(state.editor)) {
            state.changed = (*choice).selected_index_changed().subscribe(
                owner, ChoiceFieldChanged{
                    this, module_id, field_id,
                    std::weak_ptr<ComboBox>(choice)});
        } else if (const std::shared_ptr<gui_forms::TextBox> text = std::dynamic_pointer_cast<TextBox>(state.editor)) {
            // Enter commits the field (TextFieldCommitted) and Escape reverts
            // it (TextFieldCancelled); neither goes further.
            (*text).accepts_enter = true;
            (*text).accepts_escape = true;
            state.changed = (*text).text_changed().subscribe(
                owner, TextFieldChanged{this, module_id, field_id});
            state.committed = (*text).committed().subscribe(
                owner, TextFieldCommitted{this, module_id, field_id});
            state.cancelled = (*text).cancelled().subscribe(
                owner, TextFieldCancelled{
                    this, module_id, field_id,
                    std::weak_ptr<TextBox>(text)});
        }
        connect_focus(module, state.editor, state.focused);
    }

    ModuleState make_module(InstrumentModuleSpec spec) {
        ModuleState module;
        module.spec = std::move(spec);
        module.panel = make_control<RackModulePanel>(
            StableId(module.spec.stable_id));
        (*module.panel).accessible_name = module.spec.name;
        const std::string module_id = module.spec.stable_id;
        (*module.panel).move_request = MoveRequest{this, module_id};
        module.enable = make_control<CheckBox>(
            StableId(module.spec.stable_id + ".enable"));
        (*module.enable).accessible_name = "Enable " + module.spec.name;
        (*module.enable).margin = Insets{};
        (*module.panel).add_child(module.enable);
        for (std::size_t index = 0; index < module.spec.fields.size(); ++index) {
            FieldState field;
            field.editor = create_field_editor(module, index);
            module.fields.push_back(std::move(field));
        }
        module.status = make_control<Label>(
            StableId(module.spec.stable_id + ".state"),
            module.spec.status_text);
        (*module.status).font = FontSpec{FontRole::control, 9.5, 600, false, 0.30};
        (*module.status).fore_color = Color::rgba(92, 113, 128);
        (*module.status).margin = Insets{};
        (*module.status).accessible_name = "Application state";
        (*module.panel).add_child(module.status);
        module.remove = make_control<Button>(
            StableId(module.spec.stable_id + ".remove"), "×");
        (*module.remove).set_visual_style(ButtonVisualStyle::flat);
        (*module.remove).accessible_name = "Remove " + module.spec.name;
        (*module.remove).margin = Insets{};
        (*module.panel).add_child(module.remove);
        (*module.panel).enable = module.enable;
        (*module.panel).status = module.status;
        (*module.panel).remove = module.remove;
        for (std::size_t index = 0; index < module.fields.size(); ++index) {
            (*module.panel).fields.push_back(module.fields[index].editor);
            (*module.panel).field_weights.push_back(
                module.spec.fields[index].width_weight);
        }
        owner.add_child(module.panel);
        module.toggled = (*module.enable).checked_changed().subscribe(
            owner, ModuleToggled{this, module_id});
        module.remove_clicked = (*module.remove).clicked().subscribe(
            owner, RemoveClicked{this, module_id});
        connect_focus(module, module.enable, module.enable_focused);
        connect_focus(module, module.remove, module.remove_focused);
        for (std::size_t index = 0; index < module.fields.size(); ++index) {
            connect_field(module, index);
        }
        update_module_presentation(module);
        return module;
    }

    void update_module_presentation(ModuleState& module) {
        const BasicControlStyle style = module_style(module.spec.state);
        (*module.panel).set_style(style);
        (*module.panel).set_background(style.face);
        (*module.enable).set_style(style);
        synchronizing = true;
        (*module.enable).set_checked(module.spec.enabled);
        synchronizing = false;
        const bool pending = module.spec.state == InstrumentModuleState::pending;
        (*module.enable).enabled = !pending;
        (*module.remove).enabled = module.spec.removable && !pending;
        (*module.remove).visible = module.spec.removable;
        (*module.remove).set_style(style);
        (*module.status).text = module.spec.status_text;
        (*module.status).accessible_description = module.spec.status_text;
        (*module.status).fore_color =
            module.spec.state == InstrumentModuleState::invalid
                ? Color::rgba(130, 59, 57)
                : module.spec.state == InstrumentModuleState::staged
                    ? Color::rgba(121, 93, 131)
                    : Color::rgba(92, 113, 128);
        (*module.panel).accessible_description = module.spec.status_text +
            " · Alt+Left and Alt+Right request reordering";
        for (std::size_t index = 0; index < module.fields.size(); ++index) {
            const InstrumentFieldSpec& field = module.spec.fields[index];
            (*module.fields[index].editor).enabled = module.spec.enabled && !pending;
            (*module.fields[index].editor).accessible_description = field.validation_message.empty()
                    ? module.spec.name + " criterion field"
                    : field.validation_message;
            if (const std::shared_ptr<gui_forms::Panel> panel = std::dynamic_pointer_cast<Panel>(
                    module.fields[index].editor)) {
                (*panel).set_style(style);
            }
        }
        owner.invalidate(Dirty::paint | Dirty::semantics);
    }

    void synchronize_module(ModuleState& state,
                            InstrumentModuleSpec incoming) {
        const bool field_shape_matches = state.spec.fields.size() ==
                incoming.fields.size() &&
            std::equal(state.spec.fields.begin(), state.spec.fields.end(),
                       incoming.fields.begin(),
                       same_instrument_field_shape);
        if (!field_shape_matches) {
            const Rect old_bounds = state.bounds;
            Control::Ptr removed = owner.remove_child((*state.panel).runtime_id());
            if (removed && (*removed).is_alive()) (*removed).dispose();
            state = make_module(std::move(incoming));
            state.bounds = old_bounds;
            return;
        }
        state.spec = std::move(incoming);
        (*state.panel).accessible_name = state.spec.name;
        (*state.panel).field_weights.clear();
        synchronizing = true;
        for (std::size_t index = 0; index < state.fields.size(); ++index) {
            InstrumentFieldSpec& field = state.spec.fields[index];
            FieldState& field_state = state.fields[index];
            field_state.committed_value = field.value;
            (*field_state.editor).accessible_name = field.name;
            if (const std::shared_ptr<gui_forms::ComboBox> choice = std::dynamic_pointer_cast<ComboBox>(
                    field_state.editor)) {
                (*choice).items = field.choices;
                const std::vector<std::string>::iterator selected =
                    std::find(field.choices.begin(),
                                                field.choices.end(), field.value);
                (*choice).selected_index = selected == field.choices.end()
                    ? std::optional<std::size_t>{}
                    : std::optional<std::size_t>{static_cast<std::size_t>(
                        std::distance(field.choices.begin(), selected))};
            } else if (const std::shared_ptr<gui_forms::TextBox> text = std::dynamic_pointer_cast<TextBox>(
                           field_state.editor)) {
                (*text).text = field.value;
            }
            (*state.panel).field_weights.push_back(field.width_weight);
        }
        synchronizing = false;
        update_module_presentation(state);
    }

    void remove_state(ModuleState& state) noexcept {
        if (!state.panel || (*state.panel).parent().get() != &owner) return;
        Control::Ptr removed = owner.remove_child((*state.panel).runtime_id());
        if (removed && (*removed).is_alive()) (*removed).dispose();
    }

    [[nodiscard]] double scale() const noexcept {
        return owner.effective_text_scale();
    }

    struct Slot final {
        ModuleState* module{};
        Control::Ptr action;
        Rect bounds{};
    };

    struct LineCursor final {
        double x{};
        double y{};
        double height{};
        double gap{};

        void new_line() noexcept {
            x = 0.0;
            y += height + (height > 0.0 ? gap : 0.0);
            height = 0.0;
        }
    };

    std::vector<Slot> compute_slots(double available_width) const {
        const double s = scale();
        const Insets inset = owner.padding.read();
        const double width = std::max(
            0.0, available_width - inset.left - inset.right);
        const double gap = rack_gap * s;
        const double preferred_module = module_width * s;
        const double action_min = std::min(action_minimum_width * s,
                                           std::max(1.0, width));
        std::vector<Slot> slots;
        LineCursor line{0.0, 0.0, 0.0, gap};
        for (const ModuleState& const_state : states) {
            ModuleState& state = const_cast<ModuleState&>(const_state);
            const double slot_width = std::min(preferred_module,
                                               std::max(1.0, width));
            const bool compact = slot_width < compact_field_width * s;
            const double slot_height = compact
                ? std::max(module_height * s,
                           (static_cast<double>(state.spec.fields.size()) * 28.0 +
                            22.0) * s)
                : module_height * s;
            if (line.x > 0.0 &&
                line.x + slot_width > width + 0.001) {
                line.new_line();
            }
            slots.push_back({&state, {},
                {inset.left + line.x, inset.top + line.y,
                 slot_width, slot_height}});
            line.x += slot_width + gap;
            line.height = std::max(line.height, slot_height);
        }
        if (action_content) {
            if (line.x > 0.0 &&
                line.x + action_min > width + 0.001) {
                line.new_line();
            }
            const double remaining = std::max(action_min,
                width - (line.x > 0.0 ? line.x : 0.0));
            const double action_width = std::min(width, remaining);
            const double action_height = module_height * s;
            slots.push_back({nullptr, action_content,
                {inset.left + line.x, inset.top + line.y,
                 action_width, action_height}});
            line.height = std::max(line.height, action_height);
        }
        computed_content_height =
            inset.top + line.y + line.height + inset.bottom;
        return slots;
    }

    void clamp_scroll() {
        scroll_offset = std::clamp(scroll_offset, 0.0,
            std::max(0.0, computed_content_height -
                              owner.committed_arranged_bounds().height));
    }

    void arrange_children() {
        const double width = owner.committed_arranged_bounds().width;
        const std::vector<Slot> slots = compute_slots(width);
        clamp_scroll();
        for (const Slot& slot : slots) {
            Rect bounds = slot.bounds;
            bounds.y -= scroll_offset;
            if (slot.module) {
                (*slot.module).bounds = slot.bounds;
                (*(*slot.module).panel).set_compact(
                    slot.bounds.width < compact_field_width * scale());
                owner.set_child_layout((*slot.module).panel, bounds);
            } else if (slot.action) {
                owner.set_child_layout(slot.action, bounds);
            }
        }
    }

    InstrumentRack& owner;
    ModuleList states;
    std::vector<InstrumentModuleSpec> public_modules;
    Control::Ptr action_content;
    double action_minimum_width{170.0};
    double module_width{default_module_width};
    double module_height{default_module_height};
    double rack_gap{default_gap};
    mutable double computed_content_height{};
    double scroll_offset{};
    bool synchronizing{};
};

InstrumentRack::InstrumentRack(StableId stable_id)
    : Panel(std::move(stable_id)), impl_(std::make_unique<Impl>(*this)) {
    set_background(Color::rgba(242, 247, 250));
    border_style = BorderStyle::none;
    padding = Insets{};
    accessible_name = "Editing instrument rack";
}

InstrumentRack::~InstrumentRack() = default;

const std::vector<InstrumentModuleSpec>& InstrumentRack::modules() const noexcept {
    return (*impl_).public_modules;
}

void InstrumentRack::set_modules(std::vector<InstrumentModuleSpec> modules) {
    require_mutable();
    std::unordered_set<std::string> module_ids;
    for (const InstrumentModuleSpec& module : modules) {
        require_instrument_text(module.stable_id, "module ID");
        require_instrument_text(module.name, "module name");
        require_instrument_text(module.status_text, "module status");
        if (module.stable_id.empty() || module.name.empty() ||
            !module_ids.insert(module.stable_id).second) {
            throw std::invalid_argument(
                "InstrumentRack modules require unique nonempty identities and names");
        }
        if (module.fields.empty() || module.fields.size() > 8U) {
            throw std::invalid_argument(
                "InstrumentRack modules require one through eight fields");
        }
        std::unordered_set<std::string> field_ids;
        for (const InstrumentFieldSpec& field : module.fields) {
            require_instrument_text(field.stable_id, "field ID");
            require_instrument_text(field.name, "field name");
            require_instrument_text(field.value, "field value");
            require_instrument_text(field.validation_message,
                                    "field validation message");
            require_finite_positive(field.width_weight, "field weight");
            if (field.stable_id.empty() || field.name.empty() ||
                !field_ids.insert(field.stable_id).second) {
                throw std::invalid_argument(
                    "InstrumentRack fields require unique nonempty identities and names");
            }
            if (field.editor == InstrumentFieldEditor::choice &&
                field.choices.empty()) {
                throw std::invalid_argument(
                    "InstrumentRack choice fields require at least one choice");
            }
            for (const std::string& choice : field.choices) {
                require_instrument_text(choice, "choice");
            }
        }
    }

    Control::Ptr focused;
    std::optional<std::size_t> removed_focus_index;
    if (Window* window = attached_window()) focused = (*window).focused_control();
    if (focused) {
        for (std::size_t index = 0; index < (*impl_).states.size(); ++index) {
            bool survives = false;
            for (const InstrumentModuleSpec& incoming : modules) {
                if (incoming.stable_id ==
                    (*impl_).states[index].spec.stable_id) {
                    survives = true;
                    break;
                }
            }
            if (!survives && contains_control((*impl_).states[index].panel, focused)) {
                removed_focus_index = index;
                break;
            }
        }
    }

    std::vector<Impl::ModuleState> next;
    next.reserve(modules.size());
    for (InstrumentModuleSpec& module : modules) {
        Impl::ModuleList::iterator found = (*impl_).states.begin();
        while (found != (*impl_).states.end() &&
               (*found).spec.stable_id != module.stable_id) {
            ++found;
        }
        if (found == (*impl_).states.end()) {
            next.push_back((*impl_).make_module(std::move(module)));
        } else {
            Impl::ModuleState state = std::move(*found);
            (*found).panel.reset();
            (*impl_).synchronize_module(state, std::move(module));
            next.push_back(std::move(state));
        }
    }
    for (Impl::ModuleState& state : (*impl_).states) {
        if (state.panel) (*impl_).remove_state(state);
    }
    (*impl_).states = std::move(next);
    (*impl_).public_modules.clear();
    (*impl_).public_modules.reserve((*impl_).states.size());
    for (std::size_t index = 0; index < (*impl_).states.size(); ++index) {
        (*impl_).public_modules.push_back((*impl_).states[index].spec);
        set_child_index((*(*impl_).states[index].panel).runtime_id(), index);
    }
    if ((*impl_).action_content) {
        set_child_index((*(*impl_).action_content).runtime_id(), (*impl_).states.size());
    }
    (*impl_).scroll_offset = 0.0;
    invalidate(Dirty::measure | Dirty::layout | Dirty::paint | Dirty::hit_test |
               Dirty::semantics);
    if (removed_focus_index && attached_window()) {
        Control::Ptr destination;
        if (!(*impl_).states.empty()) {
            destination = first_focusable_descendant(
                (*impl_).states[std::min(*removed_focus_index,
                                       (*impl_).states.size() - 1U)].panel);
        }
        if (!destination) destination = first_focusable_descendant(
            (*impl_).action_content);
        if (destination) static_cast<void>((*attached_window()).request_focus(destination));
    }
}

Control::Ptr InstrumentRack::field_editor(std::string_view module_id,
                                          std::string_view field_id) const {
    const Impl::ModuleState* module = (*impl_).find_module(module_id);
    if (!module) return {};
    for (std::size_t index = 0; index < (*module).spec.fields.size(); ++index) {
        if ((*module).spec.fields[index].stable_id == field_id) {
            return (*module).fields[index].editor;
        }
    }
    return {};
}

std::optional<Rect> InstrumentRack::module_bounds(
    std::string_view module_id) const noexcept {
    const Impl::ModuleState* module = (*impl_).find_module(module_id);
    return module ? std::optional<Rect>((*module).bounds) : std::nullopt;
}

bool InstrumentRack::set_module_enabled(std::string_view module_id,
                                        bool enabled) {
    require_mutable();
    Impl::ModuleState* module = (*impl_).find_module(module_id);
    if (!module || (*module).spec.enabled == enabled) return module != nullptr;
    (*module).spec.enabled = enabled;
    (*impl_).update_module_presentation(*module);
    for (InstrumentModuleSpec& public_module : (*impl_).public_modules) {
        if (public_module.stable_id == module_id) public_module.enabled = enabled;
    }
    return true;
}

bool InstrumentRack::set_module_state(std::string_view module_id,
                                      InstrumentModuleState state,
                                      std::string status_text) {
    require_mutable();
    require_instrument_text(status_text, "module status");
    Impl::ModuleState* module = (*impl_).find_module(module_id);
    if (!module) return false;
    (*module).spec.state = state;
    (*module).spec.status_text = std::move(status_text);
    (*impl_).update_module_presentation(*module);
    for (InstrumentModuleSpec& public_module : (*impl_).public_modules) {
        if (public_module.stable_id == module_id) {
            public_module.state = (*module).spec.state;
            public_module.status_text = (*module).spec.status_text;
        }
    }
    return true;
}

bool InstrumentRack::set_field_value(std::string_view module_id,
                                     std::string_view field_id,
                                     std::string value) {
    require_mutable();
    require_instrument_text(value, "field value");
    Impl::ModuleState* module = (*impl_).find_module(module_id);
    if (!module) return false;
    InstrumentFieldSpec* field = (*impl_).find_field(*module, field_id);
    if (!field) return false;
    const std::size_t index = static_cast<std::size_t>(
        field - (*module).spec.fields.data());
    (*impl_).synchronizing = true;
    (*field).value = std::move(value);
    (*module).fields[index].committed_value = (*field).value;
    if (const std::shared_ptr<gui_forms::TextBox> text = std::dynamic_pointer_cast<TextBox>(
            (*module).fields[index].editor)) {
        (*text).text = (*field).value;
    } else if (const std::shared_ptr<gui_forms::ComboBox> choice = std::dynamic_pointer_cast<ComboBox>(
                   (*module).fields[index].editor)) {
        const std::vector<std::string>::iterator selected =
            std::find((*field).choices.begin(),
                                        (*field).choices.end(), (*field).value);
        (*choice).selected_index = selected == (*field).choices.end()
            ? std::optional<std::size_t>{}
            : std::optional<std::size_t>{static_cast<std::size_t>(
                std::distance((*field).choices.begin(), selected))};
    }
    (*impl_).synchronizing = false;
    for (InstrumentModuleSpec& public_module : (*impl_).public_modules) {
        if (public_module.stable_id != module_id) continue;
        for (InstrumentFieldSpec& public_field : public_module.fields) {
            if (public_field.stable_id == field_id) public_field.value = (*field).value;
        }
    }
    return true;
}

bool InstrumentRack::set_field_validation(std::string_view module_id,
                                          std::string_view field_id,
                                          std::string message) {
    require_mutable();
    require_instrument_text(message, "field validation message");
    Impl::ModuleState* module = (*impl_).find_module(module_id);
    if (!module) return false;
    InstrumentFieldSpec* field = (*impl_).find_field(*module, field_id);
    if (!field) return false;
    (*field).validation_message = std::move(message);
    (*impl_).update_module_presentation(*module);
    for (InstrumentModuleSpec& public_module : (*impl_).public_modules) {
        if (public_module.stable_id != module_id) continue;
        for (InstrumentFieldSpec& public_field : public_module.fields) {
            if (public_field.stable_id == field_id) {
                public_field.validation_message = (*field).validation_message;
            }
        }
    }
    return true;
}

void InstrumentRack::set_action_content(Control::Ptr content,
                                        double minimum_width) {
    require_mutable();
    require_finite_positive(minimum_width, "action minimum width");
    if (content == (*impl_).action_content &&
        (*impl_).action_minimum_width == minimum_width) return;
    if (content == (*impl_).action_content) {
        (*impl_).action_minimum_width = minimum_width;
        invalidate(invalidation::bounds);
        return;
    }
    if (content && (*content).parent()) {
        throw std::invalid_argument(
            "InstrumentRack action content must be unparented");
    }
    if ((*impl_).action_content) {
        Control::Ptr removed = remove_child((*(*impl_).action_content).runtime_id());
        if (removed && (*removed).is_alive()) (*removed).dispose();
    }
    (*impl_).action_content = std::move(content);
    (*impl_).action_minimum_width = minimum_width;
    if ((*impl_).action_content) {
        (*(*impl_).action_content).margin = Insets{};
        add_child((*impl_).action_content);
    }
    invalidate(invalidation::bounds | Dirty::semantics);
}

Control::Ptr InstrumentRack::action_content() const noexcept {
    return (*impl_).action_content;
}

double InstrumentRack::read_view(const decltype(module_width)&) const { return (*impl_).module_width; }

void InstrumentRack::write_view(const decltype(module_width)&, double incoming) {
    require_mutable();
    require_finite_positive(incoming, "module width");
    if ((*impl_).module_width == incoming) return;
    (*impl_).module_width = incoming;
    invalidate(invalidation::bounds);
}

double InstrumentRack::read_view(const decltype(module_height)&) const { return (*impl_).module_height; }

void InstrumentRack::write_view(const decltype(module_height)&, double incoming) {
    require_mutable();
    require_finite_positive(incoming, "module height");
    if ((*impl_).module_height == incoming) return;
    (*impl_).module_height = incoming;
    invalidate(invalidation::bounds);
}

double InstrumentRack::read_view(const decltype(rack_gap)&) const { return (*impl_).rack_gap; }

void InstrumentRack::write_view(const decltype(rack_gap)&, double incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 0.0) {
        throw std::invalid_argument(
            "InstrumentRack gap must be finite and nonnegative");
    }
    if ((*impl_).rack_gap == incoming) return;
    (*impl_).rack_gap = incoming;
    invalidate(invalidation::bounds);
}

double InstrumentRack::content_height() const noexcept {
    return (*impl_).computed_content_height;
}

double InstrumentRack::preferred_height(double available_width) const {
    require_finite_positive(available_width, "available width");
    static_cast<void>((*impl_).compute_slots(available_width));
    return (*impl_).computed_content_height;
}

double InstrumentRack::read_view(const decltype(scroll_offset)&) const { return (*impl_).scroll_offset; }

void InstrumentRack::write_view(const decltype(scroll_offset)&, double incoming) {
    require_mutable();
    if (!std::isfinite(incoming)) {
        throw std::invalid_argument(
            "InstrumentRack scroll offset must be finite");
    }
    static_cast<void>((*impl_).compute_slots(committed_arranged_bounds().width));
    (*impl_).scroll_offset = incoming;
    (*impl_).clamp_scroll();
    (*impl_).arrange_children();
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

Size InstrumentRack::measure(Size available) {
    available = {std::max(0.0, available.width),
                 std::max(0.0, available.height)};
    const double desired_height = available.width > 0.0
        ? preferred_height(available.width) : 0.0;
    return {available.width, std::min(available.height, desired_height)};
}

void InstrumentRack::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    (*impl_).arrange_children();
}

void InstrumentRack::on_pointer(PointerEvent& event) {
    Panel::on_pointer(event);
    if (event.action != PointerAction::wheel ||
        std::abs(event.wheel_delta.y) <= 0.001) return;
    scroll_offset = (*impl_).scroll_offset - event.wheel_delta.y;
    event.handled = true;
}

void InstrumentRack::on_pointer_bubble(PointerEvent& event) {
    if (!event.handled) on_pointer(event);
}

void InstrumentRack::on_key_preview(KeyEvent& event) {
    if (event.action != KeyAction::down ||
        !has_modifier(event.modifiers, Modifier::alt) ||
        (event.physical_key != PhysicalKey::left &&
         event.physical_key != PhysicalKey::right)) return;
    Window* host = attached_window();
    const Control::Ptr focused = host ? (*host).focused_control() : Control::Ptr{};
    if (!focused) return;
    for (const Impl::ModuleState& module : (*impl_).states) {
        if (!contains_control(module.panel, focused)) continue;
        (*impl_).emit_move(module.spec.stable_id,
                         event.physical_key == PhysicalKey::right);
        event.handled = true;
        return;
    }
}

SemanticDescriptor InstrumentRack::semantic_descriptor() const {
    SemanticDescriptor descriptor = Panel::semantic_descriptor();
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.read().empty()
        ? "Editing instrument rack" : accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.value = std::to_string((*impl_).states.size()) + " modules";
    descriptor.states = SemanticState::enabled | SemanticState::visible;
    descriptor.exposed = true;
    descriptor.include_descendants = true;
    return descriptor;
}

void InstrumentRack::on_dispose() noexcept {
    (*impl_).states.clear();
    (*impl_).public_modules.clear();
    (*impl_).action_content.reset();
}

namespace {

// Its rows, modules and toolbars are child controls with their own anatomies.
constexpr ControlAnatomy instrument_rack_anatomy{
    .control_id = "instrument_rack",
    .parts = panel_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& InstrumentRack::anatomy() const noexcept {
    return instrument_rack_anatomy;
}

namespace {

// The events InstrumentRack adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 5> instrument_rack_interactions{{
    {.name = "field_changed", .payload = "InstrumentFieldChange", .mutable_payload = false},
    {.name = "field_committed", .payload = "InstrumentFieldChange", .mutable_payload = false},
    {.name = "module_toggled", .payload = "InstrumentModuleToggle", .mutable_payload = false},
    {.name = "remove_requested", .payload = "InstrumentModuleRequest", .mutable_payload = false},
    {.name = "move_requested", .payload = "InstrumentModuleMoveRequest", .mutable_payload = false},
}};

} // namespace

void InstrumentRack::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), instrument_rack_interactions.begin(), instrument_rack_interactions.end());
}

// property_codemod begin InstrumentRack (tools/property_families/instrument_rack.json; generated, do not edit)

// property_codemod end InstrumentRack

} // namespace gui_forms
