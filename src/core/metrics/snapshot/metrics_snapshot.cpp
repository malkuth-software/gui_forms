#include "gui_forms/metrics/types/metrics_types.hpp"

#include <sstream>
#include <string_view>

namespace gui_forms {
namespace {

std::string escaped_json(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += character; break;
        }
    }
    return result;
}

} // namespace

std::string MetricsSnapshot::to_json() const {
    std::ostringstream output;
    output << '{'
           << "\"control_count\":" << control_count << ','
           << "\"stable_id_count\":" << stable_id_count << ','
           << "\"mutations\":" << mutations << ','
           << "\"dirty_marks\":" << dirty_marks << ','
           << "\"measure_passes\":" << measure_passes << ','
           << "\"arrange_passes\":" << arrange_passes << ','
           << "\"paint_passes\":" << paint_passes << ','
           << "\"controls_measured\":" << controls_measured << ','
           << "\"controls_arranged\":" << controls_arranged << ','
           << "\"controls_painted\":" << controls_painted << ','
           << "\"measure_nodes_visited\":" << measure_nodes_visited << ','
           << "\"arrange_nodes_visited\":" << arrange_nodes_visited << ','
           << "\"paint_nodes_visited\":" << paint_nodes_visited << ','
           << "\"measure_cache_hits\":" << measure_cache_hits << ','
           << "\"measure_cache_misses\":" << measure_cache_misses << ','
           << "\"maximum_measures_per_control_pass\":" << maximum_measures_per_control_pass << ','
           << "\"layout_boundary_stops\":" << layout_boundary_stops << ','
           << "\"layout_measure_clamps\":" << layout_measure_clamps << ','
           << "\"paint_invalidations_consumed\":" << paint_invalidations_consumed << ','
           << "\"display_chunks_rebuilt\":" << display_chunks_rebuilt << ','
           << "\"display_chunks_reused\":" << display_chunks_reused << ','
           << "\"display_commands_replayed\":" << display_commands_replayed << ','
           << "\"display_cache_entries\":" << display_cache_entries << ','
           << "\"display_generation\":" << display_generation << ','
           << "\"requested_damage_area\":" << requested_damage_area << ','
           << "\"painted_damage_area\":" << painted_damage_area << ','
           << "\"paint_bounding_area\":" << paint_bounding_area << ','
           << "\"damage_rectangles_painted\":" << damage_rectangles_painted << ','
           << "\"full_window_paints\":" << full_window_paints << ','
           << "\"partial_paints\":" << partial_paints << ','
           << "\"input_events\":" << input_events << ','
           << "\"focus_transitions\":" << focus_transitions << ','
           << "\"focus_scopes_opened\":" << focus_scopes_opened << ','
           << "\"focus_scopes_closed\":" << focus_scopes_closed << ','
           << "\"focus_scope_restorations\":" << focus_scope_restorations << ','
           << "\"focus_scope_rejections\":" << focus_scope_rejections << ','
           << "\"focus_scope_depth\":" << focus_scope_depth << ','
           << "\"maximum_focus_scope_depth\":" << maximum_focus_scope_depth << ','
           << "\"activations\":" << activations << ','
           << "\"disposals\":" << disposals << ','
           << "\"rejected_wrong_thread_operations\":" << rejected_wrong_thread_operations << ','
           << "\"subscriptions_connected\":" << subscriptions_connected << ','
           << "\"subscriptions_disconnected\":" << subscriptions_disconnected << ','
           << "\"callbacks_emitted\":" << callbacks_emitted << ','
           << "\"focus_revocations\":" << focus_revocations << ','
           << "\"capture_revocations\":" << capture_revocations << ','
           << "\"press_revocations\":" << press_revocations << ','
           << "\"undeclared_mutations\":" << undeclared_mutations << ','
           << "\"damage_region_compactions\":" << damage_region_compactions << ','
           << "\"damage_region_collapses\":" << damage_region_collapses << ','
           << "\"maximum_damage_rectangles\":" << maximum_damage_rectangles << ','
           << "\"update_scopes_started\":" << update_scopes_started << ','
           << "\"update_scope_depth\":" << update_scope_depth << ','
           << "\"maximum_update_scope_depth\":" << maximum_update_scope_depth << ','
           << "\"flush_count\":" << flush_count << ','
           << "\"read_barrier_flushes\":" << read_barrier_flushes << ','
           << "\"bounded_pass_limit_hits\":" << bounded_pass_limit_hits << ','
           << "\"callback_arbitration_retries\":"
           << callback_arbitration_retries << ','
           << "\"callback_arbitration_limit_hits\":"
           << callback_arbitration_limit_hits << ','
           << "\"scheduled_frame_requests\":" << scheduled_frame_requests << ','
           << "\"scheduler_wakes\":" << scheduler_wakes << ','
           << "\"ui_timer_ticks\":" << ui_timer_ticks << ','
           << "\"frame_deadlines_fired\":" << frame_deadlines_fired << ','
           << "\"motion_clock_ticks\":" << motion_clock_ticks << ','
           << "\"frame_requests_coalesced\":" << frame_requests_coalesced << ','
           << "\"frame_callback_faults\":" << frame_callback_faults << ','
           << "\"reentrant_frame_polls_deferred\":"
           << reentrant_frame_polls_deferred << ','
           << "\"occlusion_suspensions\":" << occlusion_suspensions << ','
           << "\"occlusion_resumes\":" << occlusion_resumes << ','
           << "\"occluded_frame_polls\":" << occluded_frame_polls << ','
           << "\"frames_presented\":" << frames_presented << ','
           << "\"present_duration_nanoseconds\":" << present_duration_nanoseconds << ','
           << "\"worst_present_duration_nanoseconds\":" << worst_present_duration_nanoseconds << ','
           << "\"retained_pixels_presented\":" << retained_pixels_presented << ','
           << "\"retained_rectangles_presented\":" << retained_rectangles_presented << ','
           << "\"live_frames_presented\":" << live_frames_presented << ','
           << "\"live_pixels_presented\":" << live_pixels_presented << ','
           << "\"live_rectangles_presented\":" << live_rectangles_presented << ','
           << "\"live_present_duration_nanoseconds\":" << live_present_duration_nanoseconds << ','
           << "\"live_pairing_waits\":" << live_pairing_waits << ','
           << "\"live_unpaired_presentations\":" << live_unpaired_presentations << ','
           << "\"screen_copy_nanoseconds\":" << screen_copy_nanoseconds << ','
           << "\"engine_part_records\":" << engine_part_records << ','
           << "\"engine_part_index_fallbacks\":" << engine_part_index_fallbacks << ','
           << "\"engine_part_records_released\":" << engine_part_records_released << ','
           << "\"engine_part_records_evicted\":" << engine_part_records_evicted << ','
           << "\"motion_values_moving\":" << motion_values_moving << ','
           << "\"motion_values_started\":" << motion_values_started << ','
           << "\"motion_values_settled\":" << motion_values_settled << ','
           << "\"motion_values_finished\":" << motion_values_finished << ','
           << "\"engine_input_changes\":" << engine_input_changes << ','
           << "\"engine_input_damaged_parts\":" << engine_input_damaged_parts << ','
           << "\"engine_input_damage_area\":" << engine_input_damage_area << ','
           << "\"engine_ambient_frames\":" << engine_ambient_frames << ','
           << "\"engine_ambient_refusals\":" << engine_ambient_refusals << ','
           << "\"effect_ambient_frames\":" << effect_ambient_frames << ','
           << "\"effects_unverified\":" << effects_unverified << ','
           << "\"engine_primitive_refusals\":" << engine_primitive_refusals << ','
           << "\"engine_material_assemblies\":" << engine_material_assemblies << ','
           << "\"paint_group_refusals\":" << paint_group_refusals << ','
           << "\"renderer_name\":\"" << escaped_json(renderer_name) << "\","
           << "\"cpu_only\":" << (cpu_only ? "true" : "false") << ','
           << "\"backplane_replay\":" << (backplane_replay ? "true" : "false")
           << '}';
    return output.str();
}

} // namespace gui_forms
