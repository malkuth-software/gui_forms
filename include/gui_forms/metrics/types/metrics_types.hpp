#pragma once

#include <cstdint>
#include <string>

namespace gui_forms {

struct MetricsSnapshot {
    std::uint64_t control_count{};
    std::uint64_t stable_id_count{};
    std::uint64_t mutations{};
    std::uint64_t dirty_marks{};
    std::uint64_t measure_passes{};
    std::uint64_t arrange_passes{};
    std::uint64_t paint_passes{};
    std::uint64_t controls_measured{};
    std::uint64_t controls_arranged{};
    std::uint64_t controls_painted{};
    std::uint64_t measure_nodes_visited{};
    std::uint64_t arrange_nodes_visited{};
    std::uint64_t paint_nodes_visited{};
    // Control::measure_for_layout (stage L): answers from a control's measure
    // cache, misses that called measure_intrinsic, and the most misses one
    // control answered within one layout pass (a solver asking more than
    // once per pass shows here). Measure calls that bypass the entry are
    // counted only by controls_measured.
    std::uint64_t measure_cache_hits{};
    std::uint64_t measure_cache_misses{};
    std::uint64_t maximum_measures_per_control_pass{};
    // Measure propagations stopped at a layout boundary
    // (Control::size_depends_on_content), and measure results with a
    // negative or non-finite dimension clamped to zero.
    std::uint64_t layout_boundary_stops{};
    std::uint64_t layout_measure_clamps{};
    std::uint64_t paint_invalidations_consumed{};
    std::uint64_t display_chunks_rebuilt{};
    std::uint64_t display_chunks_reused{};
    std::uint64_t display_commands_replayed{};
    std::uint64_t display_cache_entries{};
    std::uint64_t display_generation{};
    double requested_damage_area{};
    double painted_damage_area{};
    // The summed bounding box of each pass's painted rectangles. A sparse
    // frame paints far less than its bounding box; equal values mean the
    // rectangles were adjacent or the host requested one box.
    double paint_bounding_area{};
    std::uint64_t damage_rectangles_painted{};
    std::uint64_t full_window_paints{};
    std::uint64_t partial_paints{};
    std::uint64_t input_events{};
    std::uint64_t focus_transitions{};
    std::uint64_t focus_scopes_opened{};
    std::uint64_t focus_scopes_closed{};
    std::uint64_t focus_scope_restorations{};
    std::uint64_t focus_scope_rejections{};
    std::uint64_t focus_scope_depth{};
    std::uint64_t maximum_focus_scope_depth{};
    std::uint64_t activations{};
    std::uint64_t disposals{};
    std::uint64_t rejected_wrong_thread_operations{};
    std::uint64_t subscriptions_connected{};
    std::uint64_t subscriptions_disconnected{};
    std::uint64_t callbacks_emitted{};
    std::uint64_t focus_revocations{};
    std::uint64_t capture_revocations{};
    std::uint64_t press_revocations{};
    std::uint64_t undeclared_mutations{};
    std::uint64_t damage_region_compactions{};
    std::uint64_t damage_region_collapses{};
    std::uint64_t maximum_damage_rectangles{};
    std::uint64_t update_scopes_started{};
    std::uint64_t update_scope_depth{};
    std::uint64_t maximum_update_scope_depth{};
    std::uint64_t flush_count{};
    std::uint64_t read_barrier_flushes{};
    std::uint64_t bounded_pass_limit_hits{};
    std::uint64_t callback_arbitration_retries{};
    std::uint64_t callback_arbitration_limit_hits{};
    std::uint64_t scheduled_frame_requests{};
    // Frame polls that fired a frame deadline or a motion clock tick. UI
    // timer callbacks are counted apart, in ui_timer_ticks, so an idle check
    // sees each cause of a wake (owner, 2026-11-01).
    std::uint64_t scheduler_wakes{};
    // UI timer callbacks fired (Window::schedule_ui_timer and the helpers
    // built on it, such as AnimationTimer): one per due timer per poll; ticks
    // skipped by a late poll are counted in frame_requests_coalesced.
    std::uint64_t ui_timer_ticks{};
    std::uint64_t frame_deadlines_fired{};
    // Frames of the window's motion clock (T2 plan, phase E3): at most one
    // per frame poll, however many motion values move.
    std::uint64_t motion_clock_ticks{};
    std::uint64_t frame_requests_coalesced{};
    std::uint64_t frame_callback_faults{};
    std::uint64_t reentrant_frame_polls_deferred{};
    std::uint64_t occlusion_suspensions{};
    std::uint64_t occlusion_resumes{};
    std::uint64_t occluded_frame_polls{};
    std::uint64_t frames_presented{};
    std::uint64_t present_duration_nanoseconds{};
    std::uint64_t worst_present_duration_nanoseconds{};
    // What hosts copied to the screen (stage DP), in device pixels: four bytes
    // each in today's 32-bit rasters. Retained presents are reported through
    // Window::notify_presented and live-surface frames through
    // Window::notify_live_presented, so a frame's copy cost is a number that
    // compares across hosts. A host that does not report areas leaves the
    // pixel and rectangle counters at zero. A live frame is one surface's new
    // frame; the core splits its clip around overlays, and those fragments
    // are its rectangles. Durations are split between retained and live by
    // the pixels each copied.
    std::uint64_t retained_pixels_presented{};
    std::uint64_t retained_rectangles_presented{};
    std::uint64_t live_frames_presented{};
    std::uint64_t live_pixels_presented{};
    std::uint64_t live_rectangles_presented{};
    std::uint64_t live_present_duration_nanoseconds{};
    // The time hosts spent only copying pixels to the screen (conversion and
    // the native copy), retained and live together. The present durations
    // above also cover painting and composing; this one compares the copy
    // itself across hosts.
    std::uint64_t screen_copy_nanoseconds{};
    // The theme engine's part records (T2 plan, EnginePartTable): those
    // live now, those made with the index fallback (an item part without a
    // stable ID, whose record follows its position rather than the item;
    // GIVEN H3-1), and those released because their control left the
    // window. Records made, released and evicted are activity; the live
    // count is population and survives reset_activity.
    std::uint64_t engine_part_records{};
    std::uint64_t engine_part_index_fallbacks{};
    std::uint64_t engine_part_records_released{};
    // Item-part records evicted, least recently painted first, to keep
    // within the window's cap (maximum_item_part_records).
    std::uint64_t engine_part_records_evicted{};
    // The motion clock's values (T2 plan, phase E3): those moving now, moves
    // started (a retarget is a start), those that reached their settled
    // condition on a frame, and those that jumped to their end at once
    // (reduced motion, an occluded or minimized window, a zero duration, or
    // an end by finish_all). The moving count is population and survives
    // reset_activity; zero means the window schedules no motion frames.
    std::uint64_t motion_values_moving{};
    std::uint64_t motion_values_started{};
    std::uint64_t motion_values_settled{};
    std::uint64_t motion_values_finished{};
    // The theme engine's inputs (T2 plan, phase E6): changes the engine saw
    // to an input a part may read (a pointer move inside the window, a
    // value published or withdrawn), the parts those changes damaged
    // because they read the input, and the logical area of that damage. A
    // change no part reads damages nothing.
    std::uint64_t engine_input_changes{};
    std::uint64_t engine_input_damaged_parts{};
    double engine_input_damage_area{};
    // The theme engine's ambient effects (E7): frames that repainted an
    // ambient part, and effects refused for covering more than 10 % of the
    // window on an everyday surface (counted when the refusal begins).
    std::uint64_t engine_ambient_frames{};
    std::uint64_t engine_ambient_refusals{};
    // Effect programs (design note section 7): ambient frames that advanced
    // a control's effects, and effects registered unverified in this
    // process (effect_load_diagnostics, read when the snapshot is taken).
    std::uint64_t effect_ambient_frames{};
    std::uint64_t effects_unverified{};
    // Parts a primitive refused to draw (E8c, EnginePrimitive::accepts),
    // such as a material whose bake would exceed its bound: drawn by the
    // built-in recipe instead. Counted when a keyed part's refusal begins,
    // and at every paint of a part without a key.
    std::uint64_t engine_primitive_refusals{};
    // Materials assembled from a recipe's material and the material fields
    // a theme's rules set (T3 EC1): once per distinct assembly for a keyed
    // part (AssembledPartMaterial), and at every paint of a part without a
    // key.
    std::uint64_t engine_material_assemblies{};
    // Paint groups begun deeper than maximum_paint_group_depth, drawn
    // without isolation (Painter::begin_group).
    std::uint64_t paint_group_refusals{};
    std::string renderer_name{"unbound"};
    bool cpu_only{true};
    // The renderer's backplane replay (stage T5, C6; owner decision
    // T5-Q13): true once the window's renderer replayed a backplane image
    // whole, false while it has not or could not (the part-raster image of
    // E9-B is used instead). Kept across resets, as the renderer is.
    bool backplane_replay{};

    [[nodiscard]] std::string to_json() const;
};

} // namespace gui_forms
