#pragma once

// Stage F's metrics differential (notes:planning/stages/F-font-substitution). It
// measures a reference face and a candidate substitute with the bundled
// text engine (STXlib's font reader and shaper) and compares the two under the tolerance of the
// mapping's class (owner decisions 8 and 9 of 2026-10-23). GDI's measurements
// of the same faces are optional information for stages R and S, never part
// of the gate. Reference fonts are oracles: the tool reads them by path and
// records their version and SHA-256, never their bytes.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::tools::font_metrics {

// Thrown by whole-buffer validation before any measurement starts. Nothing is
// measured or written when an input is rejected.
class InputRejected final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// What a substitution mapping claims about its face.
enum class MappingClass : std::uint8_t {
    metric_compatible,
    nearest,
};

// The fixed measurement plan. Sizes are em sizes in device pixels: 11, 12, 13,
// 16 px are 8.25, 9, 10 and 12 pt at 96 dpi; 22 and 24 px are 11 and 12 px at
// scale 2. Wrap widths are in ems of the measured size, so a paragraph's line
// breaks are comparable across sizes.
inline constexpr std::array<double, 6> measured_sizes_px{11.0, 12.0, 13.0, 16.0, 22.0, 24.0};
inline constexpr std::array<double, 3> wrap_widths_em{20.0, 35.0, 60.0};

struct CodepointRange final {
    char32_t first{};
    char32_t last{};
};

// The code points whose advances are compared one by one: printable ASCII,
// Latin-1 Supplement and Latin Extended-A.
inline constexpr std::array<CodepointRange, 2> advance_coverage{{
    {U' ', U'~'},
    {U' ', U'ſ'},
}};

inline constexpr std::size_t maximum_face_bytes = 64U * 1024U * 1024U;
inline constexpr std::size_t maximum_corpus_bytes = 1024U * 1024U;
inline constexpr std::size_t maximum_corpus_entries = 4096U;
inline constexpr std::size_t maximum_paragraph_words = 4096U;

// A corpus file: "[strings]" lines are measured as one line each; each
// "[paragraphs]" line is one paragraph, wrapped at every wrap width. Lines
// starting with '#' and empty lines are ignored.
struct Corpus final {
    std::vector<std::string> strings{};
    std::vector<std::string> paragraphs{};
};

// Facts read from the face file's own tables, in font units.
struct FaceFacts final {
    std::string file_name{};
    std::uint64_t byte_count{};
    std::string sha256_hex{};
    std::string family{};
    std::string version{};
    std::uint16_t units_per_em{};
    std::uint16_t weight_class{};
    bool italic{};
    std::int16_t hhea_ascender{};
    std::int16_t hhea_descender{};
    std::int16_t hhea_line_gap{};
    std::int16_t typo_ascender{};
    std::int16_t typo_descender{};
    std::int16_t typo_line_gap{};
    std::uint16_t win_ascent{};
    std::uint16_t win_descent{};
    std::uint32_t glyph_count{};
};

// One code point of advance_coverage, in the order of the ranges.
struct CodepointAdvance final {
    char32_t codepoint{};
    bool mapped{};
    std::int32_t advance_units{};
};

// The bundled engine's measurements at one size.
struct EngineSizeMeasurement final {
    double size_px{};
    double ascent_px{};
    double descent_px{};
    double line_gap_px{};
    // One width per corpus string, in corpus order.
    std::vector<double> string_widths_px{};
    // Clusters no registered face covers, summed over the corpus strings.
    std::size_t missing_clusters{};
    // Shape: paragraphs x wrap_widths_em, flattened as
    // paragraph * wrap_widths_em.size() + width. Each entry lists the word
    // indices that start the second and later lines.
    std::vector<std::vector<std::uint32_t>> line_starts{};
};

// GDI's measurements at one size (Windows only), in integer device pixels as
// GDI reports them.
struct GdiSizeMeasurement final {
    double size_px{};
    std::int32_t ascent{};
    std::int32_t descent{};
    std::int32_t height{};
    std::int32_t internal_leading{};
    std::int32_t external_leading{};
    std::vector<std::int32_t> string_widths{};
};

struct GdiMeasurement final {
    // False off Windows, when not requested, or when GDI could not select the face.
    bool available{};
    std::string unavailable_reason{};
    // GetTextFaceW's answer for the requested family.
    std::string selected_face{};
    // GetFontData's bytes hash to the file's SHA-256: GDI measured this file.
    bool bytes_match_file{};
    std::vector<GdiSizeMeasurement> sizes{};
};

struct FaceMeasurement final {
    FaceFacts facts{};
    std::vector<CodepointAdvance> advances{};
    std::vector<EngineSizeMeasurement> sizes{};
    GdiMeasurement gdi{};
};

enum class Criterion : std::uint8_t {
    passed,
    failed,
    // Reserved for a gated criterion whose measurement did not run.
    not_measured,
    // Measured and reported, not gated for this mapping class.
    recorded,
};

struct AdvanceMismatch final {
    char32_t codepoint{};
    std::int32_t reference_units{};
    std::int32_t candidate_units{};
};

struct SizeDifferential final {
    double size_px{};
    // Signed mean and largest magnitude of (candidate - reference) / reference
    // over the corpus strings whose reference width is positive.
    double mean_width_deviation{};
    double max_abs_width_deviation{};
    std::size_t line_break_sets{};
    std::size_t line_break_sets_identical{};
    // The candidate's engine line height (ascent + descent + line gap).
    double candidate_line_height_px{};
    double reference_line_height_px{};
    // GDI's tmHeight for the reference face, when GDI ran.
    std::optional<std::int32_t> gdi_reference_height{};
    std::optional<std::int32_t> gdi_candidate_height{};
    // Mean (candidate - reference) / reference over GDI's extents, evidence only.
    std::optional<double> gdi_mean_width_deviation{};
    // Mean (engine - GDI) / GDI over one face's own strings: how far the
    // engine's unhinted advances sit from GDI's hinted ones (ADR-004). Evidence
    // for the stages that compare against Windows Forms, not a face property.
    std::optional<double> reference_engine_versus_gdi{};
    std::optional<double> candidate_engine_versus_gdi{};
};

struct Differential final {
    MappingClass mapping{};
    std::size_t advances_compared{};
    std::size_t advances_identical{};
    std::size_t missing_in_candidate{};
    std::size_t missing_in_reference{};
    double max_abs_advance_delta_em{};
    std::vector<AdvanceMismatch> advance_mismatches{};
    std::vector<SizeDifferential> sizes{};
    Criterion advances{};
    Criterion line_breaks{};
    Criterion line_height{};
    Criterion line_width{};
    bool gate_passed{};
};

// Tolerances of owner decision 8. A nearest mapping's mean corpus line width
// stays within this fraction of the reference at every size; a
// metric-compatible mapping's line height stays within this many pixels of
// the reference's, both measured by the bundled engine.
inline constexpr double nearest_mean_width_tolerance = 0.05;
inline constexpr double compatible_line_height_tolerance_px = 1.0;

// Reads a whole file. Rejects an absent file, an empty file or one larger than
// limit bytes.
[[nodiscard]] std::vector<std::byte> read_bounded_file(const std::filesystem::path& path,
                                                       std::size_t limit);

// Validates the whole corpus buffer (size, UTF-8, sections, entry counts)
// before building any entry.
[[nodiscard]] Corpus parse_corpus(std::string_view bytes);

// Validates the sfnt table directory and the tables the measurement reads,
// before the font reader opens the face. Throws InputRejected.
void validate_face_bytes(std::span<const std::byte> encoded);

// Lowercase hexadecimal SHA-256 of bytes.
[[nodiscard]] std::string sha256_hex(std::span<const std::byte> bytes);

// Measures one face with the bundled engine; with_gdi also runs the GDI measurement
// (Windows only; elsewhere it records why it did not run). encoded is borrowed
// until return.
[[nodiscard]] FaceMeasurement measure_face(std::string_view file_name,
                                           std::span<const std::byte> encoded,
                                           const Corpus& corpus, bool with_gdi);

// Implemented per platform: gdi_measurement_windows.cpp, gdi_measurement_unavailable.cpp.
[[nodiscard]] GdiMeasurement measure_with_gdi(std::span<const std::byte> encoded,
                                              const FaceFacts& facts, const Corpus& corpus);

// Validates that both measurements share one plan and corpus shape, then
// compares them under the mapping's tolerance. Throws InputRejected on a shape
// mismatch.
[[nodiscard]] Differential compare_faces(const FaceMeasurement& reference,
                                         const FaceMeasurement& candidate,
                                         MappingClass mapping);

// Writes one JSON report: both faces' facts and measurements, the
// differential, and the criteria.
void write_report(std::ostream& output, std::string_view corpus_name,
                  const FaceMeasurement& reference, const FaceMeasurement& candidate,
                  const Differential& differential);

// A reference face's engine measurement kept as text (brief decision 18):
// measured once from a Windows reference file, identified by its SHA-256,
// with no font data, so the gate compares bundled candidates with it on hosts
// that lack the reference face. The candidate digest is the measurement the
// bundled candidate produced when the snapshot was made; every host must
// reproduce it.
struct MetricSnapshot final {
    // The substitution table request this reference stands for: a style name
    // (regular, bold, italic or bold_italic) and the requested family.
    std::string request_style{};
    std::string request_family{};
    std::string corpus_name{};
    // corpus_digest of the corpus the snapshot was measured with.
    std::string corpus_digest{};
    std::string candidate_file{};
    std::string candidate_digest{};
    FaceMeasurement reference{};
};

// The engine measurement as snapshot records (facts, advances, sizes); GDI
// measurements are not part of it.
[[nodiscard]] std::string serialize_measurement(const FaceMeasurement& measurement);

// SHA-256 of the parsed corpus: its sections and entries joined with LF. It
// names the corpus content, so a checkout's line endings do not change it.
[[nodiscard]] std::string corpus_digest(const Corpus& corpus);

// SHA-256 of serialize_measurement's text: equal digests mean identical
// measurements.
[[nodiscard]] std::string measurement_digest(const FaceMeasurement& measurement);

void write_snapshot(std::ostream& output, const MetricSnapshot& snapshot);

// Validates the whole snapshot text before returning; throws InputRejected.
[[nodiscard]] MetricSnapshot parse_snapshot(std::string_view text);

} // namespace gui_forms::tools::font_metrics
