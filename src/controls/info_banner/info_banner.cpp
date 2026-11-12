#include "gui_forms/controls/info_banner/info_banner.hpp"

#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"
#include "gui_forms/text/types/text_types.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms {
namespace {

// The NoticeSeverity enumeration, numbered as declared. Static storage;
// any thread may read it.
std::shared_ptr<const PropertyEnumDescriptor> notice_severity_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "notice_severity",
            {{"information", 0}, {"warning", 1}, {"success", 2}, {"error", 3}},
            false});
    return value;
}

// The banner's metrics, in logical units: the keyline's width along the
// start edge, the space inside the face around everything, the icon's and
// the close mark's side, the gap between neighbouring parts, and the space
// between the title's line and the message's.
constexpr double keyline_width = 3.0;
constexpr double inset = 10.0;
constexpr double icon_side = 16.0;
constexpr double close_side = 16.0;
constexpr double gap = 8.0;
constexpr double line_gap = 2.0;

PartMark severity_mark(NoticeSeverity severity) noexcept {
    switch (severity) {
    case NoticeSeverity::information: return PartMark::information;
    case NoticeSeverity::warning: return PartMark::warning;
    case NoticeSeverity::success: return PartMark::success;
    case NoticeSeverity::error: return PartMark::error;
    default: break;
    }
    return PartMark::information;
}

constexpr std::array<AnatomyState, 2> actuator_part_states{{AnatomyState::hot, AnatomyState::pressed}};

constexpr std::array<PartField<InfoBanner::Parts>, 8> info_banner_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &InfoBanner::Parts::region},
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &InfoBanner::Parts::face},
    {.declaration = {.name = "keyline", .role = PartRole::keyline, .composed_from = {}},
     .member = &InfoBanner::Parts::keyline},
    {.declaration = {.name = "icon", .role = PartRole::glyph, .composed_from = {}},
     .member = &InfoBanner::Parts::icon},
    {.declaration = {.name = "caption", .role = PartRole::caption, .composed_from = {}},
     .member = &InfoBanner::Parts::caption},
    {.declaration = {.name = "label", .role = PartRole::label, .composed_from = {}},
     .member = &InfoBanner::Parts::label},
    {.declaration = {.name = "actuator", .role = PartRole::glyph, .composed_from = {},
                     .states = actuator_part_states},
     .member = &InfoBanner::Parts::actuator},
    {.declaration = {.name = "content", .role = PartRole::content, .composed_from = {}},
     .member = &InfoBanner::Parts::content},
}};

constexpr std::array<PartDeclaration, 8> info_banner_parts = part_declarations(info_banner_part_fields);

constexpr std::array<AnatomyState, 5> info_banner_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled, AnatomyState::focused,
    AnatomyState::focus_visible,
};

// One custom state per severity, in NoticeSeverity's order, so a
// severity's number is its state's index (`:family(notice):state(error)`).
constexpr std::array<CustomStateDeclaration, 4> info_banner_custom_states{{
    {.name = "information"},
    {.name = "warning"},
    {.name = "success"},
    {.name = "error"},
}};

// The event InfoBanner adds to those of Control.
constexpr std::array<InteractionDeclaration, 1> info_banner_interactions{{
    {.name = "closed", .payload = "", .mutable_payload = false},
}};

constexpr ControlAnatomy info_banner_anatomy{
    .control_id = "info_banner",
    .parts = info_banner_parts,
    .states = info_banner_states,
    .custom_states = info_banner_custom_states,
};

} // namespace

InfoBanner::InfoBanner(StableId stable_id) : Control(std::move(stable_id)) {
    focusable = closable.read();
}

Control::Ptr InfoBanner::set_action(Control::Ptr control) {
    require_mutable();
    if (control && control == action_) return {};
    if (control && (*control).parent()) {
        throw std::logic_error("InfoBanner action control already has a parent");
    }
    Control::Ptr previous = action_;
    if (control) add_child(control);
    if (action_) previous = remove_child((*action_).runtime_id());
    action_ = std::move(control);
    action_size_ = Size{};
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    return previous;
}

void InfoBanner::close() {
    require_mutable();
    if (!visible.read()) return;
    if (pressed_close_) set_pointer_capture(false);
    hot_close_ = false;
    pressed_close_ = false;
    visible = false;
    publish_change(closed_);
}

FontSpec InfoBanner::title_font() const noexcept {
    FontSpec spec = effective_theme().structure().typography.control;
    spec.weight = 650;
    const FontSpec font = text_font(spec);
    return font;
}

FontSpec InfoBanner::message_font() const noexcept {
    const FontSpec font = text_font(effective_theme().structure().typography.control);
    return font;
}

InfoBanner::Placement InfoBanner::placement(Size size) const {
    Placement placed;
    placed.face = Rect{0.0, 0.0, size.width, size.height};
    placed.keyline = Rect{0.0, 0.0, keyline_width, size.height};
    const double icon_x = keyline_width + inset;
    placed.icon = Rect{icon_x, inset, icon_side, icon_side};
    const double text_x = icon_x + icon_side + gap;
    double trailing = size.width - inset;
    double trailing_natural = inset;
    double tallest = icon_side;
    if (closable.read()) {
        placed.actuator = Rect{trailing - close_side, inset, close_side, close_side};
        trailing = placed.actuator.x - gap;
        trailing_natural += close_side + gap;
    }
    if (action_ && action_size_.width > 0.0) {
        const double width = std::min(action_size_.width, std::max(0.0, trailing - text_x));
        placed.content = Rect{trailing - width, inset, width, action_size_.height};
        trailing = placed.content.x - gap;
        trailing_natural += action_size_.width + gap;
        tallest = std::max(tallest, action_size_.height);
    }
    const double text_width = std::max(0.0, trailing - text_x);
    const std::string& heading = title.read();
    const std::string& body = message.read();
    Size heading_size{};
    if (!heading.empty()) heading_size = measure_text_extent_utf8(heading, title_font()).logical_size;
    Size body_size{};
    if (!body.empty()) body_size = measure_text_extent_utf8(body, message_font()).logical_size;
    const double heading_height = std::ceil(heading_size.height);
    const double body_height = std::ceil(body_size.height);
    const double between = heading.empty() || body.empty() ? 0.0 : line_gap;
    if (!heading.empty()) placed.caption = Rect{text_x, inset, text_width, heading_height};
    if (!body.empty()) placed.label = Rect{text_x, inset + heading_height + between, text_width, body_height};
    tallest = std::max(tallest, heading_height + between + body_height);
    const double text_natural = std::ceil(std::max(heading_size.width, body_size.width));
    placed.natural = Size{text_x + text_natural + trailing_natural, inset + tallest + inset};
    return placed;
}

// As wide as the space offered (a banner spans its container), or its
// natural width where the space is unbounded; as tall as its tallest
// column. A requested size wins on either axis. The action is measured
// first, against half the width, so that the text keeps the rest.
Size InfoBanner::measure(Size available) {
    const Rect requested = requested_bounds();
    action_size_ = Size{};
    const Control::Ptr retained = action_;
    if (retained && is_current_layout_child(retained)) {
        const double offered = std::isfinite(available.width) ? available.width * 0.5 : available.width;
        const Size desired = (*retained).measure(Size{offered, available.height});
        if (!is_alive()) return Size{};
        if (action_ != retained || !is_current_layout_child(retained)) {
            invalidate(Dirty::measure | Dirty::arrange);
        } else {
            action_size_ = desired;
        }
    }
    const Placement placed = placement(Size{std::isfinite(available.width) ? available.width : 0.0, 0.0});
    double width = std::isfinite(available.width) ? available.width : placed.natural.width;
    double height = placed.natural.height;
    if (requested.width > 0.0) width = requested.width;
    if (requested.height > 0.0) height = requested.height;
    const Size measured{std::min(available.width, width), std::min(available.height, height)};
    return measured;
}

// The action is measured again against the arranged width, since a parent
// that places the banner by its requested bounds never measures it.
void InfoBanner::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    const Control::Ptr retained = action_;
    if (!retained || !is_current_layout_child(retained)) return;
    const Rect client = client_rectangle();
    const Size desired = (*retained).measure(Size{client.width * 0.5, client.height});
    if (!is_alive()) return;
    if (action_ != retained || !is_current_layout_child(retained)) {
        invalidate(Dirty::measure | Dirty::arrange);
        return;
    }
    action_size_ = desired;
    const Placement placed = placement(Size{client.width, client.height});
    set_child_layout(retained, placed.content);
}

AnatomyStateSet InfoBanner::actuator_states() const {
    AnatomyStateSet states = anatomy_states();
    if (hot_close_) states.insert(AnatomyState::hot);
    if (pressed_close_) states.insert(AnatomyState::pressed);
    return states;
}

void InfoBanner::on_paint(Painter& painter, Rect) {
    const Rect client = client_rectangle();
    const Placement placed = placement(Size{client.width, client.height});
    const AnatomyStateSet states = anatomy_states();
    const NoticeSeverity level = severity.read();
    const Color tone = notice_severity_colour(level);
    PartPaint face{.role = PartRole::face, .bounds = placed.face, .states = states, .family = ControlVisualRole::notice};
    paint_part(painter, *this, face);
    PartPaint keyline{.role = PartRole::keyline, .bounds = placed.keyline, .states = states,
                      .family = ControlVisualRole::notice};
    keyline.authored_foreground = tone;
    paint_part(painter, *this, keyline);
    PartPaint icon{.role = PartRole::glyph, .bounds = placed.icon, .states = states, .family = ControlVisualRole::notice};
    icon.mark = severity_mark(level);
    icon.authored_foreground = tone;
    paint_part(painter, *this, icon);
    const std::string& heading = title.read();
    if (!heading.empty()) {
        PartPaint caption{.role = PartRole::caption, .bounds = placed.caption, .states = states,
                          .family = ControlVisualRole::notice};
        caption.text = heading;
        caption.font = title_font();
        paint_part(painter, *this, caption);
    }
    const std::string& body = message.read();
    if (!body.empty()) {
        PartPaint label{.role = PartRole::label, .bounds = placed.label, .states = states,
                        .family = ControlVisualRole::notice};
        label.text = body;
        label.font = message_font();
        paint_part(painter, *this, label);
    }
    if (closable.read()) {
        PartPaint actuator{.role = PartRole::glyph, .bounds = placed.actuator, .states = actuator_states(),
                           .family = ControlVisualRole::notice};
        actuator.mark = PartMark::close;
        paint_part(painter, *this, actuator);
    }
}

// The close mark acts on release over it, as a button does; moving off it
// while pressed keeps the capture, and a release elsewhere closes nothing.
void InfoBanner::on_pointer(PointerEvent& event) {
    if (!eligible_for_input() || !closable.read()) return;
    const Rect client = client_rectangle();
    const Placement placed = placement(Size{client.width, client.height});
    const bool over = placed.actuator.contains(event.position);
    if (event.action == PointerAction::move || event.action == PointerAction::leave) {
        const bool hot = event.action == PointerAction::move && over;
        if (hot != hot_close_) {
            hot_close_ = hot;
            invalidate(Dirty::paint);
        }
        return;
    }
    if (event.button != PointerButton::primary) return;
    if (event.action == PointerAction::down && over) {
        pressed_close_ = true;
        set_pointer_capture(true);
        invalidate(Dirty::paint);
        event.handled = true;
        return;
    }
    if (event.action == PointerAction::up && pressed_close_) {
        pressed_close_ = false;
        set_pointer_capture(false);
        invalidate(Dirty::paint);
        event.handled = true;
        if (over) close();
    }
}

void InfoBanner::on_key(KeyEvent& event) {
    if (!closable.read() || event.action != KeyAction::down) return;
    if (event.physical_key != PhysicalKey::escape) return;
    event.handled = true;
    close();
}

SemanticDescriptor InfoBanner::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::live_region;
    descriptor.name = accessible_name.read().empty() ? title.read() : accessible_name.read();
    descriptor.description = accessible_description.read().empty() ? message.read() : accessible_description.read();
    if (closable.read()) descriptor.actions = {SemanticAction::close};
    descriptor.exposed = true;
    return descriptor;
}

bool InfoBanner::on_semantic_action(SemanticAction action, std::string_view value) {
    if (action == SemanticAction::close && closable.read() && effectively_enabled()) {
        close();
        return true;
    }
    const bool handled = Control::on_semantic_action(action, value);
    return handled;
}

const ControlAnatomy& InfoBanner::anatomy() const noexcept {
    return info_banner_anatomy;
}

AnatomyStateSet InfoBanner::anatomy_states() const {
    AnatomyStateSet states = base_anatomy_states();
    states.insert(CustomStateIndex{static_cast<std::uint8_t>(severity.read())});
    return states;
}

void InfoBanner::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect client = client_rectangle();
    const Placement placed = placement(Size{client.width, client.height});
    Parts record;
    record.region = PartGeometry::from_bounds(client);
    record.face = PartGeometry::from_bounds(placed.face);
    record.keyline = PartGeometry::from_bounds(placed.keyline);
    record.icon = PartGeometry::from_bounds(placed.icon);
    if (!title.read().empty()) record.caption = PartGeometry::from_bounds(placed.caption);
    if (!message.read().empty()) record.label = PartGeometry::from_bounds(placed.label);
    if (closable.read()) {
        record.actuator = PartGeometry::from_bounds(placed.actuator);
        if (hot_close_) record.actuator.states.insert(AnatomyState::hot);
        if (pressed_close_) record.actuator.states.insert(AnatomyState::pressed);
    }
    if (action_) record.content = PartGeometry::from_bounds(placed.content);
    write_parts(anatomy(), info_banner_part_fields, record, geometry);
}

void InfoBanner::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), info_banner_interactions.begin(), info_banner_interactions.end());
}

InfoBanner::Parts InfoBanner::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), info_banner_part_fields, published.parts(), record);
    return record;
}

void InfoBanner::prepare_write(const decltype(closable)&, bool&) {
    require_mutable();
}

// A banner that stops being closable gives up focus and drops the close
// mark's hot and pressed visuals (and a pointer capture it held).
void InfoBanner::written(const decltype(closable)&) {
    focusable = closable.read();
    if (pressed_close_) set_pointer_capture(false);
    hot_close_ = false;
    pressed_close_ = false;
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// property_codemod begin InfoBanner (tools/property_families/info_banner.json; generated, do not edit)

namespace {

PropertyDescriptor info_banner_severity_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "severity";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Appearance";
    descriptor.description = "How much the message matters.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"notice_severity", "information", 0}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.enumeration = notice_severity_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_info_banner_severity() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        info_banner_severity_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor info_banner_title_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "title";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The banner's title.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_info_banner_title() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        info_banner_title_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor info_banner_message_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "message";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The banner's message.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_info_banner_message() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        info_banner_message_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor info_banner_closable_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "closable";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Behavior";
    descriptor.description = "Whether the banner can be closed by the user.";
    descriptor.default_value = BindingValue{true};
    descriptor.invalidation_effects = Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_info_banner_closable() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        info_banner_closable_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

PropertyType PropertyValueTraits<NoticeSeverity>::type() {
    return PropertyType{BindingValueKind::enumeration, false, notice_severity_enum()};
}

BindingValue PropertyValueTraits<NoticeSeverity>::to_binding(NoticeSeverity value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = notice_severity_enum();
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{static_cast<std::int64_t>(value)}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}

NoticeSeverity PropertyValueTraits<NoticeSeverity>::from_binding(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<NoticeSeverity>(choice.value);
}

const std::array<PropertyEntry, InfoBanner::info_banner_property_count>& InfoBanner::info_banner_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 4U> own{{
        {"severity", &describe_info_banner_severity,
         &read_cell_entry<InfoBanner, decltype(&InfoBanner::severity), &InfoBanner::severity>,
         &write_cell_entry<InfoBanner, decltype(&InfoBanner::severity), &InfoBanner::severity>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"title", &describe_info_banner_title,
         &read_cell_entry<InfoBanner, decltype(&InfoBanner::title), &InfoBanner::title>,
         &write_cell_entry<InfoBanner, decltype(&InfoBanner::title), &InfoBanner::title>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"message", &describe_info_banner_message,
         &read_cell_entry<InfoBanner, decltype(&InfoBanner::message), &InfoBanner::message>,
         &write_cell_entry<InfoBanner, decltype(&InfoBanner::message), &InfoBanner::message>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"closable", &describe_info_banner_closable,
         &read_cell_entry<InfoBanner, decltype(&InfoBanner::closable), &InfoBanner::closable>,
         &write_cell_entry<InfoBanner, decltype(&InfoBanner::closable), &InfoBanner::closable>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "InfoBanner's property table names a property twice");
    static const std::array<PropertyEntry, info_banner_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> InfoBanner::property_table() const noexcept {
    const std::array<PropertyEntry, info_banner_property_count>& entries = info_banner_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void InfoBanner::prepare_write(const decltype(severity)&, NoticeSeverity&) {
    require_mutable();
}

void InfoBanner::written(const decltype(severity)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

void InfoBanner::prepare_write(const decltype(title)&, std::string& incoming) {
    require_mutable();
    if (!validate_utf8(incoming).valid()) {
        throw std::invalid_argument("InfoBanner title must be valid UTF-8");
    }
}

void InfoBanner::written(const decltype(title)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void InfoBanner::prepare_write(const decltype(message)&, std::string& incoming) {
    require_mutable();
    if (!validate_utf8(incoming).valid()) {
        throw std::invalid_argument("InfoBanner message must be valid UTF-8");
    }
}

void InfoBanner::written(const decltype(message)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

// property_codemod end InfoBanner

} // namespace gui_forms
