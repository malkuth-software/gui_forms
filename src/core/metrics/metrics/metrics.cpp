#include "gui_forms/metrics/metrics/metrics.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/metrics/font_sources/bundled_font_sources.hpp"

#include <algorithm>
#include <cstdio>

namespace gui_forms {

MetricsSnapshot Metrics::snapshot() const {
    MetricsSnapshot result = values_;
    result.rejected_wrong_thread_operations =
        wrong_thread_rejections_.load(std::memory_order_relaxed);
    result.effects_unverified = effect_load_diagnostics().unverified;
    result.bundled_font_lookups_without_source = bundled_font_diagnostics().lookups_without_source;
    return result;
}

void Metrics::reset_activity() noexcept {
    const std::uint64_t controls = values_.control_count;
    const std::uint64_t stable_ids = values_.stable_id_count;
    const std::uint64_t depth = values_.update_scope_depth;
    const std::uint64_t maximum_depth = values_.maximum_update_scope_depth;
    const std::uint64_t focus_scope_depth = values_.focus_scope_depth;
    const std::uint64_t maximum_focus_scope_depth = values_.maximum_focus_scope_depth;
    const std::string renderer = values_.renderer_name;
    const bool cpu_only = values_.cpu_only;
    const bool backplane_replay = values_.backplane_replay;
    const std::uint64_t display_cache_entries = values_.display_cache_entries;
    const std::uint64_t display_generation = values_.display_generation;
    const std::uint64_t engine_part_records = values_.engine_part_records;
    const std::uint64_t motion_values_moving = values_.motion_values_moving;
    values_ = {};
    values_.engine_part_records = engine_part_records;
    values_.motion_values_moving = motion_values_moving;
    values_.control_count = controls;
    values_.stable_id_count = stable_ids;
    values_.update_scope_depth = depth;
    values_.maximum_update_scope_depth = maximum_depth;
    values_.focus_scope_depth = focus_scope_depth;
    values_.maximum_focus_scope_depth = maximum_focus_scope_depth;
    values_.renderer_name = renderer;
    values_.cpu_only = cpu_only;
    values_.backplane_replay = backplane_replay;
    values_.display_cache_entries = display_cache_entries;
    values_.display_generation = display_generation;
    wrong_thread_rejections_.store(0, std::memory_order_relaxed);
}

void Metrics::set_population(std::uint64_t controls, std::uint64_t stable_ids) noexcept {
    values_.control_count = controls;
    values_.stable_id_count = stable_ids;
}
void Metrics::record_mutation() noexcept { ++values_.mutations; }
void Metrics::record_dirty_mark(double area) noexcept {
    ++values_.dirty_marks;
    values_.requested_damage_area += area;
}
void Metrics::record_requested_damage(double area) noexcept {
    values_.requested_damage_area += area;
}
void Metrics::record_measure(std::uint64_t nodes_visited,
                             std::uint64_t callbacks) noexcept {
    ++values_.measure_passes;
    values_.measure_nodes_visited += nodes_visited;
    values_.controls_measured += callbacks;
}
void Metrics::record_measure_cache_hit() noexcept { ++values_.measure_cache_hits; }
void Metrics::record_measure_cache_miss(std::uint64_t control_pass_misses) noexcept {
    ++values_.measure_cache_misses;
    values_.maximum_measures_per_control_pass =
        std::max(values_.maximum_measures_per_control_pass, control_pass_misses);
}
void Metrics::record_layout_boundary_stop() noexcept { ++values_.layout_boundary_stops; }
void Metrics::record_layout_measure_clamp() noexcept { ++values_.layout_measure_clamps; }
void Metrics::record_resize_relayout() noexcept { ++values_.resize_relayouts; }
void Metrics::record_arrange(std::uint64_t nodes_visited,
                             std::uint64_t callbacks) noexcept {
    ++values_.arrange_passes;
    values_.arrange_nodes_visited += nodes_visited;
    values_.controls_arranged += callbacks;
}
void Metrics::record_paint(std::uint64_t nodes_visited,
                           std::uint64_t controls,
                           std::uint64_t invalidations_consumed,
                           std::uint64_t chunks_rebuilt,
                           std::uint64_t chunks_reused,
                           std::uint64_t commands_replayed,
                           double area, double bounding_area,
                           std::uint64_t rectangles, bool full_window) noexcept {
    ++values_.paint_passes;
    values_.paint_nodes_visited += nodes_visited;
    values_.controls_painted += controls;
    values_.paint_invalidations_consumed += invalidations_consumed;
    values_.display_chunks_rebuilt += chunks_rebuilt;
    values_.display_chunks_reused += chunks_reused;
    values_.display_commands_replayed += commands_replayed;
    values_.painted_damage_area += area;
    values_.paint_bounding_area += bounding_area;
    values_.damage_rectangles_painted += rectangles;
    ++(full_window ? values_.full_window_paints : values_.partial_paints);
}
void Metrics::set_display_cache(std::uint64_t entries,
                                std::uint64_t generation) noexcept {
    values_.display_cache_entries = entries;
    values_.display_generation = generation;
}
void Metrics::record_input() noexcept { ++values_.input_events; }
void Metrics::record_focus_transition() noexcept { ++values_.focus_transitions; }
void Metrics::record_focus_scope_opened(std::size_t depth) noexcept {
    ++values_.focus_scopes_opened;
    values_.focus_scope_depth = static_cast<std::uint64_t>(depth);
    values_.maximum_focus_scope_depth =
        std::max(values_.maximum_focus_scope_depth,
                 static_cast<std::uint64_t>(depth));
}
void Metrics::record_focus_scope_closed(bool restored_focus,
                                        std::size_t depth) noexcept {
    ++values_.focus_scopes_closed;
    values_.focus_scope_depth = static_cast<std::uint64_t>(depth);
    if (restored_focus) {
        ++values_.focus_scope_restorations;
    }
}
void Metrics::record_focus_scope_rejection() noexcept {
    ++values_.focus_scope_rejections;
}
void Metrics::record_activation() noexcept { ++values_.activations; }
void Metrics::record_disposal(std::uint64_t count) noexcept { values_.disposals += count; }

void Metrics::record_engine_part_created(bool index_fallback, std::uint64_t live) noexcept {
    if (index_fallback) values_.engine_part_index_fallbacks = values_.engine_part_index_fallbacks + 1U;
    values_.engine_part_records = live;
}

void Metrics::record_engine_parts_released(std::uint64_t released, std::uint64_t live) noexcept {
    values_.engine_part_records_released = values_.engine_part_records_released + released;
    values_.engine_part_records = live;
}

void Metrics::record_engine_parts_evicted(std::uint64_t evicted, std::uint64_t live) noexcept {
    values_.engine_part_records_evicted = values_.engine_part_records_evicted + evicted;
    values_.engine_part_records = live;
}
void Metrics::record_wrong_thread_rejection() noexcept {
    wrong_thread_rejections_.fetch_add(1, std::memory_order_relaxed);
}
void Metrics::record_subscription_connected() noexcept {
    ++values_.subscriptions_connected;
}
void Metrics::record_subscription_disconnected() noexcept {
    ++values_.subscriptions_disconnected;
}
void Metrics::record_callback_emitted(std::uint64_t count) noexcept {
    values_.callbacks_emitted += count;
}
void Metrics::record_focus_revocation() noexcept { ++values_.focus_revocations; }
void Metrics::record_capture_revocation() noexcept { ++values_.capture_revocations; }
void Metrics::record_press_revocation() noexcept { ++values_.press_revocations; }
void Metrics::record_undeclared_mutation() noexcept { ++values_.undeclared_mutations; }
void Metrics::record_damage_region(std::size_t rectangle_count,
                                   std::uint64_t compactions,
                                   std::uint64_t collapses) noexcept {
    values_.damage_region_compactions += compactions;
    values_.damage_region_collapses += collapses;
    values_.maximum_damage_rectangles =
        std::max(values_.maximum_damage_rectangles,
                 static_cast<std::uint64_t>(rectangle_count));
}
void Metrics::enter_update_scope() noexcept {
    ++values_.update_scopes_started;
    ++values_.update_scope_depth;
    values_.maximum_update_scope_depth =
        std::max(values_.maximum_update_scope_depth, values_.update_scope_depth);
}
void Metrics::leave_update_scope() noexcept {
    if (values_.update_scope_depth > 0) {
        --values_.update_scope_depth;
    }
}
void Metrics::record_flush(bool read_barrier) noexcept {
    ++values_.flush_count;
    if (read_barrier) {
        ++values_.read_barrier_flushes;
    }
}
void Metrics::record_pass_limit_hit() noexcept { ++values_.bounded_pass_limit_hits; }
void Metrics::record_callback_arbitration_retry(bool limit_hit) noexcept {
    ++values_.callback_arbitration_retries;
    if (limit_hit) ++values_.callback_arbitration_limit_hits;
}
void Metrics::record_frame_request() noexcept { ++values_.scheduled_frame_requests; }
void Metrics::record_frame_poll(std::uint64_t deadlines_fired,
                                std::uint64_t motion_clock_ticks,
                                std::uint64_t ui_timer_ticks,
                                std::uint64_t coalesced_requests) noexcept {
    if (deadlines_fired != 0 || motion_clock_ticks != 0) {
        ++values_.scheduler_wakes;
    }
    values_.frame_deadlines_fired += deadlines_fired;
    values_.motion_clock_ticks += motion_clock_ticks;
    values_.ui_timer_ticks += ui_timer_ticks;
    values_.frame_requests_coalesced += coalesced_requests;
}
void Metrics::record_motion_started() noexcept {
    values_.motion_values_started = values_.motion_values_started + 1U;
}
void Metrics::record_motion_settled() noexcept {
    values_.motion_values_settled = values_.motion_values_settled + 1U;
}
void Metrics::record_motion_finished() noexcept {
    values_.motion_values_finished = values_.motion_values_finished + 1U;
}
void Metrics::set_motion_values_moving(std::uint64_t moving) noexcept {
    values_.motion_values_moving = moving;
}
void Metrics::record_engine_input_change() noexcept {
    values_.engine_input_changes = values_.engine_input_changes + 1U;
}
void Metrics::record_engine_input_damage(std::uint64_t parts, double area) noexcept {
    values_.engine_input_damaged_parts = values_.engine_input_damaged_parts + parts;
    values_.engine_input_damage_area = values_.engine_input_damage_area + area;
}
void Metrics::record_engine_ambient_frame() noexcept {
    values_.engine_ambient_frames = values_.engine_ambient_frames + 1U;
}
void Metrics::record_engine_ambient_refusal() noexcept {
    values_.engine_ambient_refusals = values_.engine_ambient_refusals + 1U;
}
void Metrics::record_effect_ambient_frame() noexcept {
    values_.effect_ambient_frames = values_.effect_ambient_frames + 1U;
}
void Metrics::record_engine_primitive_refusal() noexcept {
    values_.engine_primitive_refusals = values_.engine_primitive_refusals + 1U;
}
void Metrics::record_engine_material_assembly() noexcept {
    values_.engine_material_assemblies = values_.engine_material_assemblies + 1U;
}
void Metrics::record_paint_group_refusals(std::uint64_t refusals) noexcept {
    values_.paint_group_refusals = values_.paint_group_refusals + refusals;
}
namespace {

const char* expectation_name(HousekeepingExpectation expectation) noexcept {
    switch (expectation) {
    case HousekeepingExpectation::wake_on_time: return "wake_on_time";
    case HousekeepingExpectation::paint_within_budget: return "paint_within_budget";
    case HousekeepingExpectation::live_drain_kept_up: return "live_drain_kept_up";
    case HousekeepingExpectation::damage_bounded: return "damage_bounded";
    }
    return "unknown";
}

const char* expectation_unit(HousekeepingExpectation expectation) noexcept {
    switch (expectation) {
    case HousekeepingExpectation::wake_on_time: return "ns";
    case HousekeepingExpectation::paint_within_budget: return "ns";
    case HousekeepingExpectation::live_drain_kept_up: return "frames";
    case HousekeepingExpectation::damage_bounded: return "rectangles";
    }
    return "";
}

// The 1st, 2nd, 4th, 8th... miss prints: a power of two.
bool reported_miss(std::uint64_t misses) noexcept {
    return misses != 0U && (misses & (misses - 1U)) == 0U;
}

} // namespace

void Metrics::record_expectation_miss(HousekeepingExpectation expectation, std::uint64_t expected,
                                      std::uint64_t observed) noexcept {
    std::uint64_t* misses = &values_.wake_expectation_misses;
    switch (expectation) {
    case HousekeepingExpectation::wake_on_time: misses = &values_.wake_expectation_misses; break;
    case HousekeepingExpectation::paint_within_budget: misses = &values_.paint_budget_misses; break;
    case HousekeepingExpectation::live_drain_kept_up: misses = &values_.live_drain_misses; break;
    case HousekeepingExpectation::damage_bounded: misses = &values_.damage_bound_misses; break;
    }
    *misses = *misses + 1U;
    if (!reported_miss(*misses)) return;
    values_.expectation_telemetry_records = values_.expectation_telemetry_records + 1U;
    const char* const unit = expectation_unit(expectation);
    std::fprintf(stderr,
                 "gui-forms-telemetry=expectation|name:%s|expected:%llu%s|observed:%llu%s|misses:%llu"
                 "|paint_passes:%llu|frames_presented:%llu|scheduler_wakes:%llu|flushes:%llu\n",
                 expectation_name(expectation), static_cast<unsigned long long>(expected), unit,
                 static_cast<unsigned long long>(observed), unit, static_cast<unsigned long long>(*misses),
                 static_cast<unsigned long long>(values_.paint_passes),
                 static_cast<unsigned long long>(values_.frames_presented),
                 static_cast<unsigned long long>(values_.scheduler_wakes),
                 static_cast<unsigned long long>(values_.flush_count));
    std::fflush(stderr);
}
const ExpectationThresholds& Metrics::expectation_thresholds() const noexcept {
    return thresholds_;
}
void Metrics::set_expectation_thresholds(const ExpectationThresholds& thresholds) noexcept {
    thresholds_ = thresholds;
}
void Metrics::record_reentrant_frame_poll() noexcept {
    ++values_.reentrant_frame_polls_deferred;
}
void Metrics::record_occlusion_transition(bool occluded) noexcept {
    ++(occluded ? values_.occlusion_suspensions : values_.occlusion_resumes);
}
void Metrics::record_occluded_frame_poll() noexcept {
    ++values_.occluded_frame_polls;
}
void Metrics::record_present(std::uint64_t duration) noexcept {
    ++values_.frames_presented;
    values_.present_duration_nanoseconds += duration;
    values_.worst_present_duration_nanoseconds =
        std::max(values_.worst_present_duration_nanoseconds, duration);
}
void Metrics::record_present(std::uint64_t duration, std::uint64_t device_pixels,
                             std::uint64_t rectangles) noexcept {
    record_present(duration);
    values_.retained_pixels_presented += device_pixels;
    values_.retained_rectangles_presented += rectangles;
}
void Metrics::record_screen_copy(std::uint64_t nanoseconds) noexcept {
    values_.screen_copy_nanoseconds += nanoseconds;
}
void Metrics::record_live_present(std::uint64_t duration, std::uint64_t device_pixels,
                                  std::uint64_t rectangles) noexcept {
    ++values_.live_frames_presented;
    values_.live_pixels_presented += device_pixels;
    values_.live_rectangles_presented += rectangles;
    values_.live_present_duration_nanoseconds += duration;
}
void Metrics::record_live_pairing_wait() noexcept {
    ++values_.live_pairing_waits;
}
void Metrics::record_live_unpaired_presentation() noexcept {
    ++values_.live_unpaired_presentations;
}
void Metrics::set_renderer(std::string_view name, bool cpu_only) {
    values_.renderer_name.assign(name);
    values_.cpu_only = cpu_only;
}
void Metrics::set_backplane_replay(bool replayed) noexcept {
    values_.backplane_replay = replayed;
}

} // namespace gui_forms
