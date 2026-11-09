#include "rack_module_panel.hpp"

#include "../../panel_anatomy.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <array>
#include <numeric>
#include <utility>

namespace gui_forms::detail {
namespace {

// The severity keyline's width along the module's leading edge, in logical
// units (InfoBanner's keyline's).
constexpr double severity_keyline_width = 3.0;

// One custom state per severity, in NoticeSeverity's order, so a
// severity's number is its state's index, as InfoBanner's and Toast's.
constexpr std::array<CustomStateDeclaration, 4> module_custom_states{{
    {.name = "information"},
    {.name = "warning"},
    {.name = "success"},
    {.name = "error"},
}};

// A panel's anatomy with the severity states.
constexpr ControlAnatomy module_anatomy{
    .control_id = "panel",
    .parts = panel_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = module_custom_states,
};

} // namespace

RackModulePanel::RackModulePanel(StableId stable_id)
    : Panel(std::move(stable_id)) {
    border_style = BorderStyle::line;
    margin = Insets{};
    accessible_description = "Compact editing instrument; Alt+Left and Alt+Right request reordering";
}

void RackModulePanel::set_compact(bool compact) {
    if (compact_ == compact) return;
    compact_ = compact;
    invalidate(invalidation::bounds);
}

void RackModulePanel::set_severity(std::optional<NoticeSeverity> severity) {
    require_mutable();
    if (severity_ == severity) return;
    severity_ = severity;
    invalidate(Dirty::style | Dirty::paint);
}

const ControlAnatomy& RackModulePanel::anatomy() const noexcept {
    return module_anatomy;
}

AnatomyStateSet RackModulePanel::anatomy_states() const {
    AnatomyStateSet states = Panel::anatomy_states();
    if (severity_) states.insert(CustomStateIndex{static_cast<std::uint8_t>(*severity_)});
    return states;
}

void RackModulePanel::on_paint(Painter& painter, Rect local_damage) {
    Panel::on_paint(painter, local_damage);
    if (!severity_) return;
    // The keyline over the face, inside the line border, in the severity's
    // colour as a notice's: an authored foreground over the module's role.
    const Rect bounds = local_bounds();
    const double border = border_style.read() == BorderStyle::none ? 0.0 : 1.0;
    const double height = std::max(0.0, bounds.height - 2.0 * border);
    PartPaint keyline{};
    keyline.family = visual_role.read();
    keyline.role = PartRole::keyline;
    keyline.states = anatomy_states();
    keyline.bounds = Rect{border, border, severity_keyline_width, height};
    keyline.authored_foreground = notice_severity_colour(*severity_);
    paint_part(painter, *this, keyline);
}

void RackModulePanel::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    const double scale = effective_text_scale();
    // Children sit inside the module's 1-unit line border (sweep defect
    // D19: the enable switch at x 0 covered the left border).
    const double border = border_style.read() == BorderStyle::none ? 0.0 : 1.0;
    const double width = std::max(0.0, final_bounds.width - border);
    const double height = std::max(0.0, final_bounds.height - 2.0 * border);
    const double enable_width = 20.0 * scale;
    const double remove_width = 22.0 * scale;
    const double gap = 3.0 * scale;
    if (enable) {
        set_child_layout(enable, {border + 2.0 * scale, border, enable_width,
                                  compact_ ? 26.0 * scale : height});
    }
    if (remove) {
        set_child_layout(remove, {std::max(0.0, width - remove_width), border,
                                  remove_width,
                                  compact_ ? 26.0 * scale : height});
    }
    const double field_left = border + 2.0 * scale + enable_width + 5.0 * scale;
    const double field_right = std::max(field_left,
                                         width - remove_width - 5.0 * scale);
    const double field_width = std::max(0.0, field_right - field_left);
    if (compact_) {
        double y = 4.0 * scale;
        for (const Control::Ptr& field : fields) {
            set_child_layout(field, {field_left, y, field_width, 25.0 * scale});
            y += 28.0 * scale;
        }
        if (status) {
            set_child_layout(status, {field_left, y, field_width,
                                      std::max(16.0 * scale,
                                               height - y - 3.0 * scale)});
        }
        return;
    }
    const double fields_y = 5.0 * scale;
    const double fields_height = 27.0 * scale;
    const double status_y = 37.0 * scale;
    const double total_weight = std::accumulate(
        field_weights.begin(), field_weights.end(), 0.0);
    const double available = std::max(
        0.0, field_width - gap * static_cast<double>(
            fields.empty() ? 0U : fields.size() - 1U));
    double x = field_left;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        const double weight = total_weight > 0.0
            ? field_weights[index] / total_weight
            : 1.0 / static_cast<double>(fields.size());
        const double next_width = index + 1U == fields.size()
            ? std::max(0.0, field_right - x)
            : available * weight;
        set_child_layout(fields[index], {x, fields_y, next_width,
                                         fields_height});
        x += next_width + gap;
    }
    if (status) {
        set_child_layout(status, {field_left, status_y, field_width,
                                  std::max(16.0 * scale,
                                           height - status_y - 3.0 * scale)});
    }
}

SemanticDescriptor RackModulePanel::semantic_descriptor() const {
    SemanticDescriptor descriptor = Panel::semantic_descriptor();
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.states = SemanticState::enabled | SemanticState::visible;
    descriptor.actions = {SemanticAction::decrement,
                          SemanticAction::increment};
    descriptor.exposed = true;
    descriptor.include_descendants = true;
    return descriptor;
}

bool RackModulePanel::on_semantic_action(SemanticAction action,
                                         std::string_view value) {
    if (action == SemanticAction::decrement ||
        action == SemanticAction::increment) {
        if (move_request) move_request(action == SemanticAction::increment);
        return true;
    }
    return Panel::on_semantic_action(action, value);
}

} // namespace gui_forms::detail
