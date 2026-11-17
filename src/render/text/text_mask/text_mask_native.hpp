#pragma once

#include "../../../core/resources/fonts/program_typefaces.hpp"
#include "../../../core/text/prepared/prepared_face_pool.hpp"
#include "../../../core/text/text_mask/text_mask_state.hpp"
#include "../../../core/text/unicode/unicode_grapheme.hpp"

#include <stxlib/font_file.hpp>
#include <stxlib/glyph_face.hpp>
#include <stxlib/glyph_shape.hpp>
#include <stxlib/textlayout.hpp>

namespace gui_forms::detail::mask_native {

struct NativeFailure final : std::exception {
    explicit NativeFailure(const TextMaskStatus status) noexcept : status(status) {}
    TextMaskStatus status{TextMaskStatus::native_failure};
};

// One bank face as a job shapes with it: the bank typeface's font (its
// character map and numbers; the bank keeps it, and the key the bank, for
// the job), a shaper over the typeface opened for the job, and the face's
// design ascent and descent at the job's size.
struct NativeFace final {
    const stxlib::glyphs::Font* font{};
    stxlib::glyphs::Shaper shaper{};
    double ascent{0.0};
    double descent{0.0};
};

// One job's faces. Shaping, coverage and line metrics use STXlib shapers
// over the key's bank typefaces (PreparedFontBank::typefaces); the raster
// draws with STXlib faces over the same typefaces, one per bank face, on the
// job's thread, leased from the bank's face pool (prepared_face_pool.hpp):
// opened by the first job that draws them and reused by the jobs after it.
struct NativeFonts final {
    // Destruction reverses declaration: the shapers before the raster lease
    // (its faces borrow the bank, which the key keeps alive for the job, and
    // it returns them to the bank's pool).
    PreparedFaceLease raster;
    // The leased set's faces, in bank order.
    std::array<ExecutorFace, PreparedTextLimits::font_faces>& raster_faces;
    std::array<NativeFace, 8> faces{};
    std::size_t count{0};
    std::uint32_t primary{0};
    // The job's size, 26.6 pixels to the em: the shapers' size.
    std::int64_t size_64{0};
    // The characters and glyphs of a coverage shaping (covers), reused.
    stxlib::Vector<std::uint32_t> coverage_characters{};
    stxlib::Vector<stxlib::glyphs::ShapedGlyph> coverage_glyphs{};
    explicit NativeFonts(const MaskKey& key);
    // The face that draws `cluster`: the primary if it covers it, else the
    // first bank face that does. A face covers a cluster when it maps every
    // scalar but a joiner or variation selector, or else when shaping the
    // cluster in it gives no missing glyph (STXlib composes canonical pairs
    // and hides default-ignorable characters such as format controls).
    // Throws missing_font_coverage when none does.
    [[nodiscard]] std::uint32_t select_face(const std::string_view cluster);
private:
    [[nodiscard]] bool covers(std::size_t face, std::string_view cluster);
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
    // The ISO 15924 tag of the cluster's script (stxlib::textlayout's), or 0 for the
    // common, inherited and unknown scripts: the shaper then takes the run's
    // first character with a script of its own.
    std::uint32_t script{0};
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
// First-party arrays use counted allocators (the run's characters and
// clusters on the workspace ledger) and the bidi analysis is charged in
// bytes; STXlib's glyph output and its shapers' storage are its own. Caller
// owns the counted output, reused across trials.
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
    // One run's characters and, for each, its byte offset in the line.
    std::vector<std::uint32_t, MaskAllocator<std::uint32_t>> run_characters_;
    std::vector<std::uint32_t, MaskAllocator<std::uint32_t>> run_clusters_;
    stxlib::Vector<stxlib::glyphs::ShapedGlyph> run_glyphs_{};
    // The paragraph's bidi analysis (stxlib::textlayout: levels and script
    // runs) and the line analysis each shape() reuses. They allocate on
    // STXlib's allocator; analysis_charge_ charges their bytes_held to the
    // ledger as workspace after each analysis (charge_analysis).
    stxlib::textlayout::Paragraph paragraph_{};
    stxlib::textlayout::Line line_{};
    std::shared_ptr<MaskLedger> ledger_;
    MaskCharge analysis_charge_{};
    std::size_t charged_bytes_{0};
    void release() noexcept;
    // Charges what the analyses hold now in place of the last charge. It
    // follows a growth (STXlib cannot be asked to stay within a budget), so
    // the ledger may be passed by one growth before the budget failure
    // ends the job.
    void charge_analysis();
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
