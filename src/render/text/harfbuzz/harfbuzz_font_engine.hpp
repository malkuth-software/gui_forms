#pragma once

#include "../../../core/resources/fonts/codepoint_coverage.hpp"
#include "../../../core/text/shaping/shaped_text_geometry.hpp"
#include "gui_forms/typography.hpp"
#include "gui_forms/types.hpp"

#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
#include "text_layout_diagnostics.hpp"
#endif

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace gui_forms::render::text {

class BoundedShapeWorkspace;

struct ShapedFontRun final {
    FontFaceId face{};
    Utf8Range source_range{};
#if defined(GUI_FORMS_TEXT_LAYOUT_GEOMETRY_TRACE)
    bool diagnostic_rtl{};
#endif
    std::vector<ShapedGlyph> glyphs{};
};

struct ShapedText final {
    std::vector<ShapedFontRun> runs{};
    double width{};
    double height{};
    double ascent{};
    double descent{};
    std::size_t missing_clusters{};
    bool missing_primary_face{};
};

// The encoded font a registered face was opened from. A rasterizer opens its
// own FreeType face from these bytes, at its own size, while owner lives.
struct FontFaceSource final {
    std::span<const std::byte> bytes{};
    std::shared_ptr<const void> owner{};
    std::uint32_t face_index{};
};

// The encoded font a lazily registered face opens from; `owner` keeps `bytes` alive. Empty
// bytes mean the face could not be loaded.
struct LazyFaceBytes final {
    std::span<const std::byte> bytes{};
    std::shared_ptr<const void> owner{};
};
using LazyFaceLoader = std::function<LazyFaceBytes()>;

struct ShapeStorageLimits final {
    std::size_t input_bytes{16'384};
    std::size_t runs{16'384};
    std::size_t glyphs{65'536};
    std::size_t output_bytes{8U * 1024U * 1024U};
    std::size_t workspace_bytes{16U * 1024U * 1024U};
};

struct PrimaryLineMetrics final {
    FontFaceId face{};
    double ascent_device{};
    double descent_device{};
    double line_gap_device{};
};

// Private portable text engine. It owns every encoded face and never consults
// a host font catalog. The renderer maps returned face IDs to its own private
// raster objects; HarfBuzz/FreeType objects never cross this boundary.
class HarfBuzzFontEngine final {
public:
    HarfBuzzFontEngine();
    ~HarfBuzzFontEngine();
    HarfBuzzFontEngine(const HarfBuzzFontEngine&) = delete;
    HarfBuzzFontEngine& operator=(const HarfBuzzFontEngine&) = delete;
    // Before registering faces, preallocate a finite record table and omit
    // copied family labels (prepared glyph consumers do not use them). Returns
    // requested first-party persistent bytes, excluding shared encoded owners
    // and opaque FT/HB allocations. No subsequent table growth is admitted.
    [[nodiscard]] std::size_t configure_bounded_registration(std::size_t face_limit, std::size_t byte_limit);

    [[nodiscard]] std::optional<FontFaceId> register_typeface(
        FontRole role, std::uint16_t weight, bool italic,
        std::span<const std::byte> encoded, std::uint32_t face_index = 0U);
    // A fallback face participates after all faces registered for the
    // requested role, without duplicating its encoded bytes per role.
    [[nodiscard]] std::optional<FontFaceId> register_fallback_typeface(
        std::uint16_t weight, bool italic,
        std::span<const std::byte> encoded, std::uint32_t face_index = 0U);
    // Shared immutable bytes let the rasterizer and shaper retain one font
    // allocation. Span overloads continue to take an independent owned copy.
    [[nodiscard]] std::optional<FontFaceId> register_shared_typeface(
        std::optional<FontRole> role, std::uint16_t weight, bool italic,
        std::shared_ptr<const std::vector<std::byte>> encoded,
        std::uint32_t face_index = 0U);
    // The owner keeps the immutable span alive, including file mappings. This
    // avoids a private heap copy of every bundled font in each native window.
    [[nodiscard]] std::optional<FontFaceId> register_owned_typeface(
        std::optional<FontRole> role, std::uint16_t weight, bool italic,
        std::span<const std::byte> encoded, std::shared_ptr<const void> owner,
        std::uint32_t face_index = 0U);
    // A fallback face that is loaded and opened only when a cluster first needs it: when no
    // face before it covers the cluster and `coverage` does. `coverage` must be exactly the
    // scalars the face's Unicode charmap maps to a glyph, as the bundled coverage tables are,
    // so selection is the same as with the face opened at registration; it must outlive the
    // engine. `load` runs on the shaping thread, at most once unless it throws; a face whose
    // bytes are empty or do not open covers nothing from then on.
    [[nodiscard]] std::optional<FontFaceId> register_lazy_fallback_typeface(
        std::uint16_t weight, bool italic, std::span<const gui_forms::detail::CodepointRange> coverage,
        LazyFaceLoader load);
    // A face of the numbered bundled family `family` (FontSpec::family, never zero), loaded and
    // opened the first time text names its family. It is preferred over every role face for
    // such text and takes no part in any other; fallback through the role's faces and the
    // fallback faces still covers what it lacks. `load` runs on the shaping thread, at most
    // once unless it throws; a face that does not open is left out from then on. Refused by
    // a bounded engine (configure_bounded_registration).
    [[nodiscard]] std::optional<FontFaceId> register_lazy_family_typeface(
        BundledFontFamily family, std::uint16_t weight, bool italic, LazyFaceLoader load);
    // Faces opened so far: eager faces and lazy faces that have been needed.
    [[nodiscard]] std::size_t opened_face_count() const noexcept;
    // Native setup/allocation failures throw; a call-owned partial result never
    // becomes a successful return. Callers retain old state until return succeeds.
    [[nodiscard]] ShapedText shape(std::string_view utf8, FontSpec font);
    // Exact role/weight/italic only, first registered matching primary. No
    // fallback or text shaping. Sets that owned FT face's size once using the
    // same 26.6 rounding as shape; caller confines native mutation to executor.
    // Nullopt means no exact primary. Invalid font/native failures throw.
    [[nodiscard]] std::optional<PrimaryLineMetrics> primary_line_metrics(const FontSpec font);
    // Throws length_error before a controlled allocation would exceed limits;
    // invalid input and native/resource failures also throw. The caller's old
    // owner survives until it explicitly adopts the complete returned owner.
    [[nodiscard]] std::unique_ptr<BoundedShapedText> shape_bounded(
        std::string_view utf8, FontSpec font, ShapeStorageLimits limits);
    // Private experiment: requires bounded font registration. Prepare explicitly
    // before repeated calls; includes engine records and simultaneous replacement.
    void prepare_workspace(BoundedShapeWorkspace& workspace, const ShapeStorageLimits& limits);
    // No scratch growth. Input is borrowed only until return and must not alias
    // workspace arrays. Returned arrays own exact active counts independently.
    // output_bytes is remaining aggregate allowance, already excluding retained
    // old outputs/input/row metadata. Failures preserve caller-owned old output.
    [[nodiscard]] std::unique_ptr<BoundedShapedText> shape_with_workspace(
        std::string_view utf8, FontSpec font, const ShapeStorageLimits& limits,
        BoundedShapeWorkspace& workspace);
    [[nodiscard]] ResolvedTextLayout resolve(std::string_view utf8,
                                             FontSpec font);
    // UAX #14 line-break opportunities (libunibreak 8.0, Unicode 17, the
    // locale-independent "-strict" tailoring), one per input byte, under
    // TextMetricsProvider::fill_line_break_opportunities_utf8's contract.
    // Returns exact, or invalid_request with `opportunities` unchanged. It
    // reads no face and allocates nothing.
    [[nodiscard]] static TextResolutionStatus fill_line_break_opportunities_utf8(
        std::string_view utf8, std::span<LineBreakOpportunity> opportunities) noexcept;
    // TextMetricsProvider::fill_cluster_advances_utf8's contract, from one
    // shape(): each glyph's advance (letter spacing included, as shape
    // places it) added at its cluster's first byte. Returns exact, the
    // resolution's missing-face status, or invalid_request with `advances`
    // unchanged. Shaping allocates its glyph records, as shape() does.
    [[nodiscard]] TextResolutionStatus fill_cluster_advances_utf8(
        std::string_view utf8, FontSpec font, std::span<double> advances);
    [[nodiscard]] std::size_t face_count() const noexcept;
    // Nullopt for an unknown ID. Registered faces are never removed, so the
    // span stays valid while the returned owner is held.
    [[nodiscard]] std::optional<FontFaceSource> face_source(FontFaceId id) const;
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
    [[nodiscard]] TextLayoutDiagnostics diagnostics() const noexcept;
    // Executor-confined, consumed at the named point/run. An unreached point
    // remains armed until replaced; none explicitly disarms it.
    void set_diagnostic_failure(TextLayoutFailure failure, std::uint64_t run);
#endif

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gui_forms::render::text
