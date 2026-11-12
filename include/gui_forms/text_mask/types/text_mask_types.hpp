#pragma once

#include "gui_forms/export/application_export.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace gui_forms {

enum class TextMaskRaster { outline_gray, true_mono };
enum class TextMaskStatus {
    success, cache_miss, pending, busy, invalid_input, unsupported_profile,
    missing_font_coverage, limit_exceeded, cancelled, stale, closing, closed,
    wrong_executor, native_failure, resource_failure, identifier_exhausted
};
enum class TextMaskLimit {
    none, input_bytes, line_count, key_bytes, metadata_bytes, mask_dimension,
    mask_bytes, live_mask_bytes, live_mask_objects, workspace, shape_work,
    source_key_objects, request_slots, font_bytes, font_banks, shaping_payload,
    // The mask cache's entry count.
    cache_entries
};
enum class TextMaskSlot { empty, dispatched, queued, running, completed, retiring };
enum class TextMaskRetirement { released, pending_retirement };
struct GUI_FORMS_API TextMaskRequestId { std::uint64_t session{0}; std::uint64_t serial{0}; };
struct GUI_FORMS_API TextMaskRequest {
    std::string_view utf8{}; // Borrowed only for the lookup/submit call.
    std::uint32_t primary_face{0};
    double size{16.0};
    double wrap_width{0.0};
    double device_scale{1.0};
    TextMaskRaster raster{TextMaskRaster::outline_gray};
};
struct GUI_FORMS_API TextMaskResult {
    TextMaskStatus status{TextMaskStatus::invalid_input};
    TextMaskLimit limit{TextMaskLimit::none};
};
struct GUI_FORMS_API TextMaskCancellation {
    TextMaskResult result{};
    TextMaskRetirement retirement{TextMaskRetirement::released};
};
struct GUI_FORMS_API TextMaskLine {
    std::uint32_t source_begin{0};
    std::uint32_t text_end{0};
    std::uint32_t consumed_end{0};
    double baseline{0.0};
    double advance{0.0};
    double ascent{0.0};
    double descent{0.0};
    bool hard_break{false};
    bool horizontal_overflow{false};
};
struct GUI_FORMS_API TextMaskMetrics {
    std::int32_t size_64{0};
    std::int32_t wrap_width_64{0};
    std::int32_t additional_gap_64{0};
    double logical_width{0.0};
    double logical_height{0.0};
    double device_scale{1.0};
    std::int32_t ink_left_px{0};
    std::int32_t ink_top_px{0};
    std::uint32_t width_px{0};
    std::uint32_t height_px{0};
    std::size_t stride_bytes{0};
    bool horizontal_overflow{false};
};
struct GUI_FORMS_API TextMaskRequestSnapshot {
    TextMaskRequestId id{};
    TextMaskSlot state{TextMaskSlot::empty};
    TextMaskResult completion{}; // Meaningful only for completed slots.
};
struct GUI_FORMS_API TextMaskSessionSnapshot {
    std::array<TextMaskRequestSnapshot, 9> requests{};
    std::size_t occupied{0};
    std::size_t assigned{0}; // Dispatched/running lane; at most one.
    std::size_t queued{0};
    std::size_t completed{0};
    std::size_t retiring{0};
    bool closing{false};
    bool joined{false};
};
struct GUI_FORMS_API TextMaskByteUsage {
    std::size_t live{0};
    std::size_t reserved{0};
    std::size_t peak_admitted{0}; // Peak live + reserved, in bytes.
    std::size_t limit{0};
};
struct GUI_FORMS_API TextMaskCountUsage {
    std::size_t live{0};
    std::size_t reserved{0};
    std::size_t peak_admitted{0}; // Peak live + reserved, in objects/records.
    std::size_t limit{0};
};
struct GUI_FORMS_API TextMaskBudgetSnapshot {
    TextMaskByteUsage utf8{};
    TextMaskByteUsage metadata{};
    TextMaskByteUsage coverage{};
    TextMaskByteUsage shaping_payload{};
    TextMaskByteUsage workspace{};
    TextMaskByteUsage encoded_fonts{};
    TextMaskCountUsage masks{};
    TextMaskCountUsage source_keys{};
    TextMaskCountUsage cache_records{};
    TextMaskCountUsage request_slots{};
    TextMaskCountUsage font_banks{};
};

} // namespace gui_forms
