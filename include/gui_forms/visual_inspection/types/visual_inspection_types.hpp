#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// Public, renderer-neutral names for the retained display operations exposed
// by visual inspection. These names describe GUI.Forms vocabulary; they never
// expose a Skia, AppKit, Win32, or other backend object.
enum class VisualPaintOperation : std::uint8_t {
    save,
    restore,
    translate,
    clip_rect,
    clip_rounded_rect,
    fill_rect,
    fill_rounded_rect,
    stroke_rect,
    stroke_rounded_rect,
    fill_linear_gradient,
    fill_linear_gradient_spread,
    fill_radial_gradient,
    draw_box_shadow,
    draw_inset_box_shadow,
    draw_line,
    draw_text,
    draw_image,
    draw_image_region,
    draw_image_region_sampled,
    fill_image_pattern,
    draw_live_surface,
    draw_part_raster,
    // A paint group: `rect` its bounds, `scalar` its opacity.
    begin_group,
    end_group,
    draw_prepared_text,
};

[[nodiscard]] const char* visual_paint_operation_name(
    VisualPaintOperation operation) noexcept;
[[nodiscard]] const char* paint_plane_name(PaintPlane plane) noexcept;
[[nodiscard]] const char* font_role_name(FontRole role) noexcept;
[[nodiscard]] const char* dock_style_name(DockStyle dock) noexcept;
[[nodiscard]] const char* visual_status_name(
    ControlVisualStatus status) noexcept;

// One immutable, backend-neutral record from the last committed display chunk.
// Text is redacted by default at capture time; byte length and the resolved
// FontSpec remain available for visual diagnosis without copying application
// content into ordinary diagnostic payloads.
struct VisualPaintOperationSnapshot final {
    std::size_t command_index{};
    VisualPaintOperation operation{VisualPaintOperation::save};
    Point first{};
    Point second{};
    Rect rect{};
    Color color{};
    FontSpec font{};
    std::optional<ResolvedTextLayout> resolved_text;
    ImageId image{};
    double scalar{};
    double secondary_scalar{};
    double tertiary_scalar{};
    std::vector<GradientStop> gradient_stops;
    GradientSpreadMode gradient_spread{GradientSpreadMode::pad};
    ImagePatternWrap image_pattern_wrap{ImagePatternWrap::tile};
    ImageSampling image_sampling{ImageSampling::linear};
    std::size_t text_byte_count{};
    std::string text;
    // The reading direction of a draw_text operation.
    TextOrientation text_orientation{TextOrientation::horizontal};
};

struct VisualControlStateSnapshot final {
    bool visible{};
    bool layout_collapsed{};
    bool effectively_visible{};
    bool enabled{};
    bool effectively_enabled{};
    bool focusable{};
    bool focused{};
    bool focus_cue_visible{};
    bool hovered{};
    bool pressed{};
    bool pointer_captured{};
    bool hit_test_transparent{};
    bool window_active{};
    bool initializing{};
    ControlVisualStatus visual_status{ControlVisualStatus::normal};
};

struct VisualControlLayoutSnapshot final {
    Rect requested_bounds{};
    Rect arranged_bounds{};
    Rect absolute_bounds{};
    Rect client_rectangle{};
    Rect display_rectangle{};
    Rect child_viewport{};
    Rect visual_bounds{};
    Rect effective_clip{};
    Insets visual_outsets{};
    Insets margin{};
    Insets padding{};
    Size minimum_size{};
    Size maximum_size{};
    DockStyle dock{DockStyle::none};
    AnchorStyles anchor{AnchorStyles::none};
    bool auto_size{};
    bool clipped_by_ancestor{};
    bool fully_clipped{};
    std::size_t ancestor_clip_count{};
    LayoutTransactionState transaction{};
};

struct VisualControlInspection final {
    RuntimeId runtime_id{};
    std::string stable_id;
    std::optional<RuntimeId> parent_runtime_id;
    std::string parent_stable_id;
    std::size_t depth{};
    bool overlay_tree{};
    VisualControlLayoutSnapshot layout;
    VisualControlStateSnapshot state;
    Dirty dirty{Dirty::none};
    Dirty subtree_dirty{Dirty::none};
    PaintPlane paint_plane{PaintPlane::control};
    std::string theme_id;
    bool theme_override{};
    std::optional<SurfaceMaterial> authored_material;
    std::optional<DisplayChunkInfo> display_chunk;
    bool display_chunk_current{};
    std::size_t total_display_operations{};
    bool display_operations_truncated{};
    std::vector<VisualPaintOperationSnapshot> display_operations;
};

struct VisualInspectionOptions final {
    static constexpr std::size_t maximum_allowed_controls = 1'000'000U;
    static constexpr std::size_t maximum_allowed_operations_per_control = 16'384U;
    std::size_t maximum_controls{16'384U};
    std::size_t maximum_operations_per_control{512U};
    bool include_display_operations{true};
    bool include_text{false};
    bool include_resolved_text{true};
};

struct VisualInspectionSnapshot final {
    std::uint64_t content_revision{};
    std::uint64_t display_generation{};
    Size client_size{};
    double device_scale{1.0};
    double text_scale{1.0};
    bool high_contrast{};
    bool reduced_motion{};
    bool sound_enabled{true};
    std::string theme_id;
    bool window_active{};
    bool window_occluded{};
    PaintLeaseSnapshot paint_lease;
    std::array<std::vector<Rect>, paint_plane_count> pending_damage;
    std::size_t total_controls{};
    bool controls_truncated{};
    std::vector<VisualControlInspection> controls;

    [[nodiscard]] const VisualControlInspection* find(
        std::string_view stable_id) const noexcept;
    [[nodiscard]] std::string to_json() const;
};

} // namespace gui_forms
