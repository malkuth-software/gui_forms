// The theme engine's inputs in a window (T2 plan, phase E6; GIVEN B1-1 and
// B2-1): the pointer the engine follows, the values the application
// publishes, where each keyed part was drawn, and damage by dependency: a
// change to an input damages only the parts that read it, within their
// extents.

#include "gui_forms/window.hpp"

#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "../../display/recording_painter/recording_painter.hpp"
#include "../overlay/overlay_attachment.hpp"

#include <cmath>
#include <stdexcept>

namespace gui_forms {
namespace {

// The bits of the two pointer inputs.
constexpr std::uint64_t pointer_input_bits = expression_input_bit(engine_input_position(EngineInput::pointer_x)) |
                                             expression_input_bit(engine_input_position(EngineInput::pointer_y));

// The bits of every published value's position.
constexpr std::uint64_t every_published_bit() noexcept {
    std::uint64_t bits = 0U;
    for (std::size_t value = 0; value < maximum_published_values; ++value) {
        bits = bits | expression_input_bit(published_value_position(value));
    }
    return bits;
}
constexpr std::uint64_t published_input_bits = every_published_bit();

} // namespace

bool SurfaceRoot::publish_value(std::string_view name, double value) {
    require_root_thread("published value");
    if (!valid_style_class_name(name) || !std::isfinite(value)) return false;
    PublishedValue* found = nullptr;
    for (PublishedValue& published : published_values_) {
        if (published.name == name) found = &published;
    }
    if (found != nullptr && (*found).value == value) return true;
    if (found == nullptr) {
        PublishedValue made{};
        made.name = std::string(name);
        made.value = value;
        published_values_.push_back(std::move(made));
    } else {
        (*found).value = value;
    }
    metrics_.record_engine_input_change();
    damage_published_readers(name);
    return true;
}

void SurfaceRoot::withdraw_value(std::string_view name) {
    require_root_thread("published value");
    for (std::size_t index = 0; index < published_values_.size(); ++index) {
        if (published_values_[index].name != name) continue;
        published_values_.erase(published_values_.begin() + static_cast<std::ptrdiff_t>(index));
        metrics_.record_engine_input_change();
        damage_published_readers(name);
        return;
    }
}

std::optional<double> SurfaceRoot::published_value(std::string_view name) const {
    for (const PublishedValue& published : published_values_) {
        if (published.name == name) return published.value;
    }
    return std::nullopt;
}

SurfaceRoot::EnginePaintPlace SurfaceRoot::engine_paint_place(const Control& control, const Painter& painter) const {
    EnginePaintPlace place{};
    const Painter* const recorder = recording_painter_;
    if (recording_control_ != nullptr && recorder == &painter) {
        place.painted_by = recording_control_;
        place.origin = (*recording_painter_).origin();
    } else {
        place.painted_by = &control;
    }
    const Rect bounds = absolute_bounds_of(*place.painted_by);
    place.window_origin = Point{bounds.x + place.origin.x, bounds.y + place.origin.y};
    return place;
}

void SurfaceRoot::note_engine_paint(PartRecordHandle handle, std::uint64_t inputs_read, const Control& painted_by,
                               Rect extent) {
    engine_parts_.note_paint(handle, inputs_read, painted_by.stable_id().value(), extent);
}

void SurfaceRoot::note_engine_pointer(const PointerEvent& event) {
    if (event.action == PointerAction::leave) {
        engine_pointer_.reset();
        return;
    }
    if (event.action == PointerAction::wheel) return;
    if (engine_pointer_ && *engine_pointer_ == event.position) return;
    engine_pointer_ = event.position;
    metrics_.record_engine_input_change();
    damage_engine_readers(pointer_input_bits, true);
}

bool SurfaceRoot::damage_engine_part(const EnginePartRecord& record, EngineDamage& damage) {
    const Control::Ptr recorded_by = find(record.painted_by);
    if (!recorded_by || (*recorded_by).surface_root_ != this || !(*recorded_by).is_alive()) return false;
    const Rect extent = Rect::intersection(record.extent, (*recorded_by).client_rectangle());
    if (extent.empty()) return false;
    (*recorded_by).invalidate(extent);
    damage.parts = damage.parts + 1U;
    damage.area = damage.area + extent.area();
    return true;
}

void SurfaceRoot::damage_engine_readers(std::uint64_t inputs, bool hot_only) {
    if (!engine_parts_.reads_any(inputs)) return;
    EngineDamage damage{};
    // By slot. An invalidation never makes or releases records.
    for (std::size_t slot = 0; slot < engine_parts_.slot_count(); ++slot) {
        const EnginePartRecord* const record = engine_parts_.record_in_slot(slot);
        if (record == nullptr || ((*record).inputs_read & inputs) == 0U) continue;
        if (hot_only && !(*record).states.contains(AnatomyState::hot)) continue;
        static_cast<void>(damage_engine_part(*record, damage));
    }
    if (damage.parts > 0U) metrics_.record_engine_input_damage(damage.parts, damage.area);
}

void SurfaceRoot::damage_backplane_readers(const Rect* damage) {
    backplane_generation_ = backplane_generation_ + 1U;
    EngineDamage damaged{};
    for (std::size_t slot = 0; slot < engine_parts_.slot_count(); ++slot) {
        const EnginePartRecord* const record = engine_parts_.record_in_slot(slot);
        if (record == nullptr || !(*record).reads_backplane) continue;
        if (damage != nullptr) {
            const Control::Ptr recorded_by = find((*record).painted_by);
            if (!recorded_by || (*recorded_by).surface_root_ != this) continue;
            const Rect bounds = absolute_bounds_of(*recorded_by);
            const Rect extent{bounds.x + (*record).extent.x, bounds.y + (*record).extent.y, (*record).extent.width,
                              (*record).extent.height};
            if (Rect::intersection(extent, *damage).empty()) continue;
        }
        static_cast<void>(damage_engine_part(*record, damaged));
    }
    if (damaged.parts > 0U) metrics_.record_engine_input_damage(damaged.parts, damaged.area);
}

void SurfaceRoot::damage_pattern_waiters(PatternGeneration generation) {
    switch (generation) {
    case PatternGeneration::none:
        // Nothing was made, so every raster that drew a ground is current.
        return;
    case PatternGeneration::made: {
        EngineDamage damaged{};
        for (std::size_t slot = 0; slot < engine_parts_.slot_count(); ++slot) {
            const EnginePartRecord* const record = engine_parts_.record_in_slot(slot);
            if (record == nullptr || !(*record).cached) continue;
            if (!engine_parts_.awaits_pattern(*(*record).cached)) continue;
            static_cast<void>(damage_engine_part(*record, damaged));
        }
        if (damaged.parts > 0U) metrics_.record_engine_input_damage(damaged.parts, damaged.area);
        return;
    }
    case PatternGeneration::made_unrecorded:
        // A part without a record drew a ground and recorded no place:
        // every control repaints, and glass sees the backplane again.
        mark_subtree_dirty(*root_, invalidation::paint_only);
        for (const std::shared_ptr<gui_forms::detail::OverlayAttachment>& overlay : overlays_) {
            if (const Control::Ptr overlay_root = (*overlay).overlay()) mark_subtree_dirty(*overlay_root, invalidation::paint_only);
        }
        damage_backplane_readers(nullptr);
        return;
    }
    // A generation outside the enumeration repaints nothing.
    return;
}

void SurfaceRoot::note_engine_backplane_reader(PartRecordHandle handle, bool reads) noexcept {
    engine_parts_.note_backplane_reader(handle, reads);
}

void SurfaceRoot::damage_published_readers(std::string_view name) {
    if (!engine_parts_.reads_any(published_input_bits)) return;
    EngineDamage damage{};
    for (std::size_t slot = 0; slot < engine_parts_.slot_count(); ++slot) {
        const EnginePartRecord* const record = engine_parts_.record_in_slot(slot);
        if (record == nullptr || ((*record).inputs_read & published_input_bits) == 0U) continue;
        // Where the value sits is the reading part's theme's business: a
        // control may show another theme than its window.
        const Control::Ptr owner = find((*record).control);
        if (!owner) continue;
        const CompiledThemeRules& rules = (*owner).effective_theme().engine().rules();
        const std::optional<ExpressionInputIndex> input = rules.published_value_input(name);
        if (!input || ((*record).inputs_read & expression_input_bit(*input)) == 0U) continue;
        static_cast<void>(damage_engine_part(*record, damage));
    }
    if (damage.parts > 0U) metrics_.record_engine_input_damage(damage.parts, damage.area);
}

} // namespace gui_forms
