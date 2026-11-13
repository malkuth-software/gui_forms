#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/document_view/types/document_view_types.hpp"
#include "gui_forms/types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace gui_forms {

enum class PreparedTextStatus {
    success, pending, busy, invalid_input, stale, cancelled, closing, closed,
    generation_exhausted, context_required, budget_exceeded, unsupported_profile,
    missing_font_coverage, incompatible_font, invalid_geometry, resource_failure,
    native_failure, wrong_executor, incompatible_backend
};

enum class PreparedTextPaintDisposition { refused, recorded, staged };
struct GUI_FORMS_API PreparedTextPaintResult final {
    PreparedTextPaintDisposition disposition{PreparedTextPaintDisposition::refused};
    PreparedTextStatus status{PreparedTextStatus::incompatible_backend};
};

enum class PreparedTextSlot { empty, queued, running, ready };
// ADR-004: light_gray_v2 is FreeType light auto-hinting, grayscale, the one
// GUI.Forms text profile. outline_gray_72dpi_v1 (bytecode hinting) is retired
// and refused as unsupported.
enum class PreparedRasterProfile : std::uint32_t { outline_gray_72dpi_v1 = 1, light_gray_v2 = 2 };

struct GUI_FORMS_API LayoutAuthority final {
    // Process-wide nonreused session identity, with checked exhaustion.
    std::uint64_t session{};
    std::uint64_t epoch{};
};

struct GUI_FORMS_API PreparedTextKey final {
    DocumentPageRequest page{};
    SourceByteRange source{};
    DisplayByteOffset display_begin{};
    DisplayByteOffset display_end{};
    std::uint64_t layout_serial{};
    std::uint64_t provider_instance{};
    std::uint64_t provider_generation{1};
    std::uint64_t font_set{};
    std::uint64_t font_generation{};
    std::uint64_t context_generation{};
    FontSpec font{};
    double scale{1.0};
    double wrap_width{};
    std::uint32_t tab_columns{4};
    PreparedRasterProfile raster{PreparedRasterProfile::light_gray_v2};
};

struct GUI_FORMS_API PreparedSourceEndpoint final {
    SourceByteOffset source{};
    DisplayByteOffset display{};
};

// Producer certifies real paragraph boundaries under the complete D1 token.
// The provider validates paired endpoints/mapping, never fabricates source proof.
struct GUI_FORMS_API PreparedParagraphProof final {
    bool complete_begin{};
    bool complete_end{};
};

struct GUI_FORMS_API PreparedFontSource final {
    std::span<const std::byte> encoded{};
    std::optional<FontRole> role{};
    std::uint16_t weight{400};
    bool italic{};
    std::uint32_t face_index{};
};

struct GUI_FORMS_API PreparedTextLimits final {
    static constexpr std::size_t display_bytes = 16'384;
    static constexpr std::size_t metadata_records = 16'385;
    static constexpr std::size_t metadata_bytes = 1U * 1024U * 1024U;
    static constexpr std::size_t payload_bytes = 8U * 1024U * 1024U;
    static constexpr std::size_t payload_generations = 3;
    static constexpr std::size_t workspace_bytes = 16U * 1024U * 1024U;
    static constexpr std::size_t font_faces = 8;
    static constexpr std::size_t font_face_bytes = 4U * 1024U * 1024U;
    static constexpr std::size_t font_bytes = 8U * 1024U * 1024U;
    static constexpr std::size_t font_banks = 2;
    static constexpr std::size_t mask_dimension = 4096;
    static constexpr std::size_t mask_bytes = 4U * 1024U * 1024U;
    static constexpr std::size_t mask_owners = 2;
};

struct GUI_FORMS_API PreparedTextMetrics final {
    double advance_dip{};
    double height_dip{};
    double ascent_dip{};
    double descent_dip{};
    double device_scale{1.0};
    std::int64_t device_size_26_6{};
};

struct GUI_FORMS_API PreparedTextBudgetSnapshot final {
    std::size_t payload_generations{};
    std::size_t payload_reserved_bytes{};
    std::size_t font_banks{};
    std::size_t font_bytes{};
    std::size_t input_owners{};
    std::size_t input_bytes{};
    std::size_t mask_owners{};
    std::size_t mask_bytes{};
    // Reports requested controlled payload only, not allocator/vendor/RSS quota.
};

struct GUI_FORMS_API PreparedTextSessionSnapshot final {
    PreparedTextSlot slot{PreparedTextSlot::empty};
    PreparedTextStatus completion{PreparedTextStatus::pending};
    LayoutAuthority desired{};
    bool closing{};
    bool joined{};
};

[[nodiscard]] GUI_FORMS_API bool same_prepared_text_key(const PreparedTextKey& left, const PreparedTextKey& right) noexcept;
[[nodiscard]] GUI_FORMS_API bool same_layout_authority(const LayoutAuthority left, const LayoutAuthority right) noexcept;

} // namespace gui_forms
