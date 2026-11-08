#include "gui_forms/controls/panel/numeric_up_down/numeric_up_down.hpp"

#include "../up_down/spin_buttons.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"
#include "gui_forms/window.hpp"
#include "../panel_anatomy.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <array>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace gui_forms {

NumericUpDown::NumericUpDown(StableId stable_id) : Panel(std::move(stable_id)) {
    set_tool_tip_presentation(ToolTipPresentation::field_hint);
    border_style = BorderStyle::line;
    set_background(style().paper);
}

void NumericUpDown::EditorChangeCallback::operator()(
    const std::string&) const {
    const std::shared_ptr<NumericUpDown> numeric = target.lock();
    if (numeric && !(*numeric).synchronizing_) {
        (*numeric).commit_editor_text();
    }
}

void NumericUpDown::SpinnerStepCallback::operator()(int direction) const {
    const std::shared_ptr<NumericUpDown> numeric = target.lock();
    if (numeric) (*numeric).step(direction);
}

void NumericUpDown::initialize_control_tree() {
    const std::string prefix(stable_id().value());
    editor_ = make_control<TextBox>(StableId(prefix + ".editor"));
    (*editor_).border_style = BorderStyle::none;
    spinner_ = make_control<SpinButtons>(StableId(prefix + ".spinner"));
    add_child(editor_);
    add_child(spinner_);
    const std::weak_ptr<NumericUpDown> weak =
        std::static_pointer_cast<NumericUpDown>(shared_from_this());
    editor_change_ = (*editor_).text_changed().subscribe(
        *this, EditorChangeCallback{weak});
    std::shared_ptr<gui_forms::SpinButtons> spin = std::dynamic_pointer_cast<SpinButtons>(spinner_);
    spinner_step_ = (*spin).stepped().subscribe(
        *this, SpinnerStepCallback{weak});
    synchronize_editor();
}

// Writes minimum, then maximum: a minimum above the old maximum raises it
// first, and the second write settles the range. value_changed is raised at
// most once, when the value is clamped.
void NumericUpDown::set_range(double lowest, double highest) {
    require_mutable();
    if (!std::isfinite(lowest) || !std::isfinite(highest) || lowest > highest) {
        throw std::invalid_argument("NumericUpDown range must be finite and ordered");
    }
    minimum = lowest;
    maximum = highest;
}

// The range: either bound moves the other when they would cross, as
// Windows Forms does, then clamps value into the range.
void NumericUpDown::prepare_write(const decltype(minimum)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming)) throw std::invalid_argument("NumericUpDown range must be finite and ordered");
}

void NumericUpDown::written(const decltype(minimum)&) {
    if (maximum.read() < minimum.read()) maximum = minimum.read();
    clamp_value();
    invalidate(Dirty::paint | Dirty::semantics);
}

void NumericUpDown::prepare_write(const decltype(maximum)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming)) throw std::invalid_argument("NumericUpDown range must be finite and ordered");
}

void NumericUpDown::written(const decltype(maximum)&) {
    if (minimum.read() > maximum.read()) minimum = maximum.read();
    clamp_value();
    invalidate(Dirty::paint | Dirty::semantics);
}

void NumericUpDown::clamp_value() {
    const double clamped = std::clamp(value.read(), minimum.read(), maximum.read());
    if (clamped != value.read()) value = clamped;
}

void NumericUpDown::prepare_write(const decltype(value)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < minimum.read() || incoming > maximum.read()) {
        throw std::out_of_range("NumericUpDown value is outside its range");
    }
}

void NumericUpDown::written(const decltype(value)&) {
    if (!committing_) synchronize_editor();
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(value_changed_, value.read());
}

void NumericUpDown::prepare_write(const decltype(decimal_places)&, std::uint8_t& incoming) {
    require_mutable();
    if (incoming > 12U) {
        throw std::invalid_argument("NumericUpDown decimal places may not exceed 12");
    }
}

// Decimal places and hexadecimal exclude each other: each turns the other
// off, then the editor shows the value again.
void NumericUpDown::written(const decltype(decimal_places)&) {
    if (hexadecimal.read() && decimal_places.read() != 0U) hexadecimal = false;
    synchronize_editor();
    invalidate(Dirty::paint | Dirty::semantics);
}

void NumericUpDown::prepare_write(const decltype(hexadecimal)&, bool&) {
    require_mutable();
}

void NumericUpDown::written(const decltype(hexadecimal)&) {
    if (hexadecimal.read()) decimal_places = std::uint8_t{0U};
    synchronize_editor();
    invalidate(Dirty::paint | Dirty::semantics);
}

std::string NumericUpDown::formatted_value() const {
    std::ostringstream stream;
    if (hexadecimal.read()) {
        stream << std::uppercase << std::hex << static_cast<std::int64_t>(std::llround(value.read()));
    } else {
        stream << std::fixed << std::setprecision(decimal_places.read()) << value.read();
    }
    return stream.str();
}

void NumericUpDown::synchronize_editor() {
    if (!editor_) return;
    synchronizing_ = true;
    (*editor_).text = formatted_value();
    synchronizing_ = false;
}

void NumericUpDown::commit_editor_text() {
    if (!editor_ || (*editor_).text.read().empty()) return;
    try {
        std::size_t consumed{};
        double parsed{};
        if (hexadecimal.read()) {
            parsed = static_cast<double>(std::stoll(std::string((*editor_).text.read()),
                                                    &consumed, 16));
        } else {
            parsed = std::stod(std::string((*editor_).text.read()), &consumed);
        }
        if (consumed == (*editor_).text.read().size() && std::isfinite(parsed) &&
            parsed >= minimum.read() && parsed <= maximum.read() && parsed != value.read()) {
            // The user's text stays as typed: the write does not reformat it.
            committing_ = true;
            try {
                value = parsed;
            } catch (...) {
                committing_ = false;
                throw;
            }
            committing_ = false;
        }
    } catch (const std::exception&) {
    }
}

void NumericUpDown::step(int direction) {
    const double next = std::clamp(value.read() + static_cast<double>(direction) * increment.read(),
                                   minimum.read(), maximum.read());
    if (next != value.read()) value = next;
    if (editor_ && window() != nullptr) {
        static_cast<void>((*window()).request_focus(editor_));
        (*editor_).select_all();
    }
}

void NumericUpDown::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    // The field and the spin column sit inside the control's own border
    // (the borderless editor draws none of its own).
    const double inset = border_style.read() == BorderStyle::none ? 0.0 : 1.0;
    const double inner_width = std::max(0.0, final_bounds.width - 2.0 * inset);
    const double inner_height = std::max(0.0, final_bounds.height - 2.0 * inset);
    const double column_width = std::min(button_width.read(), inner_width);
    const double field_width = std::max(0.0, inner_width - column_width);
    if (editor_) set_child_layout(editor_, {inset, inset, field_width, inner_height});
    if (spinner_) set_child_layout(spinner_, {inset + field_width, inset, column_width, inner_height});
}

void NumericUpDown::on_key_preview(KeyEvent& event) {
    if (!enabled.read() || event.action != KeyAction::down) return;
    if (event.physical_key == PhysicalKey::up || event.physical_key == PhysicalKey::down) {
        step(event.physical_key == PhysicalKey::up ? 1 : -1);
        event.handled = true;
    } else if (event.physical_key == PhysicalKey::enter) {
        commit_editor_text();
        synchronize_editor();
        event.handled = true;
    }
}

void NumericUpDown::on_pointer_preview(PointerEvent& event) {
    if (!enabled.read() || event.action != PointerAction::wheel || event.wheel_delta.y == 0.0) return;
    step(event.wheel_delta.y > 0.0 ? 1 : -1);
    event.handled = true;
}

SemanticDescriptor NumericUpDown::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::numeric_field;
    descriptor.name = accessible_name.read();
    descriptor.value = formatted_value();
    descriptor.numeric_value = value.read();
    descriptor.minimum_value = minimum.read();
    descriptor.maximum_value = maximum.read();
    // The arrow keys and the spin buttons move by the increment; there is no
    // larger step, so none is published (owner decision, stage A gap 7).
    descriptor.small_step = increment.read();
    descriptor.description = accessible_description.read();
    descriptor.actions = {SemanticAction::focus, SemanticAction::increment,
                          SemanticAction::decrement, SemanticAction::set_value};
    descriptor.exposed = true;
    descriptor.include_descendants = false;
    return descriptor;
}

bool NumericUpDown::on_semantic_action(SemanticAction action,
                                       std::string_view value_text) {
    if (action == SemanticAction::increment || action == SemanticAction::decrement) {
        step(action == SemanticAction::increment ? 1 : -1);
        return true;
    }
    if (action == SemanticAction::set_value) {
        try {
            std::size_t consumed{};
            const double parsed = std::stod(std::string(value_text), &consumed);
            if (consumed != value_text.size()) return false;
            value = parsed;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
    if (action == SemanticAction::focus && editor_ && window() != nullptr) {
        return (*window()).request_focus(editor_);
    }
    return Panel::on_semantic_action(action, value_text);
}

namespace {

// The spin arrows are hot under the pointer and pressed while held.
constexpr std::array<AnatomyState, 2> spin_arrow_states{{AnatomyState::hot, AnatomyState::pressed}};

constexpr std::array<PartField<NumericUpDown::Parts>, 7> numeric_up_down_extra_part_fields{{
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &NumericUpDown::Parts::face},
    {.declaration = {.name = "edge", .role = PartRole::edge, .composed_from = {}},
     .member = &NumericUpDown::Parts::edge},
    {.declaration = {.name = "field", .role = PartRole::well, .composed_from = "text_box"},
     .member = &NumericUpDown::Parts::field},
    {.declaration = {.name = "increment_arrow", .role = PartRole::actuator, .composed_from = "button",
                     .states = spin_arrow_states},
     .member = &NumericUpDown::Parts::increment_arrow},
    {.declaration = {.name = "decrement_arrow", .role = PartRole::actuator, .composed_from = "button",
                     .states = spin_arrow_states},
     .member = &NumericUpDown::Parts::decrement_arrow},
    {.declaration = {.name = "increment_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = spin_arrow_states},
     .member = &NumericUpDown::Parts::increment_glyph},
    {.declaration = {.name = "decrement_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = spin_arrow_states},
     .member = &NumericUpDown::Parts::decrement_glyph},
}};

constexpr std::array<StateField<NumericUpDown::States>, 9> numeric_up_down_state_fields{{
    {.state = AnatomyState::normal, .member = &NumericUpDown::States::normal},
    {.state = AnatomyState::pressed, .member = &NumericUpDown::States::pressed},
    {.state = AnatomyState::captured, .member = &NumericUpDown::States::captured},
    {.state = AnatomyState::enabled, .member = &NumericUpDown::States::enabled},
    {.state = AnatomyState::disabled, .member = &NumericUpDown::States::disabled},
    {.state = AnatomyState::focused, .member = &NumericUpDown::States::focused},
    {.state = AnatomyState::focus_visible, .member = &NumericUpDown::States::focus_visible},
    {.state = AnatomyState::invalid, .member = &NumericUpDown::States::invalid},
    {.state = AnatomyState::pending, .member = &NumericUpDown::States::pending},
}};

constexpr std::array<PartDeclaration, scrollable_detail::part_count + 7>
    numeric_up_down_parts = joined_declarations(scrollable_detail::parts,
                                         part_declarations(numeric_up_down_extra_part_fields));
constexpr std::array<AnatomyState, 9> numeric_up_down_states =
    state_declarations(numeric_up_down_state_fields);

constexpr ControlAnatomy numeric_up_down_anatomy{
    .control_id = "numeric_up_down",
    .parts = numeric_up_down_parts,
    .states = numeric_up_down_states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& NumericUpDown::anatomy() const noexcept {
    return numeric_up_down_anatomy;
}

NumericUpDown::Parts NumericUpDown::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(),
               record.scrollable);
    read_parts(anatomy(), numeric_up_down_extra_part_fields, published.parts(), record);
    return record;
}

NumericUpDown::States NumericUpDown::states() const {
    States record;
    read_states(numeric_up_down_state_fields, anatomy_states(), record);
    return record;
}

void NumericUpDown::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Panel::Parts panel = panel_parts();
    Parts record;
    record.scrollable = panel.scrollable;
    record.face = panel.face;
    record.edge = panel.edge;
    // The field is the child text box, which publishes its own well, content
    // and caret; the actuators and their glyphs are the spin column's halves,
    // from the same geometry the column paints with.
    if (editor_) {
        record.field = PartGeometry::from_bounds((*editor_).committed_arranged_bounds());
    }
    if (spinner_) {
        // spinner_ is the SpinButtons initialize_control_tree made.
        const SpinPartGeometry spin = spin_part_geometry(static_cast<const SpinButtons&>(*spinner_));
        record.increment_arrow = spin.upper_arrow;
        record.decrement_arrow = spin.lower_arrow;
        record.increment_glyph = spin.upper_glyph;
        record.decrement_glyph = spin.lower_glyph;
    }
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), numeric_up_down_extra_part_fields, record, geometry);
}

AnatomyStateSet NumericUpDown::anatomy_states() const {
    AnatomyStateSet states = ScrollableControl::anatomy_states();
    if (visual_status() == ControlVisualStatus::invalid) {
        states.insert(AnatomyState::invalid);
    } else if (visual_status() == ControlVisualStatus::pending) {
        states.insert(AnatomyState::pending);
    }
    return states;
}

namespace {

// The events NumericUpDown adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 1> numeric_up_down_interactions{{
    {.name = "value_changed", .payload = "double", .mutable_payload = false},
}};

} // namespace

void NumericUpDown::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), numeric_up_down_interactions.begin(), numeric_up_down_interactions.end());
}

// property_codemod begin NumericUpDown (tools/property_families/numeric_up_down.json; generated, do not edit)

namespace {

PropertyDescriptor numeric_up_down_value_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "value";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Behavior";
    descriptor.description = "Current numeric value.";
    descriptor.default_value = BindingValue{0.0};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    return descriptor;
}

const PropertyDescriptor& describe_numeric_up_down_value() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        numeric_up_down_value_descriptor(), PropertyOperations{true, true, false, true});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, NumericUpDown::numeric_up_down_property_count>& NumericUpDown::numeric_up_down_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 1U> own{{
        {"value", &describe_numeric_up_down_value,
         &read_cell_entry<NumericUpDown, decltype(&NumericUpDown::value), &NumericUpDown::value>,
         &write_cell_entry<NumericUpDown, decltype(&NumericUpDown::value), &NumericUpDown::value>,
         nullptr, nullptr, nullptr,
         &connect_event_entry<NumericUpDown, decltype(&NumericUpDown::value_changed_), &NumericUpDown::value_changed_>},
    }};
    static_assert(property_names_distinct(own), "NumericUpDown's property table names a property twice");
    static const std::array<PropertyEntry, numeric_up_down_property_count> entries =
        join_property_tables(Panel::panel_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> NumericUpDown::property_table() const noexcept {
    const std::array<PropertyEntry, numeric_up_down_property_count>& entries = numeric_up_down_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void NumericUpDown::prepare_write(const decltype(increment)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming <= 0.0) {
        throw std::invalid_argument("NumericUpDown increment must be finite and positive");
    }
}

void NumericUpDown::written(const decltype(increment)&) {
}

void NumericUpDown::prepare_write(const decltype(button_width)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 12.0 || incoming > 64.0) {
        throw std::out_of_range("NumericUpDown button width must be finite and between 12 and 64");
    }
}

void NumericUpDown::written(const decltype(button_width)&) {
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// property_codemod end NumericUpDown

} // namespace gui_forms
