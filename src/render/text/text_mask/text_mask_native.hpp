#pragma once

#include "../../../core/resources/fonts/program_typefaces.hpp"
#include "../../../core/text/prepared/prepared_face_pool.hpp"
#include "../../../core/text/text_mask/text_mask_state.hpp"
#include "../../../core/text/unicode/unicode_grapheme.hpp"

#include <stxlib/glyph_face.hpp>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <SheenBidi/SheenBidi.h>

namespace gui_forms::detail::mask_native {

struct NativeFailure final : std::exception {
    explicit NativeFailure(const TextMaskStatus status) noexcept : status(status) {}
    TextMaskStatus status{TextMaskStatus::native_failure};
};

struct NativeLibrary final {
    FT_Library value{};
    NativeLibrary();
    ~NativeLibrary();
    NativeLibrary(const NativeLibrary&) = delete;
    NativeLibrary& operator=(const NativeLibrary&) = delete;
};
struct NativeFace final {
    FT_Face value{};
    hb_font_t* shaping{};
    double ascent{0.0};
    double descent{0.0};
    NativeFace() = default;
    ~NativeFace();
    NativeFace(const NativeFace&) = delete;
    NativeFace& operator=(const NativeFace&) = delete;
};

// One job's faces. Shaping, coverage and line metrics use FreeType and
// HarfBuzz faces of the key's bank (until STXlib's shaper replaces them);
// the raster draws with STXlib faces over the bank's typefaces
// (PreparedFontBank::typefaces), one per bank face, on the job's thread,
// leased from the bank's face pool (prepared_face_pool.hpp): opened by the
// first job that draws them and reused by the jobs after it.
struct NativeFonts final {
    // Destruction reverses declaration: HB/FT faces before their library;
    // the raster lease first of all (its faces borrow the bank, which the
    // key keeps alive for the job, and it returns them to the bank's pool).
    NativeLibrary library{};
    std::array<NativeFace, 8> faces{};
    std::size_t count{0};
    std::uint32_t primary{0};
    PreparedFaceLease raster;
    // The leased set's faces, in bank order.
    std::array<ExecutorFace, PreparedTextLimits::font_faces>& raster_faces;
    explicit NativeFonts(const MaskKey& key);
    [[nodiscard]] std::uint32_t select_face(const std::string_view cluster) const;
};

struct NativeGlyph final {
    std::uint32_t face{0};
    std::uint32_t glyph{0};
    std::uint32_t cluster{0};
    double x{0.0};
    double y{0.0};
    double advance{0.0};
};
using NativeGlyphBuffer = std::vector<NativeGlyph, MaskAllocator<NativeGlyph>>;
// Growth follows actual native glyph counts, capped at 65,536. Capacity is
// reused across line trials; allocation/initialization precedes glyph kernels.
void resize_native_glyphs(NativeGlyphBuffer& glyphs, const std::size_t count);
struct NativeCluster final {
    std::size_t begin{0};
    std::size_t end{0};
    std::uint32_t face{0};
    hb_script_t script{HB_SCRIPT_UNKNOWN};
};
struct NativeShape final {
    std::size_t glyph_count{0};
    double advance{0.0};
    double ascent{0.0};
    double descent{0.0};
};
struct NativeWork final {
    std::size_t shape_calls{0};
    std::size_t context_bytes{0};
    void admit(const std::size_t bytes);
};

// One paragraph's resolved bidi levels survive all candidate line trials.
// Source borrows the admitted key; fonts, ledger and work outlive this object.
// First-party arrays use counted allocators; native library allocations remain
// opaque. Caller owns the counted output, reused across trials.
class NativeParagraph final {
public:
    NativeParagraph(const std::shared_ptr<MaskLedger>& ledger, NativeFonts& fonts,
        NativeWork& work, const std::string_view text);
    ~NativeParagraph();
    NativeParagraph(const NativeParagraph&) = delete;
    NativeParagraph& operator=(const NativeParagraph&) = delete;
    [[nodiscard]] NativeShape shape(const std::size_t begin, const std::size_t end,
        NativeGlyphBuffer& output);
    [[nodiscard]] std::span<const std::size_t> edges() const noexcept;
private:
    NativeFonts& fonts_;
    NativeWork& work_;
    std::string_view text_{};
    std::vector<std::size_t, MaskAllocator<std::size_t>> edges_;
    std::vector<NativeCluster, MaskAllocator<NativeCluster>> clusters_;
    SBAlgorithmRef algorithm_{};
    SBParagraphRef paragraph_{};
    hb_buffer_t* buffer_{};
    void release() noexcept;
    void append_run(const std::size_t line_begin, const std::size_t line_end,
        const std::size_t run_begin, const std::size_t run_end,
        const NativeCluster& cluster, const bool rtl,
        NativeGlyphBuffer& output, NativeShape& shape);
};

struct NativeInk final {
    std::int64_t left{0};
    std::int64_t top{0};
    std::int64_t right{0};
    std::int64_t bottom{0};
    bool present{false};
};

// The raster's working storage for one job, reused by its measure and its
// fill: one face run's glyphs as STXlib takes them (counted as workspace)
// and STXlib's mask of that run (STXlib's storage, reused from run to run).
struct NativeRasterWork final {
    std::vector<stxlib::glyphs::RunGlyph, MaskAllocator<stxlib::glyphs::RunGlyph>> glyphs;
    stxlib::glyphs::RunCoverage coverage{};
    explicit NativeRasterWork(const std::shared_ptr<MaskLedger>& ledger)
        : glyphs(MaskAllocator<stxlib::glyphs::RunGlyph>(ledger, MaskResource::workspace)) {}
};

// Glyphs are drawn a face run at a time (consecutive glyphs of one face), in
// order. outline_gray: Fit::light, x at its 1/64 pixel phase, y on whole
// pixels, runs combined as c + p * (255 - c) / 255. true_mono:
// Fit::monochrome, both axes on whole pixels, runs combined by the larger.
// A glyph outside its face throws missing_font_coverage; ink wider or
// taller than 4096 pixels throws MaskLimitFailure(mask_dimension); a run
// STXlib refuses throws native_failure.
[[nodiscard]] NativeInk measure_native_ink(NativeFonts& fonts, const MaskOptions& options,
    const std::span<const NativeGlyph> glyphs, NativeRasterWork& work);
void fill_native_mask(NativeFonts& fonts, const MaskOptions& options,
    const std::span<const NativeGlyph> glyphs, NativeRasterWork& work, TextMaskStorage& storage);

} // namespace gui_forms::detail::mask_native
