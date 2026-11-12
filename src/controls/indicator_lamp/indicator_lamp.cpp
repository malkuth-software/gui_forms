#include "gui_forms/controls/indicator_lamp.hpp"

#include "gui_forms/engine/light_planes/light_planes.hpp"
#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/text/types/text_types.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace gui_forms {
namespace {

// The IndicatorLampState enumeration, numbered as declared. Static storage;
// any thread may read it.
std::shared_ptr<const PropertyEnumDescriptor> indicator_lamp_state_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "indicator_lamp_state", {{"off", 0}, {"on", 1}, {"error", 2}}, false});
    return value;
}

// The lamp's metrics, in logical units: the bezel's side beside a legend,
// the bezel's ring around the lens, and the gap before the legend.
constexpr double legend_bezel_side = 16.0;
constexpr double bezel_ring = 2.0;
constexpr double legend_gap = 6.0;

constexpr std::array<PartField<IndicatorLamp::Parts>, 4> indicator_lamp_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &IndicatorLamp::Parts::region},
    {.declaration = {.name = "bezel", .role = PartRole::well, .composed_from = {}},
     .member = &IndicatorLamp::Parts::bezel},
    {.declaration = {.name = "lens", .role = PartRole::indicator, .composed_from = {}},
     .member = &IndicatorLamp::Parts::lens},
    {.declaration = {.name = "label", .role = PartRole::label, .composed_from = {}},
     .member = &IndicatorLamp::Parts::label},
}};

constexpr std::array<PartDeclaration, 4> indicator_lamp_parts = part_declarations(indicator_lamp_part_fields);

constexpr std::array<AnatomyState, 3> indicator_lamp_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
};

// "on" and "error" name the lit states for a theme's selectors
// (`indicator-lamp::part(lens):state(on)`); off is neither.
constexpr std::array<CustomStateDeclaration, 2> indicator_lamp_custom_states{{
    {.name = "on"},
    {.name = "error"},
}};
constexpr CustomStateIndex on_state{0U};
constexpr CustomStateIndex error_state{1U};

constexpr ControlAnatomy indicator_lamp_anatomy{
    .control_id = "indicator_lamp",
    .parts = indicator_lamp_parts,
    .states = indicator_lamp_states,
    .custom_states = indicator_lamp_custom_states,
};

// Whether `state` lights the lens.
constexpr bool lit(IndicatorLampState state) noexcept {
    const bool shown = state != IndicatorLampState::off;
    return shown;
}

// `states` with the custom state of a lit `state` added; off adds none.
AnatomyStateSet with_lit_state(AnatomyStateSet states, IndicatorLampState state) noexcept {
    if (state == IndicatorLampState::on) states.insert(on_state);
    if (state == IndicatorLampState::error) states.insert(error_state);
    return states;
}

// The family a theme without rules for the lens draws the lit lens in: a
// progress fill for on and a notice for error, so that every theme tells
// them from the dark lens.
ControlVisualRole lit_family(IndicatorLampState state) noexcept {
    const ControlVisualRole family =
        state == IndicatorLampState::error ? ControlVisualRole::notice : ControlVisualRole::progress;
    return family;
}

// The largest rectangle of `bake`'s aspect inside `box`: at its start (its
// end when `mirrored`) and centred across, its top on a whole unit. Empty
// at the box's origin for an empty bake or box.
Rect bake_rect(const engine_light::LightPlanes& bake, Rect box, bool mirrored) noexcept {
    if (bake.width == 0U || bake.height == 0U || !(box.width > 0.0) || !(box.height > 0.0)) {
        return Rect{box.x, box.y, 0.0, 0.0};
    }
    const double aspect = static_cast<double>(bake.height) / static_cast<double>(bake.width);
    double width = box.width;
    double height = width * aspect;
    if (height > box.height) {
        height = box.height;
        width = height / aspect;
    }
    const double left = mirrored ? box.x + box.width - width : box.x;
    const double top = box.y + std::floor((box.height - height) * 0.5);
    const Rect fitted{left, top, width, height};
    return fitted;
}

} // namespace

IndicatorLamp::IndicatorLamp(StableId stable_id, std::string initial_text) : Control(std::move(stable_id)) {
    text = std::move(initial_text);
}

std::string_view IndicatorLamp::state_name(IndicatorLampState value) noexcept {
    switch (value) {
    case IndicatorLampState::off: return "off";
    case IndicatorLampState::on: return "on";
    case IndicatorLampState::error: return "error";
    }
    // A value outside the enumeration reads as off, as it is drawn.
    return "off";
}

bool IndicatorLamp::changing() const noexcept {
    const bool running = change_timer_.started();
    return running;
}

std::shared_ptr<const engine_light::LightPlanes> IndicatorLamp::shown_bake() const {
    const std::string& name = light_planes.read();
    if (name.empty()) return nullptr;
    const SurfaceRoot* const root = attached_surface_root();
    if (root == nullptr) return nullptr;
    if (part_visual_context(*this, base_anatomy_states()).high_contrast) return nullptr;
    const std::shared_ptr<ApplicationResources> resources = (*root).resources();
    if (!resources) return nullptr;
    std::shared_ptr<const engine_light::LightPlanes> bake = (*resources).lend_light_planes(name);
    return bake;
}

bool IndicatorLamp::light_planes_shown() const {
    const bool shown = shown_bake() != nullptr;
    return shown;
}

engine_light::LampColour IndicatorLamp::lit_colour() const noexcept {
    if (lit_state_ == IndicatorLampState::error) return engine_light::lamp_red;
    const std::optional<Color> chosen = lamp_color.read();
    if (chosen) return engine_light::lamp_colour_from_srgb(*chosen);
    return engine_light::lamp_amber;
}

FontSpec IndicatorLamp::legend_font() const noexcept {
    const FontSpec font = text_font(effective_theme().structure().typography.control);
    return font;
}

bool IndicatorLamp::reduced_motion() const noexcept {
    const Window* const owner = attached_window();
    const bool reduced = owner != nullptr && (*owner).presentation_settings().reduced_motion;
    return reduced;
}

// A lens alone fills the shorter side of the client area; beside a legend
// the bezel is legend_bezel_side, or the client height when that is less.
// A baked lens is the bake's aspect fitted in the client area without a
// legend, else in the bezel's square, and the bezel is the same rectangle.
IndicatorLamp::Placement IndicatorLamp::placement(const engine_light::LightPlanes* bake) const {
    const Rect client = client_rectangle();
    Placement placed;
    const bool legend = !text.read().empty();
    const double shorter = std::min(client.width, client.height);
    const double side = std::max(0.0, std::floor(legend ? std::min(client.height, legend_bezel_side) : shorter));
    const double top = client.y + std::floor((client.height - side) * 0.5);
    const bool mirrored = effective_layout_direction() == LayoutDirection::right_to_left;
    const double left = mirrored ? client.x + client.width - side : client.x;
    placed.bezel = Rect{left, top, side, side};
    const double ring = std::min(bezel_ring, side * 0.25);
    placed.lens = Rect{left + ring, top + ring, std::max(0.0, side - 2.0 * ring), std::max(0.0, side - 2.0 * ring)};
    if (bake != nullptr) {
        const Rect box = legend ? placed.bezel : client;
        placed.lens = bake_rect(*bake, box, mirrored);
        placed.bezel = placed.lens;
    }
    if (!legend) return placed;
    const double label_width = std::max(0.0, client.width - side - legend_gap);
    const double label_left = mirrored ? client.x : client.x + side + legend_gap;
    placed.label = Rect{label_left, client.y, label_width, client.height};
    return placed;
}

// The bezel and the legend's measured line, or the requested size where one
// is set.
Size IndicatorLamp::measure(Size available) {
    const Rect requested = requested_bounds();
    double width = legend_bezel_side;
    double height = legend_bezel_side;
    if (!text.read().empty()) {
        const ResolvedTextLayout layout = resolve_text_layout_utf8(text.read(), legend_font());
        width += legend_gap + std::ceil(layout.logical_size.width);
        height = std::max(height, std::ceil(layout.logical_size.height));
    }
    const double chosen_width = requested.width > 0.0 ? requested.width : width;
    const double chosen_height = requested.height > 0.0 ? requested.height : height;
    const Size measured{std::min(available.width, chosen_width), std::min(available.height, chosen_height)};
    return measured;
}

void IndicatorLamp::on_paint(Painter& painter, Rect) {
    change_timer_.note_painted();
    std::shared_ptr<const engine_light::LightPlanes> bake = shown_bake();
    const Placement placed = placement(bake.get());
    const AnatomyStateSet states = base_anatomy_states();
    if (bake) {
        paint_baked_lens(painter, placed, states, std::move(bake));
    } else {
        paint_theme_lens(painter, placed, states);
    }
    if (text.read().empty()) return;
    PartPaint label{.role = PartRole::label, .bounds = placed.label, .states = with_lit_state(states, state.read()),
                    .family = ControlVisualRole::panel};
    label.text = lend_paint_text(text.read());
    label.font = legend_font();
    label.alignment = effective_layout_direction() == LayoutDirection::right_to_left ? PartTextAlignment::end
                                                                                     : PartTextAlignment::start;
    paint_part(painter, *this, label);
}

void IndicatorLamp::paint_theme_lens(Painter& painter, const Placement& placed, AnatomyStateSet states) {
    PartPaint bezel{.role = PartRole::well, .bounds = placed.bezel, .states = with_lit_state(states, state.read()),
                    .family = ControlVisualRole::panel};
    bezel.corner_radius = placed.bezel.width * 0.5;
    bezel.key = PartKey{"bezel", {}, {}};
    paint_part(painter, *this, bezel);
    // The dark lens, then the lit lens over it at the current power, clipped
    // to the lens so that its light reaches nothing else.
    const double radius = placed.lens.width * 0.5;
    PartPaint lens{.role = PartRole::indicator, .bounds = placed.lens, .states = states,
                   .family = ControlVisualRole::choice};
    lens.corner_radius = radius;
    lens.key = PartKey{"lens", {}, {}};
    paint_part(painter, *this, lens);
    if (power_ > 0.0 && lit(lit_state_)) {
        PartPaint lit_lens = lens;
        lit_lens.states = with_lit_state(states, lit_state_);
        lit_lens.family = lit_family(lit_state_);
        // The lit lens keeps its own record, so the dark lens's stays.
        lit_lens.key = PartKey{"lens", "lit", {}};
        // The application's lamp colour fills the lit lens (on only: an
        // error keeps the theme's error colour).
        const std::optional<Color> chosen = lamp_color.read();
        if (chosen && lit_state_ == IndicatorLampState::on) lit_lens.authored_background = *chosen;
        painter.begin_group(PaintGroup{.bounds = placed.lens, .opacity = std::min(1.0, power_)});
        painter.save();
        painter.clip_rounded_rect(placed.lens, radius);
        paint_part(painter, *this, lit_lens);
        painter.restore();
        painter.end_group();
    }
}

// One part, the lens, lit by the bake at the current power (0 shows the
// lamp dark in the room's light). It carries the lit states and family
// while lit, so that the recipe that stands in for a bake that cannot be
// shown (no raster of its size) still tells lit from dark.
void IndicatorLamp::paint_baked_lens(Painter& painter, const Placement& placed, AnatomyStateSet states,
                                     std::shared_ptr<const engine_light::LightPlanes> bake) {
    const bool lit_now = power_ > 0.0 && lit(lit_state_);
    PartPaint lens{.role = PartRole::indicator, .bounds = placed.lens,
                   .states = lit_now ? with_lit_state(states, lit_state_) : states,
                   .family = lit_now ? lit_family(lit_state_) : ControlVisualRole::choice};
    lens.key = PartKey{"lens", {}, {}};
    const double power = lit_now ? std::min(1.0, power_) : 0.0;
    lens.light_planes = PartLightPlanes{.planes = std::move(bake),
                                        .drive = engine_light::LampDrive{lit_colour(), power},
                                        .raster = &baked_lens_};
    paint_part(painter, *this, lens);
}

// A lamp takes no input: the pointer reaches whatever lies beneath it.
bool IndicatorLamp::hit_test_local(Point) const {
    return false;
}

SemanticDescriptor IndicatorLamp::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::static_text;
    descriptor.name = accessible_name.read().empty() ? text.read() : accessible_name.read();
    descriptor.value = std::string(state_name(state.read()));
    descriptor.description = accessible_description.read();
    descriptor.exposed = true;
    return descriptor;
}

const ControlAnatomy& IndicatorLamp::anatomy() const noexcept {
    return indicator_lamp_anatomy;
}

AnatomyStateSet IndicatorLamp::anatomy_states() const {
    const AnatomyStateSet states = with_lit_state(base_anatomy_states(), state.read());
    return states;
}

void IndicatorLamp::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const std::shared_ptr<const engine_light::LightPlanes> bake = shown_bake();
    const Placement placed = placement(bake.get());
    Parts record;
    record.region = PartGeometry::from_bounds(client_rectangle());
    record.bezel = PartGeometry::from_bounds(placed.bezel);
    record.lens = PartGeometry::from_bounds(placed.lens);
    if (!text.read().empty()) record.label = PartGeometry::from_bounds(placed.label);
    write_parts(anatomy(), indicator_lamp_part_fields, record, geometry);
}

IndicatorLamp::Parts IndicatorLamp::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), indicator_lamp_part_fields, published.parts(), record);
    return record;
}

void IndicatorLamp::begin_change() {
    const IndicatorLampState shown = state.read();
    changed_at_.reset();
    change_timer_.stop();
    cooling_from_ = power_;
    if (lit(shown)) lit_state_ = shown;
    const bool on = lit(shown);
    const bool at_once = reduced_motion() || attached_window() == nullptr || (!on && !(power_ > 0.0));
    if (at_once) {
        power_ = engine_light::lamp_power(on, engine_light::lamp_strike_seconds, true);
        return;
    }
    // The strike's first frame, or the power the cool-down starts at.
    if (on) power_ = engine_light::lamp_power(true, 0.0, false);
    const std::shared_ptr<IndicatorLamp> self = std::dynamic_pointer_cast<IndicatorLamp>(shared_from_this());
    if (!self) return;
    change_timer_.start(*this, display_frame_interval, ChangeTick{self});
}

AnimationTickResult IndicatorLamp::advance(FrameTime now) {
    if (!changed_at_) changed_at_ = now;
    const double seconds = std::chrono::duration<double>(now - *changed_at_).count();
    const bool reduced = reduced_motion();
    const bool on = lit(state.read());
    power_ = on ? engine_light::lamp_power(true, seconds, reduced)
                : engine_light::lamp_cooling_power(cooling_from_, seconds, reduced);
    invalidate(Dirty::paint);
    const bool ended = on ? engine_light::lamp_settled(seconds) : engine_light::lamp_cooled(seconds);
    if (ended || reduced) change_timer_.stop();
    return AnimationTickResult::damaged;
}

AnimationTickResult IndicatorLamp::ChangeTick::operator()(FrameTime now) const {
    const std::shared_ptr<IndicatorLamp> live = lamp.lock();
    if (!live) return AnimationTickResult::unchanged;
    const AnimationTickResult result = (*live).advance(now);
    return result;
}

void IndicatorLamp::on_detached_from_window() noexcept {
    Control::on_detached_from_window();
    change_timer_.stop();
    baked_lens_.release();
    // A lamp warming or cooling while away shows its end state when shown
    // again.
    power_ = engine_light::lamp_power(lit(state.read()), engine_light::lamp_strike_seconds, true);
}

void IndicatorLamp::prepare_write(const decltype(state)&, IndicatorLampState& incoming) {
    require_mutable();
    const bool known = incoming == IndicatorLampState::off || incoming == IndicatorLampState::on ||
                       incoming == IndicatorLampState::error;
    if (!known) throw std::invalid_argument("IndicatorLamp state is outside IndicatorLampState");
}

void IndicatorLamp::written(const decltype(state)&) {
    begin_change();
    invalidate(Dirty::paint | Dirty::semantics);
}

// property_codemod begin IndicatorLamp (tools/property_families/indicator_lamp.json; generated, do not edit)

namespace {

PropertyDescriptor indicator_lamp_state_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "state";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Appearance";
    descriptor.description = "Whether the lamp is off, on or showing an error.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"indicator_lamp_state", "off", 0}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.enumeration = indicator_lamp_state_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_indicator_lamp_state() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        indicator_lamp_state_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor indicator_lamp_text_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "text";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The legend beside the lens.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_indicator_lamp_text() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        indicator_lamp_text_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor indicator_lamp_light_planes_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "light_planes";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The baked light planes the lens is composited from, by name.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::paint;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_indicator_lamp_light_planes() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        indicator_lamp_light_planes_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor indicator_lamp_lamp_color_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "lamp_color";
    descriptor.kind = BindingValueKind::color;
    descriptor.category = "Appearance";
    descriptor.description = "The colour the lens lights in while on.";
    descriptor.default_value = BindingValue{};
    descriptor.invalidation_effects = Dirty::paint;
    descriptor.nullable = true;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_indicator_lamp_lamp_color() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        indicator_lamp_lamp_color_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

PropertyType PropertyValueTraits<IndicatorLampState>::type() {
    return PropertyType{BindingValueKind::enumeration, false, indicator_lamp_state_enum()};
}

BindingValue PropertyValueTraits<IndicatorLampState>::to_binding(IndicatorLampState value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = indicator_lamp_state_enum();
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{static_cast<std::int64_t>(value)}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}

IndicatorLampState PropertyValueTraits<IndicatorLampState>::from_binding(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<IndicatorLampState>(choice.value);
}

const std::array<PropertyEntry, IndicatorLamp::indicator_lamp_property_count>& IndicatorLamp::indicator_lamp_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 4U> own{{
        {"state", &describe_indicator_lamp_state,
         &read_cell_entry<IndicatorLamp, decltype(&IndicatorLamp::state), &IndicatorLamp::state>,
         &write_cell_entry<IndicatorLamp, decltype(&IndicatorLamp::state), &IndicatorLamp::state>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"text", &describe_indicator_lamp_text,
         &read_cell_entry<IndicatorLamp, decltype(&IndicatorLamp::text), &IndicatorLamp::text>,
         &write_cell_entry<IndicatorLamp, decltype(&IndicatorLamp::text), &IndicatorLamp::text>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"light_planes", &describe_indicator_lamp_light_planes,
         &read_cell_entry<IndicatorLamp, decltype(&IndicatorLamp::light_planes), &IndicatorLamp::light_planes>,
         &write_cell_entry<IndicatorLamp, decltype(&IndicatorLamp::light_planes), &IndicatorLamp::light_planes>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"lamp_color", &describe_indicator_lamp_lamp_color,
         &read_cell_entry<IndicatorLamp, decltype(&IndicatorLamp::lamp_color), &IndicatorLamp::lamp_color>,
         &write_cell_entry<IndicatorLamp, decltype(&IndicatorLamp::lamp_color), &IndicatorLamp::lamp_color>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "IndicatorLamp's property table names a property twice");
    static const std::array<PropertyEntry, indicator_lamp_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> IndicatorLamp::property_table() const noexcept {
    const std::array<PropertyEntry, indicator_lamp_property_count>& entries = indicator_lamp_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void IndicatorLamp::prepare_write(const decltype(text)&, std::string& incoming) {
    require_mutable();
    if (!validate_utf8(incoming).valid()) {
        throw std::invalid_argument("IndicatorLamp text must be valid UTF-8");
    }
}

void IndicatorLamp::written(const decltype(text)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void IndicatorLamp::prepare_write(const decltype(light_planes)&, std::string& incoming) {
    require_mutable();
    if (!incoming.empty() && !engine_light::light_plane_name_valid(incoming)) {
        throw std::invalid_argument("IndicatorLamp light_planes must name a bake: lower-case letters, digits and '_'");
    }
}

void IndicatorLamp::written(const decltype(light_planes)&) {
    invalidate(Dirty::measure | Dirty::paint);
}

void IndicatorLamp::prepare_write(const decltype(lamp_color)&, std::optional<Color>&) {
    require_mutable();
}

void IndicatorLamp::written(const decltype(lamp_color)&) {
    invalidate(Dirty::paint);
}

// property_codemod end IndicatorLamp

} // namespace gui_forms
