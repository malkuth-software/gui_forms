#include "gui_forms/controls/collections/choice_group.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"
#include "../panel/panel_anatomy.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace gui_forms {
namespace {
// The ChoiceGroupPresentation enumeration, numbered as declared. Static
// storage; any thread may read it.
std::shared_ptr<const PropertyEnumDescriptor> choice_group_presentation_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "choice_group_presentation", {{"buttons", 0}, {"segmented", 1}}, false});
    return value;
}

// The space between a segmented group's track and its segments, in logical
// units.
constexpr double segment_inset = 2.0;

// One item. In the buttons presentation it paints as a command button; as
// a segment it paints only its label (and a hover wash and focus ring) in
// the choice family, over the group's track and selection indicator.
class ChoiceButton final : public Button {
public:
    explicit ChoiceButton(StableId id) : Button(std::move(id)) {}
    SemanticDescriptor semantic_descriptor() const override {
        SemanticDescriptor descriptor = Button::semantic_descriptor();
        descriptor.role = SemanticRole::radio_button;
        if (selected()) descriptor.states |= SemanticState::checked;
        return descriptor;
    }
    [[nodiscard]] bool segment() const noexcept { return segment_; }
    void set_segment(bool segment) {
        if (segment_ == segment) return;
        segment_ = segment;
        invalidate(Dirty::paint);
    }
    void on_paint(Painter& painter, Rect local_damage) override {
        if (!segment_) {
            Button::on_paint(painter, local_damage);
            return;
        }
        const Rect bounds = local_bounds();
        const AnatomyStateSet states = anatomy_states();
        // The selected segment shows the group's indicator; another shows a
        // wash while hot or pressed.
        if (!selected() && (hovered_visual() || pressed_visual())) {
            PartPaint wash{.role = PartRole::face, .bounds = bounds, .states = states,
                           .family = ControlVisualRole::choice};
            paint_part(painter, *this, wash);
        }
        const std::string shown = text.read();
        PartPaint label{.role = PartRole::label, .bounds = bounds, .states = states,
                        .family = ControlVisualRole::choice};
        label.text = shown;
        label.font = text_font(font.read());
        label.alignment = PartTextAlignment::center;
        paint_part(painter, *this, label);
        if (focus_cue_visible() && bounds.width > 4.0 && bounds.height > 4.0) {
            PartPaint ring{.role = PartRole::focus, .bounds = {1.5, 1.5, bounds.width - 3.0, bounds.height - 3.0},
                           .states = states, .family = ControlVisualRole::choice};
            ring.states.insert(AnatomyState::focus_visible);
            paint_part(painter, *this, ring);
        }
    }

private:
    bool segment_{};
};
} // namespace

struct ChoiceGroup::Entry final {
    int id{};
    std::shared_ptr<ChoiceButton> button{};
    SubscriptionToken click{};
    SubscriptionToken enabled{};
    SubscriptionToken visible{};
    SubscriptionToken focus{};
};
struct ChoiceGroup::Revision final {
    std::vector<ChoiceItem> items{};
    std::vector<std::shared_ptr<Entry>> entries{};
};
struct ChoiceGroup::ItemClick final {
    ChoiceGroup& group;
    int id;
    void operator()(ButtonBase&) const { group.activate_item(id); }
};

struct ChoiceGroup::ItemAvailability final {
    ChoiceGroup& group;
    void operator()(bool) const { group.refresh_tab_stop(); }
};

void ChoiceGroup::refresh_tab_stop() {
    if (!revision_ || replacing_) return;
    const Control::Ptr retained = shared_from_this();
    const std::shared_ptr<Revision> revision = revision_;
    const Control::Ptr focused = window() == nullptr ? Control::Ptr{} : (*window()).focused_control();
    int selected = -1;
    for (std::size_t index = 0U; index < (*revision).entries.size(); ++index) {
        const std::shared_ptr<Button> button = (*(*revision).entries[index]).button;
        if (!(*button).enabled.read() || !(*button).visible.read()) continue;
        if (selected < 0 || (*button).tab_stop.read()) selected = static_cast<int>(index);
        if (button == focused) { selected = static_cast<int>(index); break; }
    }
    for (std::size_t index = 0U; index < (*revision).entries.size(); ++index) {
        (*(*(*revision).entries[index]).button).tab_stop = static_cast<int>(index) == selected;
    }
}

ChoiceGroup::ChoiceGroup(StableId id) : Panel(std::move(id)) {
    set_focusable(false);
    tab_stop = false;
}
ChoiceGroup::~ChoiceGroup() = default;

std::span<const ChoiceItem> ChoiceGroup::items() const noexcept {
    if (!revision_) return {};
    return (*revision_).items;
}
Button& ChoiceGroup::item(const int index) const {
    if (!revision_ || index < 0 || static_cast<std::size_t>(index) >= (*revision_).entries.size()) {
        throw std::out_of_range("ChoiceGroup item index");
    }
    const Entry& entry = *(*revision_).entries[static_cast<std::size_t>(index)];
    return *entry.button;
}
Control* ChoiceGroup::part(const std::string_view name, const int index) const {
    if (name != "item") return nullptr;
    Control* const result = &item(index);
    return result;
}
// The selection's write: the range check and a bound Value's refusal come
// first; a change checks the selected button, then raises changed.
void ChoiceGroup::prepare_write(const decltype(selected_index)&, int& incoming) {
    require_mutable();
    validate_local_selection(incoming);
}

void ChoiceGroup::written(const decltype(selected_index)&) {
    const int selected = selected_index.read();
    apply_selection(selected);
    if (is_alive()) publish_change(selected_index_changed_, selected);
}

void ChoiceGroup::write_bound_selected_index(const int index) {
    selected_index = index;
}
void ChoiceGroup::validate_selection(const int index) const {
    const std::size_t count = revision_ ? (*revision_).items.size() : 0U;
    if (index < -1 || (index >= 0 && static_cast<std::size_t>(index) >= count)) {
        throw std::out_of_range("ChoiceGroup selection is outside its items");
    }
}
void ChoiceGroup::validate_local_selection(const int index) const {
    validate_selection(index);
    if (binding_) (*binding_).validate_update(index);
}
void ChoiceGroup::bind(Value<int>& selection) {
    require_mutable();
    const Control::Ptr retained = shared_from_this();
    std::unique_ptr<detail::ScalarBinding<ChoiceGroup, int>> binding =
        std::make_unique<detail::ScalarBinding<ChoiceGroup, int>>(
            *this, selection, changed(), &ChoiceGroup::bound_selected_index,
            &ChoiceGroup::write_bound_selected_index, &ChoiceGroup::validate_selection);
    binding_ = std::move(binding);
    (*binding_).synchronize();
}
void ChoiceGroup::unbind() noexcept { binding_.reset(); }

void ChoiceGroup::apply_selection(const int selected) {
    const Control::Ptr retained = shared_from_this();
    const std::shared_ptr<Revision> revision = revision_;
    if (!revision) return;
    if (selected < -1 || (selected >= 0 && static_cast<std::size_t>(selected) >= (*revision).entries.size())) {
        throw std::out_of_range("ChoiceGroup selection is outside its items");
    }
    // With nothing selected the first item is the tab stop.
    const int tab_stop = selected < 0 ? 0 : selected;
    for (std::size_t index = 0U; index < (*revision).entries.size(); ++index) {
        Button& button = *(*(*revision).entries[index]).button;
        button.set_selected(static_cast<int>(index) == selected);
        button.tab_stop = static_cast<int>(index) == tab_stop;
        if (!is_alive() || revision_ != revision) return;
    }
    refresh_tab_stop();
    invalidate(Dirty::paint | Dirty::semantics);
}

void ChoiceGroup::set_items(std::vector<ChoiceItem> items) {
    require_mutable();
    if (replacing_) throw std::logic_error("ChoiceGroup cannot replace items recursively");
    if (items.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("too many ChoiceGroup items");
    }
    for (std::size_t index = 0U; index < items.size(); ++index) {
        const ChoiceItem& value = items[index];
        if (!validate_utf8(value.label).valid() || !validate_utf8(value.glyph).valid() ||
            !validate_utf8(value.accessible_name).valid()) throw std::invalid_argument("ChoiceGroup text must be UTF-8");
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (items[earlier].id == value.id) throw std::invalid_argument("duplicate ChoiceGroup item id");
        }
    }
    const Control::Ptr retained = shared_from_this();
    const std::shared_ptr<Revision> next = std::make_shared<Revision>();
    (*next).items = std::move(items);
    (*next).entries.reserve((*next).items.size());
    for (const ChoiceItem& value : (*next).items) {
        std::shared_ptr<Entry> entry{};
        if (revision_) {
            for (const std::shared_ptr<Entry>& candidate : (*revision_).entries) {
                if ((*candidate).id == value.id) { entry = candidate; break; }
            }
        }
        if (!entry) {
            entry = std::make_shared<Entry>();
            (*entry).id = value.id;
            std::string id(stable_id().value());
            id += ".item.";
            id += std::to_string(value.id);
            (*entry).button = make_control<ChoiceButton>(StableId(id));
            (*entry).click = (*(*entry).button).clicked().subscribe(*this, ItemClick{*this, value.id});
        }
        (*next).entries.push_back(std::move(entry));
    }
    replacing_ = true;
    try {
        const std::shared_ptr<Revision> previous = std::exchange(revision_, next);
        if (previous) {
            for (const std::shared_ptr<Entry>& entry : (*previous).entries) {
                const std::vector<std::shared_ptr<Entry>>::const_iterator found =
                    std::find((*next).entries.begin(), (*next).entries.end(), entry);
                if (found == (*next).entries.end()) {
                    (*entry).click.disconnect();
                    (*entry).enabled.disconnect();
                    (*entry).visible.disconnect();
                    (*entry).focus.disconnect();
                    static_cast<void>(remove_child((*(*entry).button).runtime_id()));
                }
            }
        }
        for (std::size_t index = 0U; index < (*next).items.size(); ++index) {
            const ChoiceItem& value = (*next).items[index];
            ChoiceButton& button = *(*(*next).entries[index]).button;
            const std::string text = value.glyph.empty() ? value.label : value.glyph + " " + value.label;
            button.text = text;
            button.set_segment(presentation.read() == ChoiceGroupPresentation::segmented);
            button.image = value.image;
            button.accessible_name = value.accessible_name.empty() ? value.label : value.accessible_name;
            button.tab_index = static_cast<std::uint32_t>(index);
            if (!button.parent()) add_child((*(*next).entries[index]).button);
        }
        for (std::size_t index = 0U; index < (*next).entries.size(); ++index) {
            const Button& button = *(*(*next).entries[index]).button;
            static_cast<void>(set_child_index(button.runtime_id(), (*next).entries.size() - index - 1U));
        }
        const int current = selected_index.read();
        if (current >= static_cast<int>((*next).items.size())) selected_index = -1;
        else apply_selection(current);
        invalidate(Dirty::layout | Dirty::paint | Dirty::semantics);
        for (const std::shared_ptr<Entry>& entry : (*next).entries) {
            Button& button = *(*entry).button;
            (*entry).enabled = button.enabled_changed().subscribe(*this, ItemAvailability{*this});
            (*entry).visible = button.visible_changed().subscribe(*this, ItemAvailability{*this});
            (*entry).focus = button.focus_observed().subscribe(*this, ItemAvailability{*this});
        }
        replacing_ = false;
        refresh_tab_stop();
    } catch (...) {
        replacing_ = false;
        throw;
    }
}

void ChoiceGroup::activate_item(const int id) {
    const Control::Ptr retained = shared_from_this();
    const std::shared_ptr<Revision> revision = revision_;
    if (!revision || replacing_) return;
    for (std::size_t index = 0U; index < (*revision).items.size(); ++index) {
        if ((*revision).items[index].id != id) continue;
        const int selected = static_cast<int>(index);
        selected_index = selected;
        if (is_alive() && revision_ == revision) activated_.emit(selected);
        return;
    }
}

Rect ChoiceGroup::segment_bounds(std::size_t index, std::size_t count, Size size) const noexcept {
    if (count == 0U) return Rect{};
    const double inner_width = std::max(0.0, size.width - 2.0 * segment_inset);
    const double inner_height = std::max(0.0, size.height - 2.0 * segment_inset);
    const double share = inner_width / static_cast<double>(count);
    const Window* owner = window();
    const double scale = owner != nullptr && (*owner).scale() > 0.0 ? (*owner).scale() : 1.0;
    // Edge i at round((inset + i * share) * scale) / scale: cumulative, so
    // one segment's right edge is the next one's left edge.
    const double left_edge = segment_inset + static_cast<double>(index) * share;
    const double right_edge = segment_inset + static_cast<double>(index + 1U) * share;
    const double left = std::round(left_edge * scale) / scale;
    const double right = std::round(right_edge * scale) / scale;
    const Rect segment{left, segment_inset, right - left, inner_height};
    return segment;
}

// Nothing while the group is not segmented or nothing is selected.
Rect ChoiceGroup::selection_bounds() const {
    const int selected = selected_index.read();
    if (presentation.read() != ChoiceGroupPresentation::segmented || !revision_ || selected < 0) return Rect{};
    const std::size_t count = (*revision_).entries.size();
    if (static_cast<std::size_t>(selected) >= count) return Rect{};
    const Rect client = client_rectangle();
    const Rect indicator = segment_bounds(static_cast<std::size_t>(selected), count, Size{client.width, client.height});
    return indicator;
}

// Buttons share the width equally across the whole height; segments share
// the track inside its inset.
void ChoiceGroup::arrange(const Rect bounds) {
    arrange_self(bounds);
    const std::shared_ptr<Revision> revision = revision_;
    if (!revision || (*revision).entries.empty()) return;
    const std::size_t count = (*revision).entries.size();
    const bool segmented = presentation.read() == ChoiceGroupPresentation::segmented;
    const Rect client = client_rectangle();
    const double width = client.width / static_cast<double>(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const Rect slot = segmented ? segment_bounds(index, count, Size{client.width, client.height})
                                    : Rect{static_cast<double>(index) * width, 0.0, width, client.height};
        set_child_layout((*(*revision).entries[index]).button, slot);
    }
}

// The segmented presentation draws the track over Panel's face, then the
// selection indicator behind the selected segment; the segments paint
// their labels over both.
void ChoiceGroup::on_paint(Painter& painter, Rect local_damage) {
    Panel::on_paint(painter, local_damage);
    if (presentation.read() != ChoiceGroupPresentation::segmented) return;
    const AnatomyStateSet states = anatomy_states();
    PartPaint track{.role = PartRole::track, .bounds = local_bounds(), .states = states,
                    .family = ControlVisualRole::choice};
    paint_part(painter, *this, track);
    const Rect indicator = selection_bounds();
    if (indicator.empty()) return;
    PartPaint selection{.role = PartRole::selection, .bounds = indicator, .states = states,
                        .family = ControlVisualRole::choice};
    selection.states.insert(AnatomyState::selected);
    // A keyed part, so that the engine may move the indicator between
    // segments (and does not when motion is reduced).
    selection.key = PartKey{"selection", {}, {}};
    paint_part(painter, *this, selection);
}
void ChoiceGroup::on_key_bubble(KeyEvent& event) { on_key(event); }
void ChoiceGroup::on_key(KeyEvent& event) {
    if (event.handled || event.action != KeyAction::down || !enabled.read() || !revision_) return;
    const bool backward = event.physical_key == PhysicalKey::left || event.physical_key == PhysicalKey::up;
    const bool forward = event.physical_key == PhysicalKey::right || event.physical_key == PhysicalKey::down;
    if (!backward && !forward) return;
    const int count = static_cast<int>((*revision_).items.size());
    if (count == 0) return;
    int next = std::max(0, selected_index.read());
    const std::shared_ptr<Revision> revision = revision_;
    for (int attempt = 0; attempt < count; ++attempt) {
        if (backward) next = next == 0 ? count - 1 : next - 1;
        else next = next + 1 == count ? 0 : next + 1;
        const std::shared_ptr<Button> button = (*(*revision).entries[static_cast<std::size_t>(next)]).button;
        if (!(*button).enabled.read() || !(*button).visible.read()) continue;
        const int id = (*revision).items[static_cast<std::size_t>(next)].id;
        event.handled = true;
        const Control::Ptr retained = shared_from_this();
        activate_item(id);
        if (is_alive() && revision_ == revision && window() != nullptr) {
            static_cast<void>((*window()).request_focus(button));
        }
        return;
    }
}
SemanticDescriptor ChoiceGroup::semantic_descriptor() const {
    SemanticDescriptor descriptor = Panel::semantic_descriptor();
    descriptor.role = SemanticRole::radio_group;
    descriptor.exposed = true;
    return descriptor;
}

namespace {

// The segmented presentation's parts after Panel's. The items are child
// controls, which publish their own anatomies.
constexpr std::array<PartField<ChoiceGroup::Parts>, 2> segmented_fields{{
    {.declaration = {.name = "track", .role = PartRole::track, .composed_from = {}},
     .member = &ChoiceGroup::Parts::track},
    {.declaration = {.name = "selection", .role = PartRole::selection, .composed_from = {}},
     .member = &ChoiceGroup::Parts::selection},
}};

constexpr std::array<PartDeclaration, panel_detail::parts.size() + 2U> choice_group_parts =
    joined_declarations(panel_detail::parts, part_declarations(segmented_fields));

// "segmented" names the segmented presentation for a theme's selectors
// (`:family(choice):state(segmented)`).
constexpr std::array<CustomStateDeclaration, 1> choice_group_custom_states{{
    {.name = "segmented"},
}};
constexpr CustomStateIndex segmented_state{0U};

constexpr ControlAnatomy choice_group_anatomy{
    .control_id = "choice_group",
    .parts = choice_group_parts,
    .states = scrollable_detail::states,
    .custom_states = choice_group_custom_states,
};

} // namespace

const ControlAnatomy& ChoiceGroup::anatomy() const noexcept {
    return choice_group_anatomy;
}

AnatomyStateSet ChoiceGroup::anatomy_states() const {
    AnatomyStateSet states = Panel::anatomy_states();
    if (presentation.read() == ChoiceGroupPresentation::segmented) states.insert(segmented_state);
    return states;
}

void ChoiceGroup::publish_part_geometry(std::span<PartGeometry> geometry) const {
    Panel::publish_part_geometry(geometry);
    Parts record;
    if (presentation.read() == ChoiceGroupPresentation::segmented) {
        record.track = PartGeometry::from_bounds(local_bounds());
        record.selection = PartGeometry::from_bounds(selection_bounds());
    }
    write_parts(anatomy(), segmented_fields, record, geometry);
}

ChoiceGroup::Parts ChoiceGroup::parts() const {
    Parts record;
    record.panel = Panel::parts();
    const PartGeometryBuffer published = published_part_geometry();
    read_parts(anatomy(), segmented_fields, published.parts(), record);
    return record;
}

void ChoiceGroup::prepare_write(const decltype(presentation)&, ChoiceGroupPresentation&) {
    require_mutable();
}

// Each item switches between button and segment painting; the items are
// arranged again into their new slots.
void ChoiceGroup::written(const decltype(presentation)&) {
    const bool segmented = presentation.read() == ChoiceGroupPresentation::segmented;
    const std::shared_ptr<Revision> revision = revision_;
    if (revision) {
        for (const std::shared_ptr<Entry>& entry : (*revision).entries) {
            (*(*entry).button).set_segment(segmented);
        }
    }
    invalidate(Dirty::layout | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

namespace {

// The events ChoiceGroup adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 2> choice_group_interactions{{
    {.name = "changed", .payload = "int", .mutable_payload = false},
    {.name = "activated", .payload = "int", .mutable_payload = false},
}};

} // namespace

void ChoiceGroup::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), choice_group_interactions.begin(), choice_group_interactions.end());
}

// property_codemod begin ChoiceGroup (tools/property_families/choice_group.json; generated, do not edit)

namespace {

PropertyDescriptor choice_group_presentation_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "presentation";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Appearance";
    descriptor.description = "How the choices are drawn: buttons or a segmented control.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"choice_group_presentation", "buttons", 0}};
    descriptor.invalidation_effects = Dirty::layout | Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.enumeration = choice_group_presentation_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_choice_group_presentation() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        choice_group_presentation_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

PropertyType PropertyValueTraits<ChoiceGroupPresentation>::type() {
    return PropertyType{BindingValueKind::enumeration, false, choice_group_presentation_enum()};
}

BindingValue PropertyValueTraits<ChoiceGroupPresentation>::to_binding(ChoiceGroupPresentation value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = choice_group_presentation_enum();
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{static_cast<std::int64_t>(value)}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}

ChoiceGroupPresentation PropertyValueTraits<ChoiceGroupPresentation>::from_binding(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<ChoiceGroupPresentation>(choice.value);
}

const std::array<PropertyEntry, ChoiceGroup::choice_group_property_count>& ChoiceGroup::choice_group_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 1U> own{{
        {"presentation", &describe_choice_group_presentation,
         &read_cell_entry<ChoiceGroup, decltype(&ChoiceGroup::presentation), &ChoiceGroup::presentation>,
         &write_cell_entry<ChoiceGroup, decltype(&ChoiceGroup::presentation), &ChoiceGroup::presentation>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "ChoiceGroup's property table names a property twice");
    static const std::array<PropertyEntry, choice_group_property_count> entries =
        join_property_tables(Panel::panel_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> ChoiceGroup::property_table() const noexcept {
    const std::array<PropertyEntry, choice_group_property_count>& entries = choice_group_property_entries();
    return std::span<const PropertyEntry>(entries);
}

// property_codemod end ChoiceGroup

} // namespace gui_forms
