// The window capsule control (gui_forms/controls/window_capsule.hpp): its
// anatomy, its drawing from theme parts, its own gesture's input when
// interactive, and its semantics.
#include "gui_forms/controls/window_capsule.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms {
namespace {

constexpr std::array<PartField<WindowCapsule::Parts>, 8> window_capsule_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}, .states = {}},
     .member = &WindowCapsule::Parts::region},
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}, .states = {}},
     .member = &WindowCapsule::Parts::face},
    {.declaration = {.name = "left_half", .role = PartRole::fill, .composed_from = {}, .states = {}},
     .member = &WindowCapsule::Parts::left_half},
    {.declaration = {.name = "help", .role = PartRole::face, .composed_from = {}, .states = {}},
     .member = &WindowCapsule::Parts::help},
    {.declaration = {.name = "right_half", .role = PartRole::fill, .composed_from = {}, .states = {}},
     .member = &WindowCapsule::Parts::right_half},
    {.declaration = {.name = "glyph", .role = PartRole::glyph, .composed_from = {}, .states = {}},
     .member = &WindowCapsule::Parts::glyph},
    {.declaration = {.name = "label", .role = PartRole::label, .composed_from = {}, .states = {}},
     .member = &WindowCapsule::Parts::label},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}, .states = {}},
     .member = &WindowCapsule::Parts::focus},
}};

constexpr std::array<AnatomyState, 7> window_capsule_states{{
    AnatomyState::normal,
    AnatomyState::hot,
    AnatomyState::pressed,
    AnatomyState::enabled,
    AnatomyState::disabled,
    AnatomyState::focused,
    AnatomyState::focus_visible,
}};

constexpr std::array<PartDeclaration, 8> window_capsule_parts = part_declarations(window_capsule_part_fields);

constexpr ControlAnatomy window_capsule_anatomy{
    .control_id = "window_capsule",
    .parts = window_capsule_parts,
    .states = window_capsule_states,
    .custom_states = {},
    .item_systems = {},
};

// The events WindowCapsule adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 1> window_capsule_interactions{{
    {.name = "requested", .payload = "WindowCapsuleRequest", .mutable_payload = false},
}};

// The capsule's resting size (the window manager's default theme).
constexpr Size resting_size{76.0, 20.0};

// `from` toward `to` by `amount` (0 to 1), at `alpha` (0 to 1) of `to`'s
// opacity.
Color mixed_color(Color from, Color to, double amount, double alpha) noexcept {
    const double t = std::clamp(amount, 0.0, 1.0);
    const double a = std::clamp(alpha, 0.0, 1.0);
    const double red = static_cast<double>(from.red) * (1.0 - t) + static_cast<double>(to.red) * t;
    const double green = static_cast<double>(from.green) * (1.0 - t) + static_cast<double>(to.green) * t;
    const double blue = static_cast<double>(from.blue) * (1.0 - t) + static_cast<double>(to.blue) * t;
    const double opacity = static_cast<double>(to.alpha) * a;
    const Color color{static_cast<std::uint8_t>(std::lround(red)), static_cast<std::uint8_t>(std::lround(green)),
                      static_cast<std::uint8_t>(std::lround(blue)), static_cast<std::uint8_t>(std::lround(opacity))};
    return color;
}

// A centred square of `fraction` of `area`'s shorter side.
Rect mark_box(Rect area, double fraction) noexcept {
    const double side = std::max(0.0, std::min(area.width, area.height) * fraction);
    const Rect box{area.x + (area.width - side) * 0.5, area.y + (area.height - side) * 0.5, side, side};
    return box;
}

// The child identities of the three sections.
constexpr std::string_view left_suffix = ".left-half";
constexpr std::string_view help_suffix = ".help";
constexpr std::string_view right_suffix = ".right-half";

} // namespace

WindowCapsule::WindowCapsule(StableId stable_id) : Control(std::move(stable_id)) {
    focusable = true;
}

void WindowCapsule::set_look(const WindowCapsuleLook& look) {
    if (look == look_) return;
    look_ = look;
    invalidate(Dirty::paint | Dirty::semantics);
}

void WindowCapsule::set_colors(const WindowCapsuleColors& colors) {
    if (colors == colors_) return;
    colors_ = colors;
    invalidate(Dirty::paint);
}

void WindowCapsule::set_help_width(double width) {
    const double kept = std::isfinite(width) && width > 0.0 ? width : 20.0;
    if (kept == help_width_) return;
    help_width_ = kept;
    invalidate(Dirty::paint | Dirty::semantics);
}

void WindowCapsule::set_measures(const WindowCapsuleMeasures& measures) {
    const double before = gesture_.measures().minimize_distance;
    gesture_.set_measures(measures);
    if (gesture_.measures().minimize_distance != before) invalidate(Dirty::paint);
}

void WindowCapsule::set_interactive(bool interactive) {
    if (interactive == interactive_) return;
    interactive_ = interactive;
    focusable = interactive;
    static_cast<void>(gesture_.forget(own_window));
    invalidate(Dirty::paint | Dirty::semantics);
}

Rect WindowCapsule::section_bounds(WindowCapsuleSection section) const noexcept {
    const Rect whole = client_rectangle();
    const double help = std::min(std::max(help_width_, whole.height), whole.width);
    const double half = (whole.width - help) * 0.5;
    switch (section) {
    case WindowCapsuleSection::left:
        return Rect{0.0, 0.0, half, whole.height};
    case WindowCapsuleSection::help:
        return Rect{half, 0.0, help, whole.height};
    case WindowCapsuleSection::right:
        return Rect{half + help, 0.0, whole.width - half - help, whole.height};
    case WindowCapsuleSection::none:
    default:
        return Rect{};
    }
}

WindowCapsuleSection WindowCapsule::section_at(Point local_point) const noexcept {
    if (section_bounds(WindowCapsuleSection::left).contains(local_point)) return WindowCapsuleSection::left;
    if (section_bounds(WindowCapsuleSection::help).contains(local_point)) return WindowCapsuleSection::help;
    if (section_bounds(WindowCapsuleSection::right).contains(local_point)) return WindowCapsuleSection::right;
    return WindowCapsuleSection::none;
}

Size WindowCapsule::measure(Size available) {
    static_cast<void>(available);
    return resting_size;
}

bool WindowCapsule::hit_test_local(Point local_point) const {
    const bool hit = interactive_ && client_rectangle().contains(local_point);
    return hit;
}

// Back to front: the capsule's face; the halves' fills, each reaching
// under the question mark by the capsule's radius so its rounded inner end
// is covered; the question mark's own face and text; the halves' marks;
// the focus ring. At the X the whole capsule is one fill (green turning
// red) and one mark.
void WindowCapsule::on_paint(Painter& painter, Rect local_damage) {
    static_cast<void>(local_damage);
    const Rect whole = client_rectangle();
    if (whole.empty()) return;
    const double radius = whole.height * 0.5;
    const Rect left = section_bounds(WindowCapsuleSection::left);
    const Rect middle = section_bounds(WindowCapsuleSection::help);
    const Rect right = section_bounds(WindowCapsuleSection::right);
    // invite: 0 idle to 1 with one half armed; morph: 0 there to 1 at the X.
    const double invite = std::clamp(look_.intent * 2.0, 0.0, 1.0);
    const double morph = std::clamp(look_.intent * 2.0 - 1.0, 0.0, 1.0);
    const bool left_armed = look_.arm == WindowCapsuleArm::left || look_.arm == WindowCapsuleArm::close;
    const bool right_armed = look_.arm == WindowCapsuleArm::right || look_.arm == WindowCapsuleArm::close;
    const bool whole_x = look_.arm == WindowCapsuleArm::close;
    const double minimize_distance = gesture_.measures().minimize_distance;

    PartPaint face;
    face.role = PartRole::face;
    face.family = ControlVisualRole::button;
    face.bounds = whole;
    face.corner_radius.set(radius);
    face.authored_background.set(colors_.face);
    if (look_.hovered != WindowCapsuleSection::none) face.states.insert(AnatomyState::hot);
    if (look_.pressed != WindowCapsuleSection::none) face.states.insert(AnatomyState::pressed);
    paint_part(painter, *this, face);

    PartPaint fill;
    fill.role = PartRole::fill;
    fill.family = ControlVisualRole::button;
    fill.corner_radius.set(radius);
    if (whole_x) {
        fill.bounds = whole;
        fill.content_color.set(mixed_color(colors_.ready, colors_.close, morph, 1.0));
        paint_part(painter, *this, fill);
    } else {
        const Rect left_fill{0.0, 0.0, left.width + radius, whole.height};
        const Rect right_fill{right.x - radius, 0.0, right.width + radius, whole.height};
        // The armed half turns toward the X's red; the other half lights
        // green, inviting the second click. The minimise drag lights the
        // left half as it nears its distance.
        if (left_armed || right_armed) {
            fill.bounds = left_armed ? left_fill : right_fill;
            fill.content_color.set(mixed_color(colors_.face, colors_.close, 0.6, invite));
            paint_part(painter, *this, fill);
            fill.bounds = left_armed ? right_fill : left_fill;
            fill.content_color.set(mixed_color(colors_.ready, colors_.ready, 0.0, invite));
            paint_part(painter, *this, fill);
        } else if (look_.minimize_offset > 0.0 && minimize_distance > 0.0) {
            fill.bounds = left_fill;
            fill.content_color.set(mixed_color(colors_.ready, colors_.ready, 0.0, look_.minimize_offset / minimize_distance));
            paint_part(painter, *this, fill);
        }
        PartPaint question;
        question.role = PartRole::face;
        question.family = ControlVisualRole::button;
        question.bounds = middle;
        question.corner_radius.set(0.0);
        question.frame = PartFrame::none;
        question.authored_background.set(colors_.face);
        if (look_.hovered == WindowCapsuleSection::help) question.states.insert(AnatomyState::hot);
        paint_part(painter, *this, question);
        PartPaint mark_text;
        mark_text.role = PartRole::label;
        mark_text.family = ControlVisualRole::button;
        mark_text.bounds = middle;
        mark_text.text = lend_paint_text("?");
        mark_text.font = FontSpec{FontRole::control, 13, 700, false};
        mark_text.alignment = PartTextAlignment::center;
        mark_text.authored_foreground.set(colors_.glyph);
        paint_part(painter, *this, mark_text);
    }

    PartPaint glyph;
    glyph.role = PartRole::glyph;
    glyph.family = ControlVisualRole::button;
    glyph.authored_foreground.set(colors_.glyph);
    if (whole_x) {
        glyph.bounds = mark_box(whole, 0.6);
        glyph.mark = PartMark::close;
        paint_part(painter, *this, glyph);
    } else {
        // The left mark follows the minimise drag down, at most a third of
        // the capsule's height.
        Rect left_mark = mark_box(left, 0.6);
        left_mark.y += std::min(look_.minimize_offset * 0.25, whole.height / 3.0);
        glyph.bounds = left_mark;
        glyph.mark = left_armed ? PartMark::close : PartMark::chevron_down;
        paint_part(painter, *this, glyph);
        glyph.bounds = mark_box(right, 0.6);
        glyph.mark = right_armed ? PartMark::close : PartMark::grip_dots;
        paint_part(painter, *this, glyph);
    }

    if (look_.focused != WindowCapsuleSection::none) {
        PartPaint ring;
        ring.role = PartRole::focus;
        ring.family = ControlVisualRole::button;
        ring.states.insert(AnatomyState::focus_visible);
        ring.bounds = middle;
        ring.corner_radius.set(0.0);
        if (look_.focused != WindowCapsuleSection::help) ring.corner_radius.set(radius);
        if (look_.focused == WindowCapsuleSection::left) ring.bounds = left;
        if (look_.focused == WindowCapsuleSection::right) ring.bounds = right;
        if (whole_x) {
            ring.bounds = whole;
            ring.corner_radius.set(radius);
        }
        paint_part(painter, *this, ring);
    }
}

// -- the capsule's own gesture ----------------------------------------------------------

void WindowCapsule::take(const WindowCapsuleOutcome& outcome) {
    // A capsule in a program's content keeps no frame clock: its look takes
    // the gesture's targets at once, as under reduced motion. (The window
    // manager animates the capsules it shows.)
    static_cast<void>(gesture_.advance(0.0, true));
    set_look(window_capsule_look(gesture_, own_window));
    if (outcome.command == WindowCapsuleCommand::none) return;
    WindowCapsuleRequest request{};
    request.command = outcome.command;
    request.origin = outcome.origin;
    request.point = outcome.point;
    request.offset = outcome.offset;
    request.snap = outcome.snap;
    requested_.emit(request);
}

void WindowCapsule::on_mouse_down(MouseEventArgs& arguments) {
    Control::on_mouse_down(arguments);
    if (!interactive_ || arguments.button != PointerButton::primary || !effectively_enabled()) return;
    const WindowCapsuleSection section = section_at(arguments.location);
    const WindowCapsuleOutcome outcome = gesture_.press(own_window, section, arguments.location, arguments.clicks);
    if (outcome.handled) arguments.handled = true;
    take(outcome);
}

void WindowCapsule::on_mouse_move(MouseEventArgs& arguments) {
    Control::on_mouse_move(arguments);
    if (!interactive_) return;
    if (gesture_.pressing()) {
        // No snap targets: the program decides where its window goes.
        take(gesture_.drag(arguments.location, Size{}));
        return;
    }
    const WindowCapsuleSection section = section_at(arguments.location);
    if (section == gesture_.hovered() && gesture_.window() == own_window) return;
    take(gesture_.hover(own_window, section));
}

void WindowCapsule::on_mouse_up(MouseEventArgs& arguments) {
    Control::on_mouse_up(arguments);
    if (!interactive_ || arguments.button != PointerButton::primary) return;
    const WindowCapsuleSection section = section_at(arguments.location);
    take(gesture_.release(section));
}

void WindowCapsule::on_mouse_leave() {
    Control::on_mouse_leave();
    if (!interactive_ || gesture_.pressing()) return;
    take(gesture_.hover(HostWindowId{}, WindowCapsuleSection::none));
}

// The window ends a primary press's capture after its release, when the
// gesture is already idle; any other loss (the window deactivated, the
// program's own window move taking the pointer) cancels it, so later moves
// raise no move requests (A-gf-controls-14-F4).
void WindowCapsule::on_mouse_capture_changed() {
    Control::on_mouse_capture_changed();
    if (!interactive_ || !gesture_.pressing()) return;
    take(gesture_.cancel());
}

void WindowCapsule::on_key_down(KeyEventArgs& arguments) {
    Control::on_key_down(arguments);
    if (!interactive_ || arguments.handled) return;
    if (!gesture_.keyboard()) static_cast<void>(gesture_.focus(own_window));
    const WindowCapsuleOutcome outcome = gesture_.key(arguments.physical_key, arguments.modifiers);
    if (outcome.handled) arguments.handled = true;
    take(outcome);
}

void WindowCapsule::on_focus_changed(bool focused) {
    Control::on_focus_changed(focused);
    if (!interactive_) return;
    if (focused) {
        take(gesture_.focus(own_window));
        return;
    }
    if (gesture_.forget(own_window)) set_look(WindowCapsuleLook{});
}

// -- semantics --------------------------------------------------------------------------

SemanticDescriptor WindowCapsule::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.read().empty() ? std::string("Window controls") : accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.exposed = true;
    return descriptor;
}

std::vector<SemanticNode> WindowCapsule::semantic_virtual_children() const {
    std::vector<SemanticNode> children{};
    children.reserve(3U);
    const Rect absolute = absolute_bounds();
    const bool enabled = effectively_enabled();
    const bool closing = look_.arm == WindowCapsuleArm::close;
    const std::array<WindowCapsuleSection, 3> sections{WindowCapsuleSection::left, WindowCapsuleSection::help,
                                                       WindowCapsuleSection::right};
    const std::array<std::string_view, 3> suffixes{left_suffix, help_suffix, right_suffix};
    const std::array<std::string_view, 3> names{"Maximise or minimise", "Help", "Move"};
    for (std::size_t index = 0U; index < sections.size(); ++index) {
        SemanticNode node{};
        node.stable_id = std::string(stable_id().value()) + std::string(suffixes[index]);
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::button;
        node.name = closing ? std::string("Close") : std::string(names[index]);
        const Rect shown = section_bounds(sections[index]);
        node.bounds = {absolute.x + shown.x, absolute.y + shown.y, shown.width, shown.height};
        node.states = SemanticState::visible;
        if (enabled && interactive_) node.states |= SemanticState::enabled;
        node.actions = {SemanticAction::press};
        children.push_back(std::move(node));
    }
    return children;
}

// A press on a section is its click.
bool WindowCapsule::on_semantic_child_action(std::string_view child, SemanticAction action, std::string_view) {
    if (!interactive_ || !effectively_enabled() || action != SemanticAction::press) return false;
    const std::string prefix(stable_id().value());
    WindowCapsuleSection section = WindowCapsuleSection::none;
    if (child == prefix + std::string(left_suffix)) section = WindowCapsuleSection::left;
    if (child == prefix + std::string(help_suffix)) section = WindowCapsuleSection::help;
    if (child == prefix + std::string(right_suffix)) section = WindowCapsuleSection::right;
    if (section == WindowCapsuleSection::none) return false;
    const Rect bounds = section_bounds(section);
    const Point centre{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    static_cast<void>(gesture_.press(own_window, section, centre, 1U));
    take(gesture_.release(section));
    return true;
}

// -- anatomy ----------------------------------------------------------------------------

const ControlAnatomy& WindowCapsule::anatomy() const noexcept {
    return window_capsule_anatomy;
}

AnatomyStateSet WindowCapsule::anatomy_states() const {
    AnatomyStateSet states = base_anatomy_states();
    if (look_.hovered != WindowCapsuleSection::none) states.insert(AnatomyState::hot);
    if (look_.pressed != WindowCapsuleSection::none) states.insert(AnatomyState::pressed);
    return states;
}

void WindowCapsule::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), window_capsule_interactions.begin(), window_capsule_interactions.end());
}

void WindowCapsule::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect whole = client_rectangle();
    const bool whole_x = look_.arm == WindowCapsuleArm::close;
    Parts record;
    record.region = PartGeometry::from_bounds(whole);
    record.face = PartGeometry::from_bounds(whole);
    record.left_half = PartGeometry::from_bounds(section_bounds(WindowCapsuleSection::left));
    record.right_half = PartGeometry::from_bounds(section_bounds(WindowCapsuleSection::right));
    if (!whole_x) {
        record.help = PartGeometry::from_bounds(section_bounds(WindowCapsuleSection::help));
        record.label = PartGeometry::from_bounds(section_bounds(WindowCapsuleSection::help));
    }
    record.glyph = PartGeometry::from_bounds(whole_x ? mark_box(whole, 0.6) : section_bounds(WindowCapsuleSection::left));
    if (look_.focused != WindowCapsuleSection::none) record.focus = PartGeometry::from_bounds(section_bounds(look_.focused));
    write_parts(anatomy(), window_capsule_part_fields, record, geometry);
}

WindowCapsule::Parts WindowCapsule::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), window_capsule_part_fields, published.parts(), record);
    return record;
}

} // namespace gui_forms
