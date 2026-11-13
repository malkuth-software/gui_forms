#pragma once

#include "gui_forms/allocation/counted_allocator/counted_allocator.hpp"
#include "gui_forms/export/application_export.hpp"
#include "gui_forms/layout/style/layout_style.hpp"
#include "gui_forms/text/types/text_types.hpp"
#include "gui_forms/text_shaping/types/text_shaping_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/typography/shaping_workspace/shaping_workspace.hpp"
#include "gui_forms/typography/types/typography_types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace gui_forms {

class Painter;

// Paragraph layout into caller storage: one paragraph of UTF-8 text wrapped
// at a width, for owner-drawn rows at chat scale (millions of rows, a few
// hundred laid out per frame). It is synchronous, keeps nothing between
// calls, and allocates nothing of its own: every array it fills is the
// caller's (ParagraphStorage), and a call whose storage is too small says
// how much it needs (ParagraphLayoutResult::storage_too_small) instead of
// growing anything. The text metrics provider's own work is the provider's:
// given a shaping workspace (ParagraphStorage::workspace, which
// ParagraphBuffers creates from the provider and owns) every shaping goes
// through it, and the bundled engine's painters then allocate nothing either
// once it is reserved (TextMetricsProvider::shape_in_workspace_utf8; what is
// left is a face's first use). Without one, the provider's plain calls may
// allocate (the bundled engine's do, for every shaping).
//
// With ParagraphGlyphOutput::runs it also fills each line's glyph runs
// (GlyphRunRecord, PositionedGlyph) into the caller's storage, from the
// workspace's shaping of the line: the glyphs Painter::draw_text_utf8 would
// shape and draw for the line, which paint_paragraph then draws with
// Painter::draw_glyph_runs without shaping again.
//
// It wraps as TextBlock wraps, with the same provider calls, so a laid-out
// line is the line TextBlock paints:
// - break opportunities are the provider's (fill_line_break_opportunities_
//   utf8: UAX #14 with the bundled engine);
// - each line is the run from its start to the first break opportunity
//   whose run does not fit (greedy, as CSS fills lines), its trailing
//   spaces and line end hanging, unmeasured and unpainted;
// - a run's width is the provider's measurement of the run shaped alone
//   (measure_text_extent_utf8), which is what Painter::draw_text_utf8
//   shapes and draws.
// Where TextBlock shapes every candidate run of a line (w shapings for a
// line of w words), this shapes the paragraph once for its cluster advances
// (TextMetricsProvider::fill_cluster_advances_utf8), finds each line's end
// from their sums and, under ParagraphFit::verified, confirms it with the
// whole-run measurement of the chosen run and of the next one: about two
// shapings per line, and the same lines as TextBlock wherever a run's
// measured width does not shrink as words are added to it.
//
// Direction: lines break in logical order. Each line is drawn as one run,
// and the bundled engine reorders it for display as its own UAX #9
// paragraph (SheenBidi, its base level from its first strong character),
// as TextBlock's lines are. Not supported: one base direction carried
// across the paragraph's lines, embedding controls spanning lines, and
// vertical text. ParagraphStyle::direction only resolves the start and end
// alignments.

// How white space is treated.
enum class ParagraphWhiteSpace : std::uint8_t {
    // The text is laid out as given (CSS `white-space: pre-wrap`, trailing
    // spaces hanging): a line feed, a carriage return or the pair ends a
    // line, and line ranges index the caller's text. A text ending in a
    // line end has no empty last line.
    preserve_line_ends,
    // TextBlock's `white-space: normal`: each run of spaces, tabs, line
    // feeds, carriage returns and form feeds becomes one space and the
    // text's leading and trailing white space goes. The collapsed text is
    // written to ParagraphStorage::collapsed_text and line ranges index it.
    collapse,
};

// What a word wider than the line does.
enum class ParagraphOverflowWrap : std::uint8_t {
    // It overflows on a line of its own (CSS `overflow-wrap: normal`, as
    // TextBlock).
    normal,
    // It breaks between shaping clusters where nothing else lets the line
    // fit (CSS `overflow-wrap: break-word`). A cluster is never split, so an
    // emoji sequence, a base with its marks or a ligature stays whole; a
    // single cluster wider than the line still overflows.
    break_word,
};

// Whether a layout also fills each line's glyph runs.
enum class ParagraphGlyphOutput : std::uint8_t {
    // Lines only.
    none,
    // Each line's glyph runs too (ParagraphStorage::glyph_runs and glyphs,
    // ParagraphLine::first_glyph_run): the line's shaping alone, through the
    // provider's workspace. Under ParagraphFit::verified a line's glyphs are
    // copied from the measurement that chose it, at no extra shaping; under
    // estimated each line is shaped once more. Without a workspace the
    // provider accepts, lines are laid out without glyphs
    // (ParagraphMetrics::glyph_source is then the zero source).
    runs,
};

// How exactly each line's end is found.
enum class ParagraphFit : std::uint8_t {
    // Sums of cluster advances propose the end; whole-run measurements of
    // the proposed run and of the next confirm it, so the lines and their
    // widths are the ones TextBlock lays out and Painter draws.
    verified,
    // Sums of cluster advances alone: one shaping per paragraph. A line's
    // width can differ from its painted width by the kerning and contextual
    // shaping across its ends, so a line may differ from TextBlock's by a
    // word. For estimating heights of rows not yet shown.
    estimated,
};

// What one call lays out. `font` is the effective font, already scaled
// (Control::text_font, or the font and text scale a window applies), and
// `line_box` is paragraph_line_box's answer for it: computing it once per
// font keeps that resolution out of every row.
struct GUI_FORMS_API ParagraphStyle final {
    FontSpec font{};
    LineBoxMetrics line_box{};
    // The line width in logical pixels: finite and not negative.
    double width{};
    ParagraphWhiteSpace white_space{ParagraphWhiteSpace::preserve_line_ends};
    ParagraphOverflowWrap overflow_wrap{ParagraphOverflowWrap::normal};
    ParagraphFit fit{ParagraphFit::verified};
    ParagraphGlyphOutput glyph_output{ParagraphGlyphOutput::none};
    // Where each line sits in `width`, as TextBlock's `text_align`; start
    // and end follow `direction`.
    TextAlignment alignment{TextAlignment::start};
    TextDirection direction{TextDirection::left_to_right};
};

// One laid-out line, positioned in the paragraph's box (its top left is the
// origin, y down).
struct GUI_FORMS_API ParagraphLine final {
    // The line's text: the bytes and the characters (Unicode scalar values)
    // it shows, its hanging trailing spaces and line end excluded. Offsets
    // index the laid-out text: the caller's text, or the collapsed text
    // under ParagraphWhiteSpace::collapse.
    Utf8Range bytes{};
    ScalarRange characters{};
    // Where the next line's text begins: after this line's hanging spaces
    // and line end. Equal to the text's size on the last line.
    Utf8Offset next_line_start{};
    // The line's width in logical pixels (see ParagraphFit), its left edge
    // from the box's left edge (alignment applied; negative when a line
    // overflows a right, end or centre alignment), and its baseline from
    // the box's top.
    double width{};
    double x{};
    double baseline{};
    // The line's glyph runs under ParagraphGlyphOutput::runs: glyph_run_count
    // records from first_glyph_run of ParagraphStorage::glyph_runs, in visual
    // order, their ranges and clusters in the laid-out text's offsets and
    // their positions from the line's origin. Zero otherwise.
    std::size_t first_glyph_run{};
    std::size_t glyph_run_count{};
    friend bool operator==(const ParagraphLine& left, const ParagraphLine& right) noexcept {
        return left.bytes == right.bytes && left.characters == right.characters &&
               left.next_line_start == right.next_line_start && left.width == right.width &&
               left.x == right.x && left.baseline == right.baseline &&
               left.first_glyph_run == right.first_glyph_run && left.glyph_run_count == right.glyph_run_count;
    }
};

// The caller's storage for one call, borrowed for the call only; nothing is
// retained. The arrays must not overlap each other or the text.
// - lines (output): one record per line, in order; only
//   lay_out_paragraph writes it.
// - collapsed_text (output, collapse only): the collapsed text, which the
//   line ranges index and which paint_paragraph must be given.
// - opportunities and advances (scratch): one element per laid-out byte;
//   their contents after a call are unspecified.
// - glyph_runs and glyphs (output, ParagraphGlyphOutput::runs only): every
//   line's runs, then every line's glyphs, line after line; only
//   lay_out_paragraph writes them.
// - workspace (borrowed, optional): the provider's shaping workspace
//   (TextMetricsProvider::create_shaping_workspace), holding at least the
//   laid-out size; every shaping of the call goes through it. Null uses the
//   provider's plain calls, and no glyph runs are filled.
// The call uses the leading elements it needs and leaves the rest as they
// were.
struct GUI_FORMS_API ParagraphStorage final {
    std::span<ParagraphLine> lines{};
    std::span<char> collapsed_text{};
    std::span<LineBreakOpportunity> opportunities{};
    std::span<double> advances{};
    std::span<GlyphRunRecord> glyph_runs{};
    std::span<PositionedGlyph> glyphs{};
    ShapingWorkspace* workspace{};
};

// What a call needs. per_byte is the size of the laid-out text (the
// collapsed size under collapse): the element count opportunities and
// advances need, the bytes a workspace must hold, and collapsed_text too
// under collapse (zero otherwise). lines is exact when the per-byte arrays
// were large enough to lay the text out, and otherwise per_byte, the most
// lines a text of that size can have (every line holds at least one byte),
// so storage of these sizes always succeeds on the next call.
// Under ParagraphGlyphOutput::runs, glyph_runs and glyphs are the runs and
// glyphs of every line: exact after a call that laid the text out, and
// otherwise per_byte each, which bounds the runs (each holds a byte of its
// line) and holds the glyphs of any text shaped into at most one glyph a
// byte (a call that needs more says so). Zero for ParagraphGlyphOutput::none.
struct GUI_FORMS_API ParagraphStorageRequirement final {
    std::size_t per_byte{};
    std::size_t collapsed_bytes{};
    std::size_t lines{};
    std::size_t glyph_runs{};
    std::size_t glyphs{};
    friend bool operator==(const ParagraphStorageRequirement& left,
                           const ParagraphStorageRequirement& right) noexcept {
        return left.per_byte == right.per_byte && left.collapsed_bytes == right.collapsed_bytes &&
               left.lines == right.lines && left.glyph_runs == right.glyph_runs && left.glyphs == right.glyphs;
    }
};

// A laid-out paragraph's size and what it cost.
struct GUI_FORMS_API ParagraphMetrics final {
    std::size_t line_count{};
    // line_count line boxes: zero for an empty text, which has no line.
    double height{};
    double widest_line{};
    // The least exact status the provider answered for the break
    // opportunities, the cluster advances and every measurement (exact,
    // estimated or a missing face), in TextResolutionStatus's order. It
    // describes the text engine's answers only, never the fit: a provider
    // using the white-space estimate for breaks
    // (estimate_line_break_opportunities_utf8, TextMetricsProvider's
    // default) makes it estimated at any width.
    TextResolutionStatus status{TextResolutionStatus::exact};
    // The lines wider than ParagraphStyle::width (beyond the fit
    // tolerance). When nothing fits (a width of zero, or narrower than a
    // cluster) each line holds the smallest unit the overflow rule allows,
    // a word under ParagraphOverflowWrap::normal and a cluster under
    // break_word, and every line with visible text overflows. Zero when
    // every line fits.
    std::size_t overflowing_lines{};
    // Provider shaping calls (the cluster advances, each whole-run
    // measurement and, under ParagraphGlyphOutput::runs, each line shaped for
    // its glyphs) and the bytes they shaped: what the layout cost. A
    // measurement of the run the workspace has just shaped (a one-line
    // paragraph's line) is read from it and not counted.
    std::uint64_t shaping_calls{};
    std::uint64_t shaped_bytes{};
    // The size of the laid-out text (the collapsed size under collapse).
    std::size_t laid_out_bytes{};
    // Under ParagraphGlyphOutput::runs, the engine that shaped the glyph
    // runs and the runs and glyphs written (ParagraphGlyphRuns); the zero
    // source when none were filled (no glyph output, no workspace the
    // provider accepts, or a line the workspace could not stage).
    GlyphSource glyph_source{};
    std::size_t glyph_run_count{};
    std::size_t glyph_count{};
};

enum class ParagraphRequestError : std::uint8_t {
    // The text is not valid UTF-8.
    invalid_utf8,
    // The font is not one valid_font_spec admits.
    invalid_font,
    // The width is negative or not finite.
    invalid_width,
    // The line box's height is not positive and finite (not prepared with
    // paragraph_line_box).
    invalid_line_box,
};

// The result of lay_out_paragraph and measure_paragraph: laid out with its
// metrics; storage too small, with what it needs; or a refused request. On
// storage_too_small the lines array holds the first lines it could hold
// (partial) when the per-byte arrays were large enough, and nothing was
// written otherwise. A refused request writes nothing.
class GUI_FORMS_API ParagraphLayoutResult final {
public:
    [[nodiscard]] static ParagraphLayoutResult laid_out(const ParagraphMetrics& metrics) noexcept;
    [[nodiscard]] static ParagraphLayoutResult storage_too_small(
        const ParagraphStorageRequirement& required) noexcept;
    [[nodiscard]] static ParagraphLayoutResult refused(ParagraphRequestError error) noexcept;

    [[nodiscard]] bool succeeded() const noexcept;
    // The metrics of a laid-out paragraph; null otherwise. Valid while the
    // result lives.
    [[nodiscard]] const ParagraphMetrics* metrics() const noexcept;
    // What a call whose storage was too small needs; null otherwise.
    [[nodiscard]] const ParagraphStorageRequirement* required_storage() const noexcept;
    // The error of a refused request; none otherwise.
    [[nodiscard]] std::optional<ParagraphRequestError> error() const noexcept;

private:
    explicit ParagraphLayoutResult(
        std::variant<ParagraphMetrics, ParagraphStorageRequirement, ParagraphRequestError> state) noexcept;

    std::variant<ParagraphMetrics, ParagraphStorageRequirement, ParagraphRequestError> state_;
};

// The line box every line of a paragraph in `font` shares, as TextBlock's:
// the provider's resolution of a space in the effective font, then
// line_box_metrics with `line_height` (a used line height in logical
// pixels, already scaled; none for `line-height: normal`). A null provider
// uses estimate_text_layout_utf8. Resolving may allocate in the provider;
// call it once per font and keep the answer in ParagraphStyle.
[[nodiscard]] GUI_FORMS_API LineBoxMetrics paragraph_line_box(TextMetricsProvider* provider, FontSpec font,
                                                              std::optional<double> line_height);

// The storage lay_out_paragraph needs for `text` before the line count is
// known: per_byte and collapsed_bytes exact, lines the bound per_byte, and
// under `glyph_output` runs glyph_runs and glyphs per_byte (see
// ParagraphStorageRequirement). It reads the text only (counting the
// collapsed size under collapse) and allocates nothing.
[[nodiscard]] GUI_FORMS_API ParagraphStorageRequirement paragraph_storage_bound(
    std::string_view text, ParagraphWhiteSpace white_space,
    ParagraphGlyphOutput glyph_output = ParagraphGlyphOutput::none) noexcept;

// Lays `text` out in `style` into `storage`, through `provider` (a window's
// text_metrics_provider() or a Painter; null uses the renderer-free
// estimates). Inputs are read only; storage is written as ParagraphStorage
// states. The checks come first and change nothing: a refused request, then
// per-byte arrays (or a workspace) shorter than the text. Allocates nothing
// of its own. A workspace the provider refuses (one another provider made)
// is not used: the call goes on with the provider's plain calls. Not
// thread-safe with other calls into the same provider or workspace.
[[nodiscard]] GUI_FORMS_API ParagraphLayoutResult lay_out_paragraph(TextMetricsProvider* provider,
                                                                    std::string_view text,
                                                                    const ParagraphStyle& style,
                                                                    const ParagraphStorage& storage);

// The same layout without line records: the line count, height and widest
// line, for estimating rows (with ParagraphFit::estimated, one shaping per
// paragraph). storage.lines is ignored; the per-byte arrays (and
// collapsed_text under collapse) are required as for lay_out_paragraph.
[[nodiscard]] GUI_FORMS_API ParagraphLayoutResult measure_paragraph(TextMetricsProvider* provider,
                                                                    std::string_view text,
                                                                    const ParagraphStyle& style,
                                                                    const ParagraphStorage& storage);

// How paint_paragraph places a paragraph: `origin` is its box's top left in
// the painter's coordinates, `visible` the part of those coordinates worth
// drawing (lines whose line box misses it vertically are skipped without
// being visited), and each baseline is snapped to `device_scale`'s pixels
// with snap_text_baseline, as controls snap theirs.
struct GUI_FORMS_API ParagraphPaint final {
    Point origin{};
    Color color{};
    double device_scale{1.0};
    Rect visible{};
};

// Draws laid-out lines with Painter::draw_text_utf8, one call per non-empty
// visible line at (origin.x + line.x, snapped origin.y + line.baseline), in
// style.font. `text` is the text the lines were laid out from (the
// collapsed text under collapse) and `lines` their records, unchanged since.
// A line whose range lies outside `text` stops the drawing. Returns the
// lines drawn. A theme part draws a line the same way: its text is
// text.substr(line.bytes) and paint_text_part_at takes the same origin and
// line.width.
GUI_FORMS_API std::size_t paint_paragraph(Painter& painter, std::string_view text,
                                          std::span<const ParagraphLine> lines, const ParagraphStyle& style,
                                          const ParagraphPaint& paint);

// A laid-out paragraph's glyph runs (ParagraphGlyphOutput::runs), borrowed
// from the storage it was laid out into: its metrics' glyph_source, and the
// leading glyph_run_count runs and glyph_count glyphs of the storage's
// arrays (ParagraphBuffers::glyph_runs gives them).
struct GUI_FORMS_API ParagraphGlyphRuns final {
    GlyphSource source{};
    std::span<const GlyphRunRecord> runs{};
    std::span<const PositionedGlyph> glyphs{};
};

// As paint_paragraph above, drawing each line's glyph runs with
// Painter::draw_glyph_runs instead of shaping its text again, at the same
// origin: the same pixels with a painter of the engine that shaped them, and
// the text drawn otherwise. A line whose runs lie outside `glyphs.runs` is
// drawn as text; with the zero source every line is.
GUI_FORMS_API std::size_t paint_paragraph(Painter& painter, std::string_view text,
                                          std::span<const ParagraphLine> lines, const ParagraphGlyphRuns& glyphs,
                                          const ParagraphStyle& style, const ParagraphPaint& paint);

// Owned storage for repeated calls, sized once and reused: reserve for the
// largest text and line count expected, then pass storage() to every call.
// A call never grows it; on storage_too_small, reserve the requirement and
// call again. Its arrays are counted under AllocationSubsystem::text_layout.
// It also owns a shaping workspace once reserved with a provider: reserve
// with the provider the calls go through, so that their shaping allocates
// nothing either. Single-threaded, like the calls it serves.
class GUI_FORMS_API ParagraphBuffers final {
public:
    ParagraphBuffers() = default;
    // Grows the line and per-byte arrays to at least these sizes (never
    // shrinks them), and the workspace, if there is one, to per_byte. Lines
    // already written are kept; scratch contents are not.
    void reserve(std::size_t per_byte, std::size_t lines);
    // As above, with the glyph arrays grown to the requirement's.
    void reserve(const ParagraphStorageRequirement& required);
    // As above, and with a shaping workspace from `provider`
    // (TextMetricsProvider::create_shaping_workspace) reserved for
    // required.per_byte bytes: created on the first reserve with a provider,
    // and created again when the provider differs from the one that created
    // it (`provider` is compared, never kept for use). A provider without
    // workspaces, or null, leaves none. Throws as the workspace's reserve
    // does, with the arrays grown and the old workspace kept.
    void reserve(const ParagraphStorageRequirement& required, TextMetricsProvider* provider);
    // Frees all the storage, the workspace with it.
    void release() noexcept;
    // Spans over the whole arrays and the workspace; valid until the next
    // reserve or release.
    [[nodiscard]] ParagraphStorage storage() noexcept;
    // The lines a successful lay_out_paragraph with storage() wrote, given
    // its metrics' line_count; valid until the next reserve or release.
    [[nodiscard]] std::span<const ParagraphLine> lines(std::size_t line_count) const noexcept;
    // The collapsed text a collapse layout wrote, given its metrics'
    // laid_out_bytes; valid until the next reserve, release or call.
    [[nodiscard]] std::string_view collapsed_text(std::size_t laid_out_bytes) const noexcept;
    // The glyph runs a successful lay_out_paragraph with storage() wrote,
    // given its metrics; valid until the next reserve, release or call.
    [[nodiscard]] ParagraphGlyphRuns glyph_runs(const ParagraphMetrics& metrics) const noexcept;

private:
    template <typename Value>
    using CountedArray = std::vector<Value, detail::CountedAllocator<Value, AllocationSubsystem::text_layout>>;
    CountedArray<ParagraphLine> lines_{};
    CountedArray<char> collapsed_text_{};
    CountedArray<LineBreakOpportunity> opportunities_{};
    CountedArray<double> advances_{};
    CountedArray<GlyphRunRecord> glyph_runs_{};
    CountedArray<PositionedGlyph> glyphs_{};
    std::unique_ptr<ShapingWorkspace> workspace_{};
    // The provider that created workspace_, compared by address only.
    const TextMetricsProvider* workspace_provider_{};
};

} // namespace gui_forms
