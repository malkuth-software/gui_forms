#include "../src/render/text/prepared_window_shape.hpp"
#include "prepared_text_test_support.hpp"

#include <stxlib/font_file.hpp>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <cstdlib>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace gf = gui_forms;
namespace detail = gui_forms::detail;
namespace text = gui_forms::render::text;
using prepared_test::require;

namespace probe {
thread_local bool enabled = false;
thread_local std::size_t attempts = 0U;
thread_local std::size_t fail_at = std::numeric_limits<std::size_t>::max();
thread_local detail::PreparedWindowBatchAuthority* revoke = nullptr;
thread_local detail::PreparedLedger* close_ledger = nullptr;
void* allocate(const std::size_t bytes) {
    if (enabled) {
        const std::size_t index = attempts;
        ++attempts;
        if (index == fail_at) throw std::bad_alloc();
        if (index == 0U && close_ledger != nullptr) {
            std::lock_guard<std::mutex> lock((*close_ledger).mutex);
            (*close_ledger).closing = true;
        }
        if (index == 4U && revoke != nullptr) {
            std::lock_guard<std::mutex> lock((*revoke).mutex);
            (*revoke).desired.reset();
        }
    }
    const std::size_t requested = bytes == 0U ? 1U : bytes;
    void* result = std::malloc(requested);
    if (result == nullptr) throw std::bad_alloc();
    return result;
}
class Scope final {
public:
    explicit Scope(const std::size_t failure) { attempts = 0U; fail_at = failure; enabled = true; }
    ~Scope() { enabled = false; revoke = nullptr; close_ledger = nullptr; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};
}
void* operator new(const std::size_t bytes) { void* result = probe::allocate(bytes); return result; }
void* operator new[](const std::size_t bytes) { void* result = probe::allocate(bytes); return result; }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {
struct PublishedTraits final {
    using Run = decltype(std::declval<const detail::PreparedWindowGeometryRow&>().runs[0]);
    using Glyph = decltype(std::declval<const detail::PreparedWindowGeometryRow&>().glyphs[0]);
};
static_assert(std::is_same_v<PublishedTraits::Run, const text::BoundedFontRun&>);
static_assert(std::is_same_v<PublishedTraits::Glyph, const text::ShapedGlyph&>);

struct Fixture final {
    gf::PreparedTextService service{};
    gf::EncodedFontLease lease{};
    std::shared_ptr<const detail::PreparedFontBank> fonts{};
    std::shared_ptr<detail::PreparedLedger> ledger{};
    std::shared_ptr<detail::PreparedWindowBatchAuthority> authority{std::make_shared<detail::PreparedWindowBatchAuthority>()};
    detail::PreparedWindowKey key{};
    std::string display{};
    std::vector<detail::PreparedWindowParagraph> paragraphs{};
    std::vector<gf::PreparedSourceEndpoint> endpoints{};
    std::unique_ptr<detail::PreparedWindowBatchStorage> batch{};

    explicit Fixture(const std::span<const std::byte> encoded, const std::size_t count = 3U,
        const bool missing_last = false, const std::span<const std::byte> arabic = {},
        const std::span<const std::byte> hebrew = {}, const bool all_blank = false,
        const double logical_size = 16.0, const double scale = 1.25) {
        require(count >= 1U && count <= 512U, "bounded fixture row count");
        paragraphs.reserve(count);
        endpoints.reserve(1U + 2U * count);
        // Every fixture literal is at most 32 UTF-8 bytes, plus a CRLF.
        display.reserve(34U * count);
        const std::array<gf::PreparedFontSource, 3U> sources{{
            {.encoded = encoded, .role = gf::FontRole::content}, {.encoded = arabic}, {.encoded = hebrew}}};
        const std::size_t source_count = arabic.empty() && hebrew.empty() ? 1U : 3U;
        const std::span<const gf::PreparedFontSource> admitted(sources.data(), source_count);
        require(service.create_font_bank(admitted, lease) == gf::PreparedTextStatus::success, "exact fixture font bank");
        fonts = detail::PreparedTextAccess::fonts(lease);
        ledger = (*fonts).ledger;
        endpoints.push_back({gf::SourceByteOffset(100U), gf::DisplayByteOffset(7U)});
        for (std::size_t index = 0U; index < count; ++index) {
            const std::size_t begin = display.size();
            if (!all_blank && index + 1U != count && index % 3U != 1U) {
                if (!arabic.empty() && index % 6U == 0U) display.append("العربية 123");
                else if (!hebrew.empty() && index % 3U == 2U) display.append("שלום ABC");
                else display.append("office á");
            }
            if (index + 1U == count && missing_last) display.append("\xf4\x8f\xbf\xbf");
            const std::size_t end = display.size();
            if (end != begin) endpoints.push_back({gf::SourceByteOffset(100U + end),
                gf::DisplayByteOffset(7U + static_cast<std::uint32_t>(end))});
            detail::PreparedWindowSeparator separator = detail::PreparedWindowSeparator::none;
            if (index + 1U != count) {
                if (index % 3U == 0U) { display.append("\r\n"); separator = detail::PreparedWindowSeparator::crlf; }
                else if (index % 3U == 1U) { display.append("\r"); separator = detail::PreparedWindowSeparator::cr; }
                else { display.append("\n"); separator = detail::PreparedWindowSeparator::lf; }
            }
            const std::size_t after = display.size();
            if (after != end) endpoints.push_back({gf::SourceByteOffset(100U + after),
                gf::DisplayByteOffset(7U + static_cast<std::uint32_t>(after))});
            const detail::PreparedWindowParagraph paragraph{
                {gf::SourceByteOffset(100U + begin), gf::SourceByteOffset(100U + end)},
                gf::DisplayByteOffset(7U + static_cast<std::uint32_t>(begin)),
                gf::DisplayByteOffset(7U + static_cast<std::uint32_t>(end)), {true, true}, separator,
                {gf::SourceByteOffset(100U + end), gf::SourceByteOffset(100U + after)},
                gf::DisplayByteOffset(7U + static_cast<std::uint32_t>(end)),
                gf::DisplayByteOffset(7U + static_cast<std::uint32_t>(after))};
            paragraphs.push_back(paragraph);
        }
        key.text = prepared_test::make_key(service, lease, display, logical_size, scale);
        key.controller_instance = 1U;
        key.projection_generation = 1U;
        key.authority = {1U, 1U};
        require(detail::desire_prepared_window(*authority, key) == gf::PreparedTextStatus::success, "desire");
        const gf::DocumentMapSpan map{key.text.source, key.text.display_begin, key.text.display_end, gf::DocumentMapKind::identity_utf8};
        const std::size_t map_count = display.empty() ? 0U : 1U;
        const std::span<const gf::DocumentMapSpan> maps(&map, map_count);
        const detail::PreparedWindowView view{key, display, maps, endpoints, paragraphs, true};
        detail::PreparedWindowInput input{};
        require(detail::own_prepared_window(view, ledger, input) == gf::PreparedTextStatus::success, "owned certified input");
        require(detail::admit_prepared_window(key, authority, ledger, fonts, input, batch) == gf::PreparedTextStatus::success, "batch admission");
    }
};

struct Worker final {
    Fixture& fixture;
    gf::PreparedTextStatus status{gf::PreparedTextStatus::pending};
    void run() {
        text::PreparedWindowShaper shaper(fixture.fonts);
        status = shaper.initialize();
        if (status == gf::PreparedTextStatus::success) status = shaper.shape(*fixture.batch);
    }
};

// FreeType 2.14's 16.16 rounding, written out here as the oracle:
// FT_DivFix and FT_MulFix round half away from zero.
std::int64_t oracle_divide(const std::int64_t dividend, const std::int64_t divisor) {
    require(dividend >= 0 && divisor > 0, "oracle division of positive values");
    const std::int64_t quotient = (dividend * 65536 + divisor / 2) / divisor;
    return quotient;
}

std::int64_t oracle_multiply(const std::int64_t value, const std::int64_t scale) {
    const std::int64_t magnitude = value < 0 ? -value : value;
    const std::int64_t product = (magnitude * scale + 0x8000) / 65536;
    const std::int64_t result = value < 0 ? -product : product;
    return result;
}

// The whole pixel at or below `value` (26.6), in 26.6.
std::int64_t oracle_floor_pixel(const std::int64_t value) {
    std::int64_t pixels = value / 64;
    if (value % 64 != 0 && value < 0) --pixels;
    const std::int64_t floored = pixels * 64;
    return floored;
}

// The face's size metrics at `device_size` (26.6) as FreeType 2.14 gave
// them (y_scale = size / units per em; ascender up, descender down, height
// to the nearest whole pixel), from the face's design numbers as STXlib's
// independent font reader reads them.
text::PrimaryLineMetrics reference_primary_metrics(const detail::PreparedFontFace& source,
    const std::int64_t device_size) {
    stxlib::glyphs::Font font{};
    const stxlib::View<stxlib::u8> bytes(reinterpret_cast<const stxlib::u8*>(source.bytes.get()), source.size);
    const stxlib::OperationStatus opened = stxlib::glyphs::open_font(bytes, source.index, font);
    require(opened.succeeded() && font.units_per_em != 0U, "independent admitted font face");
    require(device_size > 0, "native metric size fits");
    const std::int64_t y_scale = oracle_divide(device_size, static_cast<std::int64_t>(font.units_per_em));
    const std::int64_t ascender = oracle_multiply(font.ascender, y_scale);
    const std::int64_t descender = oracle_multiply(font.descender, y_scale);
    const std::int64_t height = oracle_multiply(font.height, y_scale);
    const std::int64_t ascender_ceiling = oracle_floor_pixel(ascender + 63);
    const std::int64_t descender_floor = oracle_floor_pixel(descender);
    const std::int64_t height_rounded = oracle_floor_pixel(height + 32);
    text::PrimaryLineMetrics result{};
    result.ascent_device = static_cast<double>(ascender_ceiling) / 64.0;
    result.descent_device = -static_cast<double>(descender_floor) / 64.0;
    const double line_height = static_cast<double>(height_rounded) / 64.0;
    const double gap = line_height - result.ascent_device - result.descent_device;
    result.line_gap_device = std::max(0.0, gap);
    return result;
}

void compare_rows(Fixture& fixture) {
    text::HarfBuzzFontEngine reference{};
    const detail::PreparedFontBank& bank = *fixture.fonts;
    for (std::size_t index = 0U; index < bank.face_count; ++index) {
        const detail::PreparedFontFace& face = bank.faces[index];
        const std::span<const std::byte> bytes(face.bytes.get(), face.size);
        const std::optional<gf::FontFaceId> id = reference.register_owned_typeface(face.role, face.weight,
            face.italic, bytes, fixture.fonts, face.index);
        require(id.has_value(), "reference registered exact font");
    }
    const gf::PreparedTextMetrics metrics = detail::prepared_device_metrics(fixture.key.text.font, fixture.key.text.scale);
    gf::FontSpec font = fixture.key.text.font;
    font.size = static_cast<double>(metrics.device_size_26_6) / 64.0;
    font.letter_spacing *= fixture.key.text.scale;
    const text::PrimaryLineMetrics primary = reference_primary_metrics(bank.faces[0], metrics.device_size_26_6);
    const detail::PreparedWindowBatchStorage& batch = *fixture.batch;
    std::size_t requested = (*batch.input).charged_bytes + sizeof(batch) +
        batch.row_count * (sizeof(detail::PreparedWindowRowStorage) + sizeof(detail::PreparedWindowGeometryRow));
    double top = 0.0;
    double width = 0.0;
    for (std::size_t index = 0U; index < batch.row_count; ++index) {
        const detail::PreparedWindowParagraph& paragraph = fixture.paragraphs[index];
        const std::size_t begin = paragraph.display_begin.value - fixture.key.text.display_begin.value;
        const std::size_t length = paragraph.display_end.value - paragraph.display_begin.value;
        const std::string_view source(fixture.display.data() + begin, length);
        const std::unique_ptr<text::BoundedShapedText> expected = reference.shape_bounded(source, font, {});
        const detail::PreparedWindowGeometryRow& actual = batch.geometry[index];
        require(actual.paragraph_index == index && actual.run_count == (*expected).run_count &&
            actual.glyph_count == (*expected).glyph_count, "row identity/counts");
        require(actual.metrics.height_dip == (*expected).height / fixture.key.text.scale &&
            actual.metrics.advance_dip == (*expected).width / fixture.key.text.scale &&
            actual.metrics.ascent_dip == (*expected).ascent / fixture.key.text.scale &&
            actual.metrics.descent_dip == (*expected).descent / fixture.key.text.scale &&
            actual.metrics.device_size_26_6 == metrics.device_size_26_6, "row metrics and device rounding");
        const double ascent = std::max(primary.ascent_device, (*expected).ascent);
        const double descent = std::max(primary.descent_device, (*expected).descent);
        const double height = std::max((*expected).height, ascent + descent + primary.line_gap_device);
        require(actual.top_device == top && actual.baseline_device == top + ascent &&
            actual.height_device == height && height > 0.0, "device row placement matches independent primary metrics");
        top += height;
        width = std::max(width, (*expected).width);
        for (std::size_t run = 0U; run < actual.run_count; ++run) {
            require(actual.runs[run].source_range == (*expected).runs[run].source_range &&
                actual.runs[run].face == (*expected).runs[run].face &&
                actual.runs[run].glyph_begin == (*expected).runs[run].glyph_begin &&
                actual.runs[run].glyph_count == (*expected).runs[run].glyph_count, "paragraph-relative runs");
        }
        for (std::size_t glyph = 0U; glyph < actual.glyph_count; ++glyph)
            require(actual.glyphs[glyph] == (*expected).glyphs[glyph], "exact independent paragraph geometry");
        requested += actual.run_count * sizeof(text::BoundedFontRun) + actual.glyph_count * sizeof(text::ShapedGlyph);
    }
    require(requested == batch.requested_bytes, "exact retained bytes exclude retired temporary shape owners");
    require(batch.height_device == top && batch.width_device == width, "complete logical device extent");
}

void test_blank_placement(const std::span<const std::byte> font) {
    Fixture fixture(font, 512U, false, {}, {}, true);
    text::PreparedWindowShaper shaper(fixture.fonts);
    require(shaper.initialize() == gf::PreparedTextStatus::success, "blank-row shaper");
    require(shaper.shape(*fixture.batch) == gf::PreparedTextStatus::success, "all consecutive blanks positioned");
    compare_rows(fixture);
    const detail::PreparedWindowBatchStorage& batch = *fixture.batch;
    require(batch.row_count == 512U && batch.height_device > 4096.0 && batch.width_device == 0.0,
        "logical extent retains all rows beyond any later raster crop");
    for (std::size_t index = 0U; index < batch.row_count; ++index) {
        const detail::PreparedWindowGeometryRow& row = batch.geometry[index];
        require(row.glyph_count == 0U && row.run_count == 0U && row.metrics.ascent_dip == 0.0 &&
            row.metrics.descent_dip == 0.0 && row.baseline_device > row.top_device,
            "blank row has real baseline without mutating empty shaping semantics");
    }
}

void test_placement_sizes(const std::span<const std::byte> font) {
    struct Setting final { double size{}; double scale{}; };
    const std::array<Setting, 3U> settings{{{4.0, 0.5}, {16.123, 1.333}, {128.0, 4.0}}};
    for (const Setting setting : settings) {
        Fixture fixture(font, 1U, false, {}, {}, true, setting.size, setting.scale);
        text::PreparedWindowShaper shaper(fixture.fonts);
        require(shaper.initialize() == gf::PreparedTextStatus::success, "size-bound shaper");
        require(shaper.shape(*fixture.batch) == gf::PreparedTextStatus::success, "admitted size/scale placement");
        compare_rows(fixture);
        require(fixture.display.empty() && (*fixture.batch).row_count == 1U &&
            (*fixture.batch).height_device > 0.0, "empty EOF retains one positive line box");
    }
}

void test_exact_primary_metrics(const std::span<const std::byte> font) {
    text::HarfBuzzFontEngine engine{};
    const std::optional<gf::FontFaceId> fallback = engine.register_fallback_typeface(400U, false, font);
    require(fallback.has_value(), "fallback-only metrics fixture");
    gf::FontSpec specification{gf::FontRole::content, 20.0, 400U, false};
    require(!engine.primary_line_metrics(specification), "fallback cannot supply exact primary line metrics");
    const std::optional<gf::FontFaceId> primary = engine.register_typeface(gf::FontRole::content, 400U, false, font);
    require(primary.has_value(), "exact primary metrics face");
    const std::optional<text::PrimaryLineMetrics> metrics = engine.primary_line_metrics(specification);
    require(metrics && (*metrics).face == *primary, "exact admitted primary chosen ahead of fallback");
    specification.weight = 700U;
    require(!engine.primary_line_metrics(specification), "nearby weight is not an exact primary");
    specification.weight = 400U;
    specification.size = std::numeric_limits<double>::quiet_NaN();
    bool refused = false;
    try {
        const std::optional<text::PrimaryLineMetrics> invalid = engine.primary_line_metrics(specification);
        static_cast<void>(invalid);
    }
    catch (const std::invalid_argument&) { refused = true; }
    require(refused, "nonfinite metric size refused before native conversion");
}

// The fixture face with its hhea line gap set to `gap` font units. The
// bundled faces have none, so this is how a face with a gap (Calibri and
// Consolas have one) reaches the one line-height rule. The table directory
// is read in place: a 12-byte header, then 16-byte records of tag,
// checksum, offset and length; the line gap is hhea's signed 16-bit field
// at byte 8. The stale checksum is not read by the font reader.
std::vector<std::byte> with_line_gap(const std::span<const std::byte> font, const std::int16_t gap) {
    std::vector<std::byte> result(font.begin(), font.end());
    require(result.size() >= 12U, "font table directory header");
    const std::size_t tables = (static_cast<std::size_t>(result[4]) << 8U) | static_cast<std::size_t>(result[5]);
    require(result.size() >= 12U + 16U * tables, "font table directory records");
    for (std::size_t index = 0U; index < tables; ++index) {
        const std::size_t record = 12U + 16U * index;
        const bool hhea = result[record] == std::byte{'h'} && result[record + 1U] == std::byte{'h'} &&
            result[record + 2U] == std::byte{'e'} && result[record + 3U] == std::byte{'a'};
        if (!hhea) continue;
        std::size_t offset = 0U;
        for (std::size_t byte = 0U; byte < 4U; ++byte)
            offset = (offset << 8U) | static_cast<std::size_t>(result[record + 8U + byte]);
        require(offset + 10U <= result.size(), "hhea table in the font");
        const std::uint16_t encoded = static_cast<std::uint16_t>(gap);
        result[offset + 8U] = static_cast<std::byte>(encoded >> 8U);
        result[offset + 9U] = static_cast<std::byte>(encoded & 0xFFU);
        return result;
    }
    require(false, "font has an hhea table");
    return result;
}

// TextBox and prepared text share one line-height rule: the line box is the
// larger of the shaped height and ascent + descent + the primary face's
// rounded line gap. With a face that has a gap, a prepared row and the line
// box TextBox builds from resolve() are the same height, and include it.
void test_line_gap_rule(const std::span<const std::byte> font) {
    const std::vector<std::byte> gapped = with_line_gap(font, 250);
    Fixture fixture(gapped);
    Worker worker{fixture};
    std::thread thread(&Worker::run, &worker);
    thread.join();
    require(worker.status == gf::PreparedTextStatus::success, "a face with a line gap shapes");
    compare_rows(fixture);

    const gf::PreparedTextMetrics metrics = detail::prepared_device_metrics(fixture.key.text.font, fixture.key.text.scale);
    gf::FontSpec device_font = fixture.key.text.font;
    device_font.size = static_cast<double>(metrics.device_size_26_6) / 64.0;
    device_font.letter_spacing *= fixture.key.text.scale;
    text::HarfBuzzFontEngine engine{};
    const std::optional<gf::FontFaceId> face =
        engine.register_typeface(device_font.role, device_font.weight, device_font.italic, gapped);
    require(face.has_value(), "the gapped face registers as the primary");
    const gf::ResolvedTextLayout resolved = engine.resolve("office \xc3\xa1", device_font);
    const std::optional<text::PrimaryLineMetrics> primary = engine.primary_line_metrics(device_font);
    require(primary.has_value() && (*primary).line_gap_device > 0.0, "the face's rounded line gap is not zero");
    require(resolved.line_gap == (*primary).line_gap_device, "resolve() reports the primary face's line gap");
    const double line_box = std::max(1.0, std::max(resolved.logical_size.height,
        resolved.ascent + resolved.descent + resolved.line_gap));
    const detail::PreparedWindowGeometryRow& row = (*fixture.batch).geometry[0];
    require(line_box == row.height_device, "a TextBox line box and a prepared row are the same height");
}

void test_rows(const std::span<const std::byte> font, const std::span<const std::byte> arabic,
    const std::span<const std::byte> hebrew) {
    Fixture fixture(font, 512U, false, arabic, hebrew);
    const char* original = (*(*fixture.batch).input).display.get();
    Worker worker{fixture};
    std::thread thread(&Worker::run, &worker);
    thread.join();
    require(worker.status == gf::PreparedTextStatus::success, "worker can shape owner-admitted batch");
    require((*(*fixture.batch).input).display.get() == original, "input never copied or replaced");
    compare_rows(fixture);
    std::shared_ptr<const detail::PreparedWindowBatchStorage> retained(std::move(fixture.batch));
    require((*fixture.ledger).usage.payload_generations == 1U, "retained geometry keeps generation reservation");
    retained.reset();
    require((*fixture.ledger).usage.payload_generations == 0U, "last owner releases generation");
}

void test_failures(const std::span<const std::byte> font) {
    Fixture fixture(font);
    text::PreparedWindowShaper shaper(fixture.fonts);
    require(shaper.initialize() == gf::PreparedTextStatus::success, "shaper initialized");
    const std::size_t base = (*fixture.batch).requested_bytes;
    const char* original = (*(*fixture.batch).input).display.get();
    // Three rows: one nonempty and two empty; table + 3 + 1 + 1 allocations.
    for (std::size_t failure = 0U; failure < 6U; ++failure) {
        gf::PreparedTextStatus status{};
        {
            probe::Scope scope(failure);
            status = shaper.shape(*fixture.batch);
        }
        require(status == gf::PreparedTextStatus::resource_failure && !(*fixture.batch).geometry,
            "late allocation failure publishes no partial rows");
        require((*fixture.batch).requested_bytes == base && (*(*fixture.batch).input).display.get() == original &&
            (*fixture.ledger).usage.payload_generations == 1U && (*fixture.batch).width_device == 0.0 &&
            (*fixture.batch).height_device == 0.0, "failure preserves input/base/reservation/extent");
    }
    {
        probe::Scope scope(std::numeric_limits<std::size_t>::max());
        probe::revoke = fixture.authority.get();
        const gf::PreparedTextStatus status = shaper.shape(*fixture.batch);
        require(status == gf::PreparedTextStatus::stale, "revocation during later row refused");
    }
    require(!(*fixture.batch).geometry && (*fixture.batch).requested_bytes == base, "stale partial output discarded");
    (*fixture.authority).closing = true;
    require(shaper.shape(*fixture.batch) == gf::PreparedTextStatus::closing, "closing refused");
}

void test_zero_limits(const std::span<const std::byte> font) {
    text::HarfBuzzFontEngine engine{};
    const std::size_t charged = engine.configure_bounded_registration(1U, gf::PreparedTextLimits::workspace_bytes);
    require(charged != 0U, "bounded registration accounting");
    const std::optional<gf::FontFaceId> id = engine.register_typeface(gf::FontRole::content, 400U, false, font);
    require(id.has_value(), "zero-limit fixture font");
    text::BoundedShapeWorkspace workspace{};
    engine.prepare_workspace(workspace, {});
    text::ShapeStorageLimits limits{};
    limits.runs = 0U;
    limits.glyphs = 0U;
    limits.output_bytes = sizeof(text::BoundedShapedText);
    const gf::FontSpec specification{gf::FontRole::content, 16.0, 400U, false};
    std::unique_ptr<text::BoundedShapedText> output = engine.shape_with_workspace("", specification, limits, workspace);
    require((*output).height == 16.0 && (*output).ascent == 0.0 && (*output).descent == 0.0 &&
        (*output).run_count == 0U && (*output).glyph_count == 0U, "empty text succeeds at zero remaining counts");
    const text::BoundedShapedText* old = output.get();
    bool refused = false;
    try { output = engine.shape_with_workspace("x", specification, limits, workspace); }
    catch (const std::length_error&) { refused = true; }
    require(refused && output.get() == old, "nonempty exhausted count refuses unchanged");
    --limits.output_bytes;
    refused = false;
    try { output = engine.shape_with_workspace("", specification, limits, workspace); }
    catch (const std::length_error&) { refused = true; }
    require(refused && output.get() == old, "one byte below empty owner budget refuses");
    refused = false;
    try { engine.prepare_workspace(workspace, limits); }
    catch (const std::length_error&) { refused = true; }
    require(refused, "workspace preparation still requires positive capacities");
    refused = false;
    try { output = engine.shape_bounded("", specification, limits); }
    catch (const std::length_error&) { refused = true; }
    require(refused && output.get() == old, "original bounded API unchanged");
}

void test_late_coverage(const std::span<const std::byte> font) {
    Fixture fixture(font, 3U, true);
    text::PreparedWindowShaper shaper(fixture.fonts);
    require(shaper.initialize() == gf::PreparedTextStatus::success, "late-coverage shaper");
    const std::size_t original = (*fixture.batch).requested_bytes;
    require(shaper.shape(*fixture.batch) == gf::PreparedTextStatus::missing_font_coverage,
        "unsupported last paragraph refuses whole batch");
    require(!(*fixture.batch).geometry && (*fixture.batch).requested_bytes == original &&
        (*fixture.ledger).usage.payload_generations == 1U, "late coverage refusal preserves input/reservation");
}

void test_initialize_closing(const std::span<const std::byte> font) {
    Fixture fixture(font);
    text::PreparedWindowShaper shaper(fixture.fonts);
    gf::PreparedTextStatus status{};
    {
        probe::Scope scope(std::numeric_limits<std::size_t>::max());
        probe::close_ledger = fixture.ledger.get();
        status = shaper.initialize();
    }
    require(status == gf::PreparedTextStatus::closing, "Closing during initialization prevents publication");
    require(!(*fixture.batch).geometry && (*fixture.ledger).usage.payload_generations == 1U,
        "Initialization closing preserves admitted input reservation");
}

struct ForeignShaperCalls final {
    text::PreparedWindowShaper& shaper;
    detail::PreparedWindowBatchStorage& batch;
    gf::PreparedTextStatus initialized{gf::PreparedTextStatus::pending};
    gf::PreparedTextStatus shaped{gf::PreparedTextStatus::pending};

    void run() {
        initialized = shaper.initialize();
        shaped = shaper.shape(batch);
    }
};

void test_executor_and_existing_output(const std::span<const std::byte> font) {
    Fixture fixture(font);
    text::PreparedWindowShaper shaper(fixture.fonts);
    detail::PreparedWindowBatchStorage& batch = *fixture.batch;
    const char* input = (*batch.input).display.get();
    const std::size_t input_bytes = batch.requested_bytes;
    ForeignShaperCalls uninitialized{shaper, batch};
    std::thread first_thread(&ForeignShaperCalls::run, &uninitialized);
    first_thread.join();
    require(uninitialized.initialized == gf::PreparedTextStatus::wrong_executor &&
        uninitialized.shaped == gf::PreparedTextStatus::wrong_executor,
        "Foreign executor cannot initialize or shape an uninitialized context");
    require(!batch.geometry && batch.requested_bytes == input_bytes &&
        (*batch.input).display.get() == input,
        "Wrong-executor refusal preserves admitted input and accounting");

    require(shaper.initialize() == gf::PreparedTextStatus::success, "Owner initializes shaper");
    require(shaper.initialize() == gf::PreparedTextStatus::busy, "Initialized context cannot be replaced");
    require(shaper.shape(batch) == gf::PreparedTextStatus::success, "Owner shapes complete batch");
    const detail::PreparedWindowGeometryRow* geometry = batch.geometry.get();
    const std::size_t shaped_bytes = batch.requested_bytes;
    require(shaper.shape(batch) == gf::PreparedTextStatus::busy,
        "A populated batch refuses another shaping operation");
    ForeignShaperCalls populated{shaper, batch};
    std::thread second_thread(&ForeignShaperCalls::run, &populated);
    second_thread.join();
    require(populated.initialized == gf::PreparedTextStatus::wrong_executor &&
        populated.shaped == gf::PreparedTextStatus::wrong_executor,
        "Foreign executor remains refused after initialization and publication");
    require(batch.geometry.get() == geometry && batch.requested_bytes == shaped_bytes &&
        (*batch.input).display.get() == input && (*fixture.ledger).usage.payload_generations == 1U,
        "Busy and wrong-executor refusals preserve completed geometry and its reservation");
}

void test_owner_context_budget(const std::span<const std::byte> font) {
    std::size_t baseline_peak = 0U;
    {
        Fixture baseline(font);
        text::PreparedWindowShaper shaper(baseline.fonts);
        require(shaper.initialize() == gf::PreparedTextStatus::success, "baseline context initializes");
        require(shaper.shape(*baseline.batch) == gf::PreparedTextStatus::success, "baseline context shapes");
        baseline_peak = (*baseline.batch).workspace_peak_bytes;
    }
    Fixture fixture(font);
    constexpr std::size_t owner_bytes = 4096U;
    text::PreparedWindowShaper charged(fixture.fonts, owner_bytes);
    require(charged.initialize() == gf::PreparedTextStatus::success, "charged context initializes");
    require(charged.shape(*fixture.batch) == gf::PreparedTextStatus::success, "charged context shapes");
    require((*fixture.batch).workspace_peak_bytes == baseline_peak + owner_bytes &&
        (*fixture.batch).workspace_peak_bytes <= gf::PreparedTextLimits::workspace_bytes,
        "external owner context is included in aggregate workspace reporting");
    text::PreparedWindowShaper excessive(fixture.fonts, std::numeric_limits<std::size_t>::max());
    gf::PreparedTextStatus refused = gf::PreparedTextStatus::success;
    std::size_t allocations = 0U;
    {
        probe::Scope scope(0U);
        refused = excessive.initialize();
        allocations = probe::attempts;
    }
    require(refused == gf::PreparedTextStatus::budget_exceeded && allocations == 0U,
        "oversized owner context refuses before allocation or overflowing addition");
}

void test_projection(const std::span<const std::byte> font) {
    Fixture fixture(font, 1U);
    fixture.batch.reset();
    fixture.display = "[U+0000]";
    fixture.key.text = prepared_test::make_key(fixture.service, fixture.lease, fixture.display, 16.0, 1.25);
    text::PreparedWindowShaper shaper(fixture.fonts);
    require(shaper.initialize() == gf::PreparedTextStatus::success, "Projection shaper initialized");
    std::unique_ptr<detail::PreparedWindowBatchStorage> projected{};
    for (std::size_t variant = 0U; variant < 2U; ++variant) {
        const bool atomic = variant == 0U;
        const std::uint64_t source_length = atomic ? 1U : fixture.display.size();
        fixture.key.text.source.end = gf::SourceByteOffset(100U + source_length);
        fixture.key.text.page.permitted = fixture.key.text.source;
        ++fixture.key.authority.epoch;
        require(detail::desire_prepared_window(*fixture.authority, fixture.key) == gf::PreparedTextStatus::success,
            "Projection variant desire");
        const gf::DocumentMapKind kind = atomic ? gf::DocumentMapKind::atomic_token : gf::DocumentMapKind::identity_utf8;
        const gf::DocumentMapSpan map{fixture.key.text.source, fixture.key.text.display_begin,
            fixture.key.text.display_end, kind};
        const std::array<gf::PreparedSourceEndpoint, 2U> endpoints{{
            {fixture.key.text.source.begin, fixture.key.text.display_begin},
            {fixture.key.text.source.end, fixture.key.text.display_end}}};
        const detail::PreparedWindowParagraph paragraph{fixture.key.text.source, fixture.key.text.display_begin,
            fixture.key.text.display_end, {true, true}, detail::PreparedWindowSeparator::none,
            {fixture.key.text.source.end, fixture.key.text.source.end}, fixture.key.text.display_end,
            fixture.key.text.display_end};
        const detail::PreparedWindowView view{fixture.key, fixture.display, {&map, 1U}, endpoints, {&paragraph, 1U}, true};
        detail::PreparedWindowInput input{};
        require(detail::own_prepared_window(view, fixture.ledger, input) == gf::PreparedTextStatus::success,
            "Certified projected or literal input");
        require(detail::admit_prepared_window(fixture.key, fixture.authority, fixture.ledger, fixture.fonts,
            input, fixture.batch) == gf::PreparedTextStatus::success, "Projection admission");
        require(shaper.shape(*fixture.batch) == gf::PreparedTextStatus::success, "Projection shaping");
        const detail::PreparedWindowInputData& retained = *(*fixture.batch).input;
        require(retained.mappings[0].kind == kind &&
            retained.paragraphs[0].content.begin.value == fixture.key.text.source.begin.value &&
            retained.paragraphs[0].content.end.value == fixture.key.text.source.end.value,
            "Shaping preserves distinct source mapping for identical display text");
        if (atomic) projected = std::move(fixture.batch);
    }
    const detail::PreparedWindowGeometryRow& atomic = (*projected).geometry[0];
    const detail::PreparedWindowGeometryRow& literal = (*fixture.batch).geometry[0];
    require(atomic.glyph_count == literal.glyph_count && atomic.run_count == literal.run_count,
        "Display-identical projection and literal shape counts match");
    for (std::size_t index = 0U; index < atomic.glyph_count; ++index)
        require(atomic.glyphs[index] == literal.glyphs[index], "Glyph clusters stay display-relative");
}

std::vector<std::byte> read_fallback(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(static_cast<bool>(input), "fallback fixture opens");
    const std::streampos end = input.tellg();
    const std::streamoff offset = static_cast<std::streamoff>(end);
    require(offset > 0 && offset <= 4 * 1024 * 1024, "bounded fallback bytes");
    const std::size_t size = static_cast<std::size_t>(offset);
    std::vector<std::byte> bytes(size);
    char* destination = reinterpret_cast<char*>(bytes.data());
    const std::streamsize count = static_cast<std::streamsize>(size);
    input.seekg(0);
    input.read(destination, count);
    require(static_cast<bool>(input), "fallback fixture read");
    return bytes;
}
} // namespace

int main(const int argc, char** argv) {
    try {
        require(argc == 2, "font directory required");
        const std::filesystem::path directory(argv[1]);
        const std::vector<std::byte> font = prepared_test::read_font(directory);
        const std::filesystem::path arabic_path = directory / "NotoSansArabic-Minimal.ttf";
        const std::filesystem::path hebrew_path = directory / "NotoSansHebrew-Regular.ttf";
        const std::vector<std::byte> arabic = read_fallback(arabic_path);
        const std::vector<std::byte> hebrew = read_fallback(hebrew_path);
        test_rows(font, arabic, hebrew);
        test_blank_placement(font);
        test_placement_sizes(font);
        test_exact_primary_metrics(font);
        test_line_gap_rule(font);
        test_failures(font);
        test_zero_limits(font);
        test_late_coverage(font);
        test_initialize_closing(font);
        test_executor_and_existing_output(font);
        test_owner_context_budget(font);
        test_projection(font);
        std::cout << "Prepared window shaping fixtures passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
