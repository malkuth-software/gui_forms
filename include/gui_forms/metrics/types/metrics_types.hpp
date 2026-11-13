#pragma once

#include "gui_forms/export/application_export.hpp"
#include <cstdint>
#include <string>

namespace gui_forms {

// Housekeeping expectations (owner, 2026-11-11). Housekeeping (the
// scheduler wake, paint ticks, live-surface drains, damage collection) never
// throws and is never treated as a fault; its cheap counters are checked
// against what it should have produced and how long it should have taken.
// A missed expectation is counted in the snapshot and reported as one
// rate-limited telemetry line on stderr: the 1st, 2nd, 4th, 8th... miss of
// each expectation prints, the others are counted only.
enum class HousekeepingExpectation : std::uint8_t {
    // A scheduled wake polled its due requests within late_wake_nanoseconds
    // of their deadline, so the frame it was scheduled for was produced.
    wake_on_time,
    // A paint tick (one SurfaceRoot::paint) finished within
    // paint_budget_nanoseconds.
    paint_within_budget,
    // A live-surface drain skipped at most live_drain_skipped_frames of its
    // producer's published frames since the previous one it presented.
    live_drain_kept_up,
    // A damage plane stayed within its rectangle capacity: it was not
    // collapsed to its bounds.
    damage_bounded
};

// What each expectation allows. Defaults are generous: they flag a stall,
// not an ordinary busy frame.
struct GUI_FORMS_API ExpectationThresholds final {
    std::uint64_t late_wake_nanoseconds{100'000'000U};
    std::uint64_t paint_budget_nanoseconds{50'000'000U};
    std::uint64_t live_drain_skipped_frames{8U};
};

struct GUI_FORMS_API MetricsSnapshot {
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
    // Arranges that changed a control's size, after its first, along an
    // axis its children's layout follows (Control::layout_resize_response).
    std::uint64_t resize_relayouts{};
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
    // Missed housekeeping expectations (HousekeepingExpectation), one
    // counter each, and the telemetry lines printed for them.
    std::uint64_t wake_expectation_misses{};
    std::uint64_t paint_budget_misses{};
    std::uint64_t live_drain_misses{};
    std::uint64_t damage_bound_misses{};
    std::uint64_t expectation_telemetry_records{};
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
    // Content pairing (Window::require_live_surface_content, stage S2): a
    // wait is one present tick at which a surface kept its previous frame
    // because its newest frame was older than the content it must show; an
    // unpaired presentation is a frame presented anyway after that wait.
    std::uint64_t live_pairing_waits{};
    std::uint64_t live_unpaired_presentations{};
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
    // Steps a dithered material's dither gave way so that its part stays
    // within the bake budget (EnginePrimitive::affordable_dither): one
    // for blue noise to the ordered table or the ordered table to none,
    // two for blue noise to none. Counted at every paint that lends the
    // lowered material, so a face keeps its material and loses only the
    // dither, never falling back to the built-in look for it.
    std::uint64_t engine_dither_degradations{};
    // Materials assembled from a recipe's material and the material fields
    // a theme's rules set (T3 EC1): once per distinct assembly for a keyed
    // part (AssembledPartMaterial), and at every paint of a part without a
    // key.
    std::uint64_t engine_material_assemblies{};
    // Paint groups begun deeper than maximum_paint_group_depth, drawn
    // without isolation (Painter::begin_group).
    std::uint64_t paint_group_refusals{};
    // Arrangements refused because the layout pass was arranging another
    // control (LayoutDiagnosticCode::arrange_outside_slot): a custom layout
    // that arranged a child directly instead of set_child_layout.
    std::uint64_t layout_arrange_refusals{};
    // Uses of a stale image identity refused: a window's scoped patch,
    // replace or update given one, and draws recorded with one (counted
    // when the control's paint is recorded, and drawn as nothing). A
    // replace or a generation-advancing patch returns the identity that
    // replaces the one passed in.
    std::uint64_t stale_image_refusals{};
    // Host font lookups in this process that found no bundled font directory
    // (bundled_font_diagnostics, read when the snapshot is taken): nonzero
    // means some window drew no bundled text; bundled_font_refusal says why.
    std::uint64_t bundled_font_lookups_without_source{};
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
