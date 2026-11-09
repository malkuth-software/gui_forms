#include "gui_forms/controls/visual_inspector_view/visual_inspector_view.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include "gui_forms/window.hpp"
#include "../basic/basic_control_rendering.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <array>
#include <cstddef>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms {
namespace {

constexpr Color inspector_ink = Color::rgba(225, 234, 241);
constexpr Color inspector_muted = Color::rgba(157, 174, 187);
constexpr Color inspector_blue = Color::rgba(64, 170, 232);
constexpr Color inspector_gold = Color::rgba(247, 194, 72);
constexpr Color inspector_magenta = Color::rgba(238, 102, 184);
constexpr Color inspector_panel = Color::rgba(30, 39, 47);
constexpr Color inspector_panel_light = Color::rgba(44, 55, 64);

std::string rect_text(Rect value) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(1) << value.x << ", " << value.y
           << "  " << value.width << " × " << value.height;
    return output.str();
}

std::string state_text(const VisualControlInspection& target) {
    std::ostringstream output;
    output << (target.state.visible ? "visible" : "hidden") << " · "
           << (target.state.enabled ? "enabled" : "disabled");
    if (target.state.focused) output << " · focused";
    if (target.state.hovered) output << " · hovered";
    if (target.state.pressed) output << " · pressed";
    if (target.state.pointer_captured) output << " · captured";
    output << " · " << visual_status_name(target.state.visual_status);
    return output.str();
}

std::string representative_font_text(const VisualControlInspection& target) {
    const VisualPaintOperationSnapshot* representative = nullptr;
    for (const VisualPaintOperationSnapshot& operation :
         target.display_operations) {
        if (operation.operation != VisualPaintOperation::draw_text) continue;
        if (representative == nullptr ||
            operation.font.size > (*representative).font.size) {
            representative = &operation;
        }
    }
    if (representative == nullptr) return "no committed text draw";
    const VisualPaintOperationSnapshot& operation = *representative;
    std::ostringstream output;
    output << font_role_name(operation.font.role) << " " << std::fixed
           << std::setprecision(1) << operation.font.size << "/"
           << operation.font.weight;
    if (operation.font.italic) output << "i";
    if (operation.font.letter_spacing != 0.0) {
        // Two decimals: tracking is authored in hundredths (0.24), which one
        // decimal showed as 0.2 (sweep defect D23).
        output << " +" << std::setprecision(2) << operation.font.letter_spacing << "ls"
               << std::setprecision(1);
    }
    if (operation.resolved_text) {
        const ResolvedTextLayout& resolved = *operation.resolved_text;
        output << " · " << text_resolution_status_name(resolved.status);
        if (!resolved.primary_family.empty()) {
            output << " · " << resolved.primary_family;
        }
        output << " · " << resolved.runs.size() << "r";
        if (resolved.missing_clusters != 0U) {
            output << " · !" << resolved.missing_clusters;
        }
    } else {
        output << " · unavailable";
    }
    return output.str();
}

std::string operation_summary(const VisualControlInspection& target) {
    std::size_t clips{};
    std::size_t fills{};
    std::size_t gradients{};
    std::size_t shadows{};
    std::size_t text{};
    for (const VisualPaintOperationSnapshot& operation :
         target.display_operations) {
        switch (operation.operation) {
        case VisualPaintOperation::clip_rect:
        case VisualPaintOperation::clip_rounded_rect: ++clips; break;
        case VisualPaintOperation::fill_rect:
        case VisualPaintOperation::fill_rounded_rect: ++fills; break;
        case VisualPaintOperation::fill_linear_gradient:
        case VisualPaintOperation::fill_linear_gradient_spread:
        case VisualPaintOperation::fill_radial_gradient: ++gradients; break;
        case VisualPaintOperation::draw_box_shadow:
        case VisualPaintOperation::draw_inset_box_shadow: ++shadows; break;
        case VisualPaintOperation::draw_text: ++text; break;
        default: break; // The summary counts only the operations above.
        }
    }
    std::ostringstream output;
    output << target.total_display_operations << " ops · " << clips
           << " clips · " << fills << " fills · " << gradients
           << " gradients · " << shadows << " shadows · " << text << " text";
    return output.str();
}

std::string material_summary(const VisualControlInspection& target) {
    if (!target.authored_material) {
        return "none authored; resolved paint is recorded below";
    }
    std::ostringstream output;
    output << (*target.authored_material).fills.size() << " layers · "
           << (*target.authored_material).shadows.size() << " shadows · "
           << (*target.authored_material).keylines.size() << " keylines · radius "
           << (*target.authored_material).corner_radius;
    return output.str();
}

VisualInspectionOptions view_options() {
    VisualInspectionOptions options;
    options.maximum_controls = 16'384U;
    options.maximum_operations_per_control = 512U;
    options.include_display_operations = true;
    options.include_text = false;
    return options;
}

// The semantic value shows no resolved text, so it is not resolved (that
// shapes every text operation in the window).
VisualInspectionOptions semantic_options() {
    VisualInspectionOptions options = view_options();
    options.include_resolved_text = false;
    return options;
}

VisualInspectionOptions overlay_options() {
    VisualInspectionOptions options;
    options.maximum_controls = 16'384U;
    options.include_display_operations = false;
    return options;
}

// The semantic value: the target's identity, state, arranged bounds,
// effective clip and operation counts, from one inspection of the window.
std::string inspected_value(Window& owner, const std::string& target_stable_id) {
    const VisualInspectionSnapshot snapshot =
        owner.visual_inspection_snapshot(semantic_options());
    const VisualControlInspection* target = snapshot.find(target_stable_id);
    if (target == nullptr) return "Target not found: " + target_stable_id;
    return (*target).stable_id + "; " + state_text(*target) + "; arranged " +
           rect_text((*target).layout.arranged_bounds) + "; effective clip " +
           rect_text((*target).layout.effective_clip) + "; " +
           operation_summary(*target);
}

// What the view draws below its title: the target's rows, or one message
// when there are none (no window, or no such target).
struct InspectorRow final {
    std::string label;
    std::string value;
    Color color;
    // How much of `value` a poll compares; the rest is drawn but describes the
    // paint itself. The window's pending damage always counts the inspector's
    // own repaint, so comparing it would make every poll repaint the panel.
    std::size_t compared_length{std::string::npos};
};

struct InspectorContent final {
    std::string message;
    Color message_color{};
    std::vector<InspectorRow> rows;
};

InspectorContent inspector_content(Window* owner, const std::string& target_stable_id) {
    InspectorContent content;
    if (owner == nullptr) {
        content.message = "Detached";
        content.message_color = inspector_muted;
        return content;
    }
    const VisualInspectionSnapshot snapshot =
        (*owner).visual_inspection_snapshot(view_options());
    const VisualControlInspection* target = snapshot.find(target_stable_id);
    if (target == nullptr) {
        content.message = "Target not found: " + target_stable_id;
        content.message_color = inspector_gold;
        return content;
    }
    content.rows = {
        {"IDENTITY", (*target).stable_id + "  #" +
             std::to_string((*target).runtime_id.value), inspector_ink},
        {"STATE", state_text(*target), inspector_ink},
        {"REQUESTED", rect_text((*target).layout.requested_bounds), inspector_muted},
        {"ARRANGED", rect_text((*target).layout.arranged_bounds), inspector_blue},
        {"ABSOLUTE", rect_text((*target).layout.absolute_bounds), inspector_blue},
        {"VISUAL", rect_text((*target).layout.visual_bounds), inspector_magenta},
        {"EFFECTIVE CLIP", rect_text((*target).layout.effective_clip), inspector_gold},
        {"VIEWPORT", rect_text((*target).layout.child_viewport), inspector_muted},
        {"MATERIAL", material_summary(*target), inspector_ink},
        {"RESOLVED FONT", representative_font_text(*target), inspector_ink},
        {"DISPLAY", operation_summary(*target), inspector_ink},
        {"CHUNK", (*target).display_chunk
             ? ((*target).display_chunk_current ? "current generation " :
                                                "STALE generation ") +
                   std::to_string((*(*target).display_chunk).generation)
             : "not painted", (*target).display_chunk_current ? inspector_blue :
                                                               inspector_gold},
        {"THEME", (*target).theme_id +
             ((*target).theme_override ? " · local override" : " · inherited"),
             inspector_muted},
    };
    const std::string controls_text = std::to_string(snapshot.total_controls) + " controls";
    const std::size_t pending_damage = snapshot.pending_damage[0].size() +
        snapshot.pending_damage[1].size() + snapshot.pending_damage[2].size();
    const std::string window_text =
        controls_text + " · " + std::to_string(pending_damage) + " pending damage rects";
    content.rows.push_back({"WINDOW", window_text, inspector_muted, controls_text.size()});
    return content;
}

void append_color(std::string& key, Color color) {
    key.push_back(static_cast<char>(color.red));
    key.push_back(static_cast<char>(color.green));
    key.push_back(static_cast<char>(color.blue));
    key.push_back(static_cast<char>(color.alpha));
}

// Every drawn string and colour, separated by a byte no row text contains
// (0x1F, the unit separator), so two different contents never share a key.
std::string content_key(const InspectorContent& content) {
    std::string key = content.message;
    key.push_back('\x1f');
    append_color(key, content.message_color);
    for (const InspectorRow& row : content.rows) {
        key.push_back('\x1f');
        key += row.label;
        key.push_back('\x1f');
        key.append(row.value, 0U, row.compared_length);
        key.push_back('\x1f');
        append_color(key, row.color);
    }
    return key;
}

} // namespace

VisualInspectorView::VisualInspectorView(StableId stable_id,
                                         std::string initial_target)
    : Control(std::move(stable_id)) {
    target_stable_id = std::move(initial_target);
    accessible_name = "Visual state inspector";
}

void VisualInspectorView::prepare_write(const decltype(target_stable_id)&, std::string&) {
    require_mutable();
}

// A new target drops the cached semantic value.
void VisualInspectorView::written(const decltype(target_stable_id)&) {
    semantic_value_.reset();
    invalidate(Dirty::paint | Dirty::semantics | Dirty::accessibility);
}

void VisualInspectorView::refresh() {
    semantic_value_.reset();
    invalidate(Dirty::paint | Dirty::semantics);
}

bool VisualInspectorView::refresh_if_changed() {
    // An occluded window paints nothing; the first poll after it shows again
    // compares against what was last drawn.
    const Window* owner = window();
    if (owner != nullptr && (*owner).occluded()) return false;
    const InspectorContent content = inspector_content(window(), target_stable_id.read());
    const std::string key = content_key(content);
    const bool changed = key != painted_content_;
    if (changed) refresh();
    return changed;
}

void VisualInspectorView::start_polling(FrameInterval interval) {
    const std::shared_ptr<VisualInspectorView> self =
        std::static_pointer_cast<VisualInspectorView>(shared_from_this());
    poll_timer_.start(*this, interval, PollTick{self});
}

void VisualInspectorView::stop_polling() noexcept { poll_timer_.stop(); }

AnimationTickResult VisualInspectorView::PollTick::operator()(FrameTime) const {
    const std::shared_ptr<VisualInspectorView> live = control.lock();
    const bool changed = live && (*live).refresh_if_changed();
    const AnimationTickResult result =
        changed ? AnimationTickResult::damaged : AnimationTickResult::unchanged;
    return result;
}

void VisualInspectorView::on_paint(Painter& painter, Rect) {
    poll_timer_.note_painted();
    const Rect bounds = client_rectangle();
    painter.fill_rect(bounds, inspector_panel);
    painter.fill_rect({0.0, 0.0, bounds.width, 44.0}, inspector_panel_light);
    painter.fill_rect({0.0, 0.0, 5.0, bounds.height}, inspector_blue);

    const FontSpec title = effective_font({FontRole::control, 14.0, 700, false});
    const FontSpec section = effective_font({FontRole::control, 10.0, 700, false});
    const FontSpec body = effective_font({FontRole::monospace, 10.0, 400, false});
    painter.draw_text_utf8({18.0, 27.0}, "VISUAL STATE INSPECTOR", title,
                           inspector_ink);

    const InspectorContent content = inspector_content(window(), target_stable_id.read());
    painted_content_ = content_key(content);
    if (content.rows.empty()) {
        painter.draw_text_utf8({18.0, 68.0}, content.message, body, content.message_color);
        return;
    }
    // The rows share the height between the title band and the legend,
    // 30 units each at most and 18 at least; rows that still do not fit
    // are left out rather than drawn over the legend (sweep defect D25).
    // A value wider than its column ends in an ellipsis (D24).
    constexpr double first_baseline = 64.0;
    constexpr double legend_reserve = 34.0;
    const double last_baseline = bounds.height - legend_reserve;
    const double gaps = static_cast<double>(std::max<std::size_t>(content.rows.size(), 2U) - 1U);
    const double pitch = std::clamp((last_baseline - first_baseline) / gaps, 18.0, 30.0);
    const double value_width = std::max(0.0, bounds.width - 18.0 - 145.0);
    double y = first_baseline;
    for (const InspectorRow& row : content.rows) {
        if (y > last_baseline) break;
        painter.draw_text_utf8({18.0, y}, row.label, section, inspector_muted);
        const std::string value = elide_text_end(painter, row.value, body, value_width);
        painter.draw_text_utf8({145.0, y}, value, body, row.color);
        y += pitch;
        painter.draw_line({18.0, y - pitch / 3.0}, {bounds.width - 18.0, y - pitch / 3.0},
                          Color::rgba(73, 86, 96), 1.0);
    }
    painter.draw_text_utf8(
        {18.0, bounds.height - 20.0},
        "Blue arranged · magenta visual · gold effective clip",
        effective_font({FontRole::content, 9.0, 400, false}), inspector_muted);
}

VisualInspectorView::SemanticValueKey VisualInspectorView::semantic_value_key(const Window& owner) {
    SemanticValueKey key;
    key.content_revision = owner.paint_lease_snapshot().content_revision;
    key.display_generation = owner.metrics_snapshot().display_generation;
    const Control::Ptr focused = owner.focused_control();
    if (focused) key.focused = (*focused).runtime_id();
    const Control::Ptr pressed = owner.pressed_control();
    if (pressed) key.pressed = (*pressed).runtime_id();
    const Control::Ptr captured = owner.captured_control();
    if (captured) key.captured = (*captured).runtime_id();
    key.window_active = owner.active();
    return key;
}

bool VisualInspectorView::same_key(const SemanticValueKey& left,
                                   const SemanticValueKey& right) noexcept {
    return left.content_revision == right.content_revision &&
           left.display_generation == right.display_generation &&
           left.focused == right.focused && left.pressed == right.pressed &&
           left.captured == right.captured && left.window_active == right.window_active;
}

std::string VisualInspectorView::accessible_value() const {
    Window* owner = window();
    if (owner == nullptr) return "Detached";
    if (semantic_value_ && same_key((*semantic_value_).key, semantic_value_key(*owner))) {
        return (*semantic_value_).value;
    }
    // The key is read after the inspection, which may lay the window out
    // first and so move its revision.
    std::string value = inspected_value(*owner, target_stable_id.read());
    semantic_value_ = SemanticValue{semantic_value_key(*owner), value};
    return value;
}

SemanticDescriptor VisualInspectorView::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::group;
    descriptor.name = "Visual state inspector";
    descriptor.value = accessible_value();
    descriptor.exposed = true;
    return descriptor;
}

VisualInspectorOverlay::VisualInspectorOverlay(StableId stable_id,
                                               std::string initial_target)
    : Control(std::move(stable_id)) {
    target_stable_id = std::move(initial_target);
    hit_test_transparent = true;
    set_paint_plane(PaintPlane::overlay);
}

void VisualInspectorOverlay::refresh() { invalidate(Dirty::paint); }

VisualInspectorOverlay::Outlines VisualInspectorOverlay::current_outlines() const {
    Outlines outlines;
    Window* owner = window();
    if (owner == nullptr || target_stable_id.read().empty()) return outlines;
    const VisualInspectionSnapshot snapshot =
        (*owner).visual_inspection_snapshot(overlay_options());
    const VisualControlInspection* target = snapshot.find(target_stable_id.read());
    if (target == nullptr || (*target).runtime_id == runtime_id()) return outlines;
    outlines.shown = true;
    outlines.arranged = rectangle_from_window((*target).layout.absolute_bounds);
    outlines.visual = rectangle_from_window((*target).layout.visual_bounds);
    outlines.clip = rectangle_from_window((*target).layout.effective_clip);
    return outlines;
}

bool VisualInspectorOverlay::refresh_if_changed() {
    const Window* owner = window();
    if (owner != nullptr && (*owner).occluded()) return false;
    const Outlines outlines = current_outlines();
    const bool changed = !painted_ || !(outlines == *painted_);
    if (changed) refresh();
    return changed;
}

void VisualInspectorOverlay::start_polling(FrameInterval interval) {
    const std::shared_ptr<VisualInspectorOverlay> self =
        std::static_pointer_cast<VisualInspectorOverlay>(shared_from_this());
    poll_timer_.start(*this, interval, PollTick{self});
}

void VisualInspectorOverlay::stop_polling() noexcept { poll_timer_.stop(); }

AnimationTickResult VisualInspectorOverlay::PollTick::operator()(FrameTime) const {
    const std::shared_ptr<VisualInspectorOverlay> live = control.lock();
    const bool changed = live && (*live).refresh_if_changed();
    const AnimationTickResult result =
        changed ? AnimationTickResult::damaged : AnimationTickResult::unchanged;
    return result;
}

void VisualInspectorOverlay::on_paint(Painter& painter, Rect) {
    poll_timer_.note_painted();
    const Outlines outlines = current_outlines();
    painted_ = outlines;
    if (!outlines.shown) return;
    // The outlines' colours are the legend's data (owner decision W-7), so
    // each side is a fill part in a content colour, drawn as given.
    paint_outline(painter, outlines.visual, inspector_magenta, 2.0);
    paint_outline(painter, outlines.arranged, inspector_blue, 2.0);
    if (!outlines.clip.empty()) paint_outline(painter, outlines.clip, inspector_gold, 1.0);
}

// The four sides of a `width`-unit outline centred on `bounds`' edges, as
// fill parts in `colour`.
void VisualInspectorOverlay::paint_outline(Painter& painter, Rect bounds, Color colour, double width) const {
    const double half = width * 0.5;
    const std::array<Rect, 4> sides{{
        {bounds.x - half, bounds.y - half, bounds.width + width, width},
        {bounds.x - half, bounds.bottom() - half, bounds.width + width, width},
        {bounds.x - half, bounds.y + half, width, std::max(0.0, bounds.height - width)},
        {bounds.right() - half, bounds.y + half, width, std::max(0.0, bounds.height - width)},
    }};
    PartPaint side;
    side.family = ControlVisualRole::panel;
    side.role = PartRole::fill;
    side.content_color = colour;
    for (const Rect& edge : sides) {
        side.bounds = edge;
        paint_part(painter, *this, side);
    }
}

bool VisualInspectorOverlay::hit_test_local(Point) const { return false; }

SemanticDescriptor VisualInspectorOverlay::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.exposed = false;
    return descriptor;
}

namespace {

constexpr std::array<PartField<VisualInspectorView::Parts>, 5> visual_inspector_view_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &VisualInspectorView::Parts::region},
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &VisualInspectorView::Parts::face},
    {.declaration = {.name = "header", .role = PartRole::face, .composed_from = {}},
     .member = &VisualInspectorView::Parts::header},
    {.declaration = {.name = "accent_strip", .role = PartRole::keyline, .composed_from = {}},
     .member = &VisualInspectorView::Parts::accent_strip},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &VisualInspectorView::Parts::focus},
}};

constexpr std::array<StateField<VisualInspectorView::States>, 5> visual_inspector_view_state_fields{{
    {.state = AnatomyState::normal, .member = &VisualInspectorView::States::normal},
    {.state = AnatomyState::enabled, .member = &VisualInspectorView::States::enabled},
    {.state = AnatomyState::disabled, .member = &VisualInspectorView::States::disabled},
    {.state = AnatomyState::focused, .member = &VisualInspectorView::States::focused},
    {.state = AnatomyState::focus_visible, .member = &VisualInspectorView::States::focus_visible},
}};

constexpr std::array<PartDeclaration, 5> visual_inspector_view_parts =
    part_declarations(visual_inspector_view_part_fields);
constexpr std::array<AnatomyState, 5> visual_inspector_view_states =
    state_declarations(visual_inspector_view_state_fields);

constexpr ControlAnatomy visual_inspector_view_anatomy{
    .control_id = "visual_inspector_view",
    .parts = visual_inspector_view_parts,
    .states = visual_inspector_view_states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& VisualInspectorView::anatomy() const noexcept {
    return visual_inspector_view_anatomy;
}

void VisualInspectorView::publish_part_geometry(std::span<PartGeometry> geometry) const {

    const Rect bounds = client_rectangle();
    Parts record;
    record.region = PartGeometry::from_bounds(bounds);
    record.face = PartGeometry::from_bounds(bounds);
    // The lighter title band and the blue strip on_paint lays across the face.
    record.header = PartGeometry::from_bounds(
        Rect::intersection({0.0, 0.0, bounds.width, 44.0}, bounds));
    record.accent_strip = PartGeometry::from_bounds(
        Rect::intersection({0.0, 0.0, 5.0, bounds.height}, bounds));
    record.focus = PartGeometry::from_bounds(bounds);
    write_parts(anatomy(), visual_inspector_view_part_fields, record, geometry);
}

VisualInspectorView::Parts VisualInspectorView::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), visual_inspector_view_part_fields, published.parts(), record);
    return record;
}

VisualInspectorView::States VisualInspectorView::states() const {
    States record;
    read_states(visual_inspector_view_state_fields, anatomy_states(), record);
    return record;
}

namespace {

constexpr std::array<PartField<VisualInspectorOverlay::Parts>, 3> visual_inspector_overlay_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &VisualInspectorOverlay::Parts::region},
    {.declaration = {.name = "highlight", .role = PartRole::selection, .composed_from = {}},
     .member = &VisualInspectorOverlay::Parts::highlight},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &VisualInspectorOverlay::Parts::focus},
}};

constexpr std::array<StateField<VisualInspectorOverlay::States>, 5> visual_inspector_overlay_state_fields{{
    {.state = AnatomyState::normal, .member = &VisualInspectorOverlay::States::normal},
    {.state = AnatomyState::enabled, .member = &VisualInspectorOverlay::States::enabled},
    {.state = AnatomyState::disabled, .member = &VisualInspectorOverlay::States::disabled},
    {.state = AnatomyState::focused, .member = &VisualInspectorOverlay::States::focused},
    {.state = AnatomyState::focus_visible, .member = &VisualInspectorOverlay::States::focus_visible},
}};

constexpr std::array<PartDeclaration, 3> visual_inspector_overlay_parts =
    part_declarations(visual_inspector_overlay_part_fields);
constexpr std::array<AnatomyState, 5> visual_inspector_overlay_states =
    state_declarations(visual_inspector_overlay_state_fields);

constexpr ControlAnatomy visual_inspector_overlay_anatomy{
    .control_id = "visual_inspector_overlay",
    .parts = visual_inspector_overlay_parts,
    .states = visual_inspector_overlay_states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& VisualInspectorOverlay::anatomy() const noexcept {
    return visual_inspector_overlay_anatomy;
}

void VisualInspectorOverlay::publish_part_geometry(std::span<PartGeometry> geometry) const {

    const Rect bounds = client_rectangle();
    Parts record;
    record.region = PartGeometry::from_bounds(bounds);
    record.focus = PartGeometry::from_bounds(bounds);
    // The target's arranged and visual bounds that on_paint outlines.
    Window* owner = window();
    if (owner != nullptr && !target_stable_id.read().empty()) {
        const VisualInspectionSnapshot snapshot =
            (*owner).visual_inspection_snapshot(overlay_options());
        const VisualControlInspection* target = snapshot.find(target_stable_id.read());
        if (target != nullptr && (*target).runtime_id != runtime_id()) {
            const Rect arranged = rectangle_from_window((*target).layout.absolute_bounds);
            const Rect visual = rectangle_from_window((*target).layout.visual_bounds);
            record.highlight = PartGeometry::from_bounds(Rect::united(arranged, visual));
        }
    }
    write_parts(anatomy(), visual_inspector_overlay_part_fields, record, geometry);
}

VisualInspectorOverlay::Parts VisualInspectorOverlay::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), visual_inspector_overlay_part_fields, published.parts(), record);
    return record;
}

VisualInspectorOverlay::States VisualInspectorOverlay::states() const {
    States record;
    read_states(visual_inspector_overlay_state_fields, anatomy_states(), record);
    return record;
}

// property_codemod begin VisualInspectorView (tools/property_families/visual_inspector_view.json; generated, do not edit)

namespace {

PropertyDescriptor visual_inspector_view_target_stable_id_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "target_stable_id";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Behavior";
    descriptor.description = "Stable ID of the control the view inspects; empty for none.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_visual_inspector_view_target_stable_id() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        visual_inspector_view_target_stable_id_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, VisualInspectorView::visual_inspector_view_property_count>& VisualInspectorView::visual_inspector_view_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 1U> own{{
        {"target_stable_id", &describe_visual_inspector_view_target_stable_id,
         &read_cell_entry<VisualInspectorView, decltype(&VisualInspectorView::target_stable_id), &VisualInspectorView::target_stable_id>,
         &write_cell_entry<VisualInspectorView, decltype(&VisualInspectorView::target_stable_id), &VisualInspectorView::target_stable_id>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "VisualInspectorView's property table names a property twice");
    static const std::array<PropertyEntry, visual_inspector_view_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> VisualInspectorView::property_table() const noexcept {
    const std::array<PropertyEntry, visual_inspector_view_property_count>& entries = visual_inspector_view_property_entries();
    return std::span<const PropertyEntry>(entries);
}

// property_codemod end VisualInspectorView

// property_codemod begin VisualInspectorOverlay (tools/property_families/visual_inspector_overlay.json; generated, do not edit)

namespace {

PropertyDescriptor visual_inspector_overlay_target_stable_id_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "target_stable_id";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Behavior";
    descriptor.description = "Stable ID of the control the overlay outlines; empty for none.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::paint;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_visual_inspector_overlay_target_stable_id() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        visual_inspector_overlay_target_stable_id_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, VisualInspectorOverlay::visual_inspector_overlay_property_count>& VisualInspectorOverlay::visual_inspector_overlay_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 1U> own{{
        {"target_stable_id", &describe_visual_inspector_overlay_target_stable_id,
         &read_cell_entry<VisualInspectorOverlay, decltype(&VisualInspectorOverlay::target_stable_id), &VisualInspectorOverlay::target_stable_id>,
         &write_cell_entry<VisualInspectorOverlay, decltype(&VisualInspectorOverlay::target_stable_id), &VisualInspectorOverlay::target_stable_id>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "VisualInspectorOverlay's property table names a property twice");
    static const std::array<PropertyEntry, visual_inspector_overlay_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> VisualInspectorOverlay::property_table() const noexcept {
    const std::array<PropertyEntry, visual_inspector_overlay_property_count>& entries = visual_inspector_overlay_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void VisualInspectorOverlay::prepare_write(const decltype(target_stable_id)&, std::string&) {
    require_mutable();
}

void VisualInspectorOverlay::written(const decltype(target_stable_id)&) {
    invalidate(Dirty::paint);
}

// property_codemod end VisualInspectorOverlay

} // namespace gui_forms
