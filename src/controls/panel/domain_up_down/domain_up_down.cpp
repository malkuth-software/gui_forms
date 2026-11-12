#include "gui_forms/controls/domain_up_down.hpp"

#include "../up_down/spin_buttons.hpp"
#include "../panel_anatomy.hpp"
#include "../text_items_property.hpp"
#include "gui_forms/detail/bound_member_function.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

unsigned char folded(unsigned char byte) noexcept {
    const bool upper = byte >= 'A' && byte <= 'Z';
    const unsigned char result = upper ? static_cast<unsigned char>(byte - 'A' + 'a') : byte;
    return result;
}

// Equal ignoring ASCII case: how typed text matches an item.
bool equal_folded(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (folded(static_cast<unsigned char>(left[index])) !=
            folded(static_cast<unsigned char>(right[index]))) {
            return false;
        }
    }
    return true;
}

// The byte length of the UTF-8 sequence `lead` starts, from its high bits.
// A continuation or invalid lead counts as one byte, so a malformed
// sequence is read one byte at a time.
std::size_t utf8_sequence_length(unsigned char lead) noexcept {
    if (lead >= 0xF0U) return 4U;
    if (lead >= 0xE0U) return 3U;
    if (lead >= 0xC0U) return 2U;
    return 1U;
}

// The sorted order: ASCII case folded, then byte order, so "apple" and
// "Apple" keep a fixed order.
struct SortedItemOrder final {
    bool operator()(const std::string& left, const std::string& right) const noexcept {
        const std::size_t shared = std::min(left.size(), right.size());
        for (std::size_t index = 0; index < shared; ++index) {
            const unsigned char a = folded(static_cast<unsigned char>(left[index]));
            const unsigned char b = folded(static_cast<unsigned char>(right[index]));
            if (a != b) return a < b;
        }
        if (left.size() != right.size()) return left.size() < right.size();
        return left < right;
    }
};

} // namespace

DomainUpDown::DomainUpDown(StableId stable_id) : Panel(std::move(stable_id)) {
    // A field paints with the controls, not on the window's backplane as a
    // container panel does: a parent that paints its own face (a Ribbon
    // hosting it) would otherwise cover its border and spin buttons, which
    // still take the pointer.
    set_paint_plane(PaintPlane::control);
    set_tool_tip_presentation(ToolTipPresentation::field_hint);
    border_style = BorderStyle::line;
    // A field's ground: the theme's editor role, resolved per colour scheme
    // (no colour written here, which would fix the light paper in every
    // scheme).
    visual_role = ControlVisualRole::editor;
}

BindingValue DomainUpDown::items_property_value() const {
    BindingValue value = text_items_property::value_of(items.read());
    return value;
}

void DomainUpDown::set_items_property(const BindingValue& value) {
    std::vector<std::string> incoming = text_items_property::items_of(value, "DomainUpDown");
    items = std::move(incoming);
}

void DomainUpDown::reset_items_property() {
    items.clear();
}

bool DomainUpDown::should_serialize_items_property() const noexcept {
    return !items.read().empty();
}

DomainUpDown::~DomainUpDown() {
    // The editor may outlive this control (anyone may hold editor()); its
    // filter must not call into a destroyed control.
    if (editor_) (*editor_).set_edit_filter(Delegate<TextEdit&>{});
}

void DomainUpDown::EditorChangeCallback::operator()(const std::string&) const {
    const std::shared_ptr<DomainUpDown> domain = target.lock();
    if (!domain) return;
    if (!(*domain).synchronizing_) (*domain).match_text();
    if ((*domain).is_alive()) (*domain).on_text_changed();
}

void DomainUpDown::SpinnerStepCallback::operator()(int direction) const {
    const std::shared_ptr<DomainUpDown> domain = target.lock();
    if (!domain) return;
    // The upper arrow moves up the list, to the previous item.
    if (direction > 0) {
        (*domain).up_button();
    } else {
        (*domain).down_button();
    }
}

void DomainUpDown::initialize_control_tree() {
    const std::string prefix(stable_id().value());
    editor_ = make_control<TextBox>(StableId(prefix + ".editor"));
    (*editor_).border_style = BorderStyle::none;
    // The editor stays writable: read-only is enforced by the edit filter,
    // which also sees the characters a read-only field refuses.
    (*editor_).set_edit_filter(
        Delegate<TextEdit&>::bind<DomainUpDown, &DomainUpDown::filter_edit>(*this));
    spinner_ = make_control<SpinButtons>(StableId(prefix + ".spinner"));
    add_child(editor_);
    add_child(spinner_);
    const std::weak_ptr<DomainUpDown> weak =
        std::static_pointer_cast<DomainUpDown>(shared_from_this());
    editor_change_ = (*editor_).text_changed().subscribe(*this, EditorChangeCallback{weak});
    SpinButtons& spin = static_cast<SpinButtons&>(*spinner_);
    spinner_step_ = spin.stepped().subscribe(*this, SpinnerStepCallback{weak});
}

void DomainUpDown::filter_edit(TextEdit& edit) {
    if (!read_only.read()) return;
    edit.verdict = TextEditVerdict::refuse;
    if (edit.kind != TextEditKind::typing || edit.replacement.empty()) return;
    // The first character typed, as its UTF-8 bytes (fewer when the
    // replacement ends inside the sequence).
    const unsigned char lead = static_cast<unsigned char>(edit.replacement[0]);
    const std::size_t length = utf8_sequence_length(lead);
    const std::string_view typed = edit.replacement.substr(0U, length);
    const std::optional<std::size_t> found = next_item_starting_with(typed);
    // Showing the item changes the editor's text inside its filter call; the
    // refused edit is then abandoned, as TextBox's filter contract states.
    if (found) select(found, true);
}

std::optional<std::size_t> DomainUpDown::next_item_starting_with(std::string_view prefix) const {
    const std::size_t count = items.read().size();
    if (count == 0U || prefix.empty()) return std::nullopt;
    const std::size_t start = selected_index_ ? *selected_index_ + 1U : 0U;
    for (std::size_t step = 0U; step < count; ++step) {
        const std::size_t index = (start + step) % count;
        const std::string_view item = items[index];
        if (item.size() >= prefix.size() && equal_folded(item.substr(0U, prefix.size()), prefix)) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> DomainUpDown::matching_index(std::string_view candidate) const {
    for (std::size_t index = 0; index < items.read().size(); ++index) {
        if (equal_folded(items[index], candidate)) return index;
    }
    return std::nullopt;
}

void DomainUpDown::select(std::optional<std::size_t> index, bool show_text) {
    if (show_text && index && editor_) {
        synchronizing_ = true;
        (*editor_).text = items[*index];
        synchronizing_ = false;
    }
    if (selected_index_ == index) return;
    selected_index_ = index;
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(selected_index_changed_, selected_index_);
}

void DomainUpDown::match_text() {
    select(matching_index(text.read()), false);
}

void DomainUpDown::sort_items() {
    if (!sorted.read()) return;
    TextItemsCapability<DomainUpDown>::sort_items(*this, SortedItemOrder{});
}

void DomainUpDown::prepare_edit(const decltype(items)&, const CollectionEdit&, std::span<std::string>) {
    require_mutable();
}

void DomainUpDown::written(const decltype(items)&, const CollectionEdit&) {
    sort_items();
    match_text();
    invalidate(Dirty::semantics);
}

std::optional<std::size_t> DomainUpDown::read_view(const decltype(selected_index)&) const {
    const std::optional<std::size_t> current = selected_index_;
    return current;
}

void DomainUpDown::write_view(const decltype(selected_index)&, std::optional<std::size_t> incoming) {
    require_mutable();
    if (incoming && *incoming >= items.read().size()) {
        throw std::out_of_range("DomainUpDown selected index is past the end");
    }
    select(incoming, true);
}

// A copy: the editor's text view reads the editing engine by value.
std::string DomainUpDown::read_view(const decltype(text)&) const {
    if (!editor_) return std::string{};
    std::string current = (*editor_).text.read();
    return current;
}

void DomainUpDown::write_view(const decltype(text)&, std::string incoming) {
    require_mutable();
    if (!editor_) return;
    synchronizing_ = true;
    (*editor_).text = std::move(incoming);
    synchronizing_ = false;
    match_text();
}

void DomainUpDown::prepare_write(const decltype(read_only)&, bool&) {
    require_mutable();
}

void DomainUpDown::written(const decltype(read_only)&) {
    // Undo and redo do not pass the edit filter; history from before the
    // field became read-only must not change it.
    if (editor_) (*editor_).clear_undo_history();
    invalidate(Dirty::paint | Dirty::semantics);
}

void DomainUpDown::prepare_write(const decltype(sorted)&, bool&) {
    require_mutable();
}

void DomainUpDown::written(const decltype(sorted)&) {
    sort_items();
    match_text();
    invalidate(Dirty::semantics);
}

void DomainUpDown::step(std::ptrdiff_t offset) {
    if (items.read().empty()) return;
    const std::ptrdiff_t count = static_cast<std::ptrdiff_t>(items.read().size());
    std::ptrdiff_t next = 0;
    if (!selected_index_) {
        next = offset < 0 ? count - 1 : 0;
    } else {
        next = static_cast<std::ptrdiff_t>(*selected_index_) + offset;
        if (next < 0) {
            next = wrap.read() ? count - 1 : 0;
        } else if (next >= count) {
            next = wrap.read() ? 0 : count - 1;
        }
    }
    select(static_cast<std::size_t>(next), true);
    // Stepping puts the focus in the field with the new item's text
    // selected, so typing replaces it. Outside a window nothing has focus.
    Window* const host = window();
    if (editor_ && host != nullptr) {
        static_cast<void>((*host).request_focus(editor_));
        (*editor_).select_all();
    }
}

void DomainUpDown::up_button() {
    step(-1);
}

void DomainUpDown::down_button() {
    step(1);
}

void DomainUpDown::on_paint_overlay(Painter& painter, Rect local_damage) {
    Panel::on_paint_overlay(painter, local_damage);
    if (!spinner_) return;
    // spinner_ is the SpinButtons initialize_control_tree made.
    const SpinPartNames names{"up_arrow", "down_arrow", "up_glyph", "down_glyph"};
    paint_spin_parts(painter, *this, static_cast<const SpinButtons&>(*spinner_), names);
}

void DomainUpDown::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    // The spin column takes button_width.read() (or the whole width, when narrower)
    // at the right; the field takes the rest.
    // Both sit inside the control's own border (the borderless editor
    // draws none of its own).
    const double inset = border_style.read() == BorderStyle::none ? 0.0 : 1.0;
    const double inner_width = std::max(0.0, final_bounds.width - 2.0 * inset);
    const double inner_height = std::max(0.0, final_bounds.height - 2.0 * inset);
    const double column_width = std::min(button_width.read(), inner_width);
    const double field_width = std::max(0.0, inner_width - column_width);
    const Rect field{inset, inset, field_width, inner_height};
    const Rect column{inset + field_width, inset, column_width, inner_height};
    if (editor_) set_child_layout(editor_, field);
    if (spinner_) set_child_layout(spinner_, column);
}

void DomainUpDown::on_key_preview(KeyEvent& event) {
    if (!enabled.read() || event.action != KeyAction::down) return;
    if (event.physical_key == PhysicalKey::up) {
        up_button();
        event.handled = true;
    } else if (event.physical_key == PhysicalKey::down) {
        down_button();
        event.handled = true;
    } else {
        // Other keys go to the field.
    }
}

void DomainUpDown::on_pointer_preview(PointerEvent& event) {
    if (!enabled.read() || event.action != PointerAction::wheel || event.wheel_delta.y == 0.0) return;
    // Wheel up moves up the list.
    if (event.wheel_delta.y > 0.0) {
        up_button();
    } else {
        down_button();
    }
    event.handled = true;
}

SemanticDescriptor DomainUpDown::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::combo_box;
    descriptor.name = accessible_name.read();
    descriptor.value = std::string(text.read());
    descriptor.description = accessible_description.read();
    descriptor.actions = {SemanticAction::focus, SemanticAction::increment,
                          SemanticAction::decrement, SemanticAction::set_value};
    if (read_only.read()) descriptor.states |= SemanticState::read_only;
    descriptor.exposed = true;
    descriptor.include_descendants = false;
    return descriptor;
}

bool DomainUpDown::on_semantic_action(SemanticAction action, std::string_view value_text) {
    // Increment moves to the next item, as the down arrow does.
    if (action == SemanticAction::increment) {
        down_button();
        return true;
    }
    if (action == SemanticAction::decrement) {
        up_button();
        return true;
    }
    if (action == SemanticAction::set_value) {
        const std::optional<std::size_t> index = matching_index(value_text);
        if (index) {
            selected_index = index;
            return true;
        }
        if (read_only.read()) return false;
        text = std::string(value_text);
        return true;
    }
    Window* const host = window();
    if (action == SemanticAction::focus && editor_ && host != nullptr) {
        const bool focused = (*host).request_focus(editor_);
        return focused;
    }
    return Panel::on_semantic_action(action, value_text);
}

namespace {

// The spin arrows are hot under the pointer and pressed while held.
constexpr std::array<AnatomyState, 2> spin_arrow_states{{AnatomyState::hot, AnatomyState::pressed}};

constexpr std::array<PartField<DomainUpDown::Parts>, 7> domain_up_down_extra_part_fields{{
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &DomainUpDown::Parts::face},
    {.declaration = {.name = "edge", .role = PartRole::edge, .composed_from = {}},
     .member = &DomainUpDown::Parts::edge},
    {.declaration = {.name = "field", .role = PartRole::well, .composed_from = "text_box"},
     .member = &DomainUpDown::Parts::field},
    {.declaration = {.name = "up_arrow", .role = PartRole::actuator, .composed_from = "button",
                     .states = spin_arrow_states},
     .member = &DomainUpDown::Parts::up_arrow},
    {.declaration = {.name = "down_arrow", .role = PartRole::actuator, .composed_from = "button",
                     .states = spin_arrow_states},
     .member = &DomainUpDown::Parts::down_arrow},
    {.declaration = {.name = "up_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = spin_arrow_states},
     .member = &DomainUpDown::Parts::up_glyph},
    {.declaration = {.name = "down_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = spin_arrow_states},
     .member = &DomainUpDown::Parts::down_glyph},
}};

constexpr std::array<StateField<DomainUpDown::States>, 10> domain_up_down_state_fields{{
    {.state = AnatomyState::normal, .member = &DomainUpDown::States::normal},
    {.state = AnatomyState::pressed, .member = &DomainUpDown::States::pressed},
    {.state = AnatomyState::captured, .member = &DomainUpDown::States::captured},
    {.state = AnatomyState::enabled, .member = &DomainUpDown::States::enabled},
    {.state = AnatomyState::disabled, .member = &DomainUpDown::States::disabled},
    {.state = AnatomyState::focused, .member = &DomainUpDown::States::focused},
    {.state = AnatomyState::focus_visible, .member = &DomainUpDown::States::focus_visible},
    {.state = AnatomyState::read_only, .member = &DomainUpDown::States::read_only},
    {.state = AnatomyState::invalid, .member = &DomainUpDown::States::invalid},
    {.state = AnatomyState::pending, .member = &DomainUpDown::States::pending},
}};

constexpr std::array<PartDeclaration, scrollable_detail::part_count + 7> domain_up_down_parts =
    joined_declarations(scrollable_detail::parts, part_declarations(domain_up_down_extra_part_fields));
constexpr std::array<AnatomyState, 10> domain_up_down_states =
    state_declarations(domain_up_down_state_fields);

constexpr ControlAnatomy domain_up_down_anatomy{
    .control_id = "domain_up_down",
    .parts = domain_up_down_parts,
    .states = domain_up_down_states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& DomainUpDown::anatomy() const noexcept {
    return domain_up_down_anatomy;
}

DomainUpDown::Parts DomainUpDown::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(), record.scrollable);
    read_parts(anatomy(), domain_up_down_extra_part_fields, published.parts(), record);
    return record;
}

DomainUpDown::States DomainUpDown::states() const {
    States record;
    read_states(domain_up_down_state_fields, anatomy_states(), record);
    return record;
}

void DomainUpDown::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Panel::Parts panel = panel_parts();
    Parts record;
    record.scrollable = panel.scrollable;
    record.face = panel.face;
    record.edge = panel.edge;
    // The field is the child text box; the arrows and their glyphs are the
    // spin column's halves, from the geometry the column paints with.
    if (editor_) {
        record.field = PartGeometry::from_bounds((*editor_).committed_arranged_bounds());
    }
    if (spinner_) {
        // spinner_ is the SpinButtons initialize_control_tree made.
        const SpinPartGeometry spin = spin_part_geometry(static_cast<const SpinButtons&>(*spinner_));
        record.up_arrow = spin.upper_arrow;
        record.down_arrow = spin.lower_arrow;
        record.up_glyph = spin.upper_glyph;
        record.down_glyph = spin.lower_glyph;
    }
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), domain_up_down_extra_part_fields, record, geometry);
}

AnatomyStateSet DomainUpDown::anatomy_states() const {
    AnatomyStateSet states = ScrollableControl::anatomy_states();
    if (read_only.read()) states.insert(AnatomyState::read_only);
    const ControlVisualStatus status = visual_status();
    if (status == ControlVisualStatus::invalid) {
        states.insert(AnatomyState::invalid);
    } else if (status == ControlVisualStatus::pending) {
        states.insert(AnatomyState::pending);
    } else {
        // A normal status adds no state.
    }
    return states;
}

namespace {

// The events DomainUpDown adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 1> domain_up_down_interactions{{
    {.name = "selected_index_changed", .payload = "std::optional<std::size_t>", .mutable_payload = false},
}};

} // namespace

void DomainUpDown::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), domain_up_down_interactions.begin(),
                        domain_up_down_interactions.end());
}

namespace {

// The items collection in its BindingValue form (text_items_property.hpp):
// a hand table entry (extra_entries) over the CollectionCell.
PropertyDescriptor domain_up_down_items_descriptor() {
    PropertyDescriptor descriptor = text_items_property::descriptor("Ordered text items the spinner steps through.");
    return descriptor;
}

const PropertyDescriptor& describe_domain_up_down_items() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        domain_up_down_items_descriptor(), PropertyOperations{true, true, true, false});
    return descriptor;
}

} // namespace

// property_codemod begin DomainUpDown (tools/property_families/domain_up_down.json; generated, do not edit)

const std::array<PropertyEntry, DomainUpDown::domain_up_down_property_count>& DomainUpDown::domain_up_down_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 1U> own{{
        {"items", &describe_domain_up_down_items,
         &read_member_entry<DomainUpDown, decltype(&DomainUpDown::items_property_value), &DomainUpDown::items_property_value>,
         &write_member_entry<DomainUpDown, decltype(&DomainUpDown::set_items_property), &DomainUpDown::set_items_property>,
         &reset_member_entry<DomainUpDown, decltype(&DomainUpDown::reset_items_property), &DomainUpDown::reset_items_property>, &should_serialize_member_entry<DomainUpDown, decltype(&DomainUpDown::should_serialize_items_property), &DomainUpDown::should_serialize_items_property>, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "DomainUpDown's property table names a property twice");
    static const std::array<PropertyEntry, domain_up_down_property_count> entries =
        join_property_tables(Panel::panel_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> DomainUpDown::property_table() const noexcept {
    const std::array<PropertyEntry, domain_up_down_property_count>& entries = domain_up_down_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void DomainUpDown::on_text_changed() {}

void DomainUpDown::prepare_write(const decltype(wrap)&, bool&) {
    require_mutable();
}

void DomainUpDown::written(const decltype(wrap)&) {}

void DomainUpDown::prepare_write(const decltype(button_width)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 12.0 || incoming > 64.0) {
        throw std::out_of_range("DomainUpDown button width must be finite and between 12 and 64");
    }
}

void DomainUpDown::written(const decltype(button_width)&) {
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// property_codemod end DomainUpDown

} // namespace gui_forms
