#include "harfbuzz_font_engine.hpp"
#include "bounded_shape_workspace.hpp"
#include "reusable_shape_workspace.hpp"
#include "../../../core/resources/fonts/program_typefaces.hpp"
#include "../textlayout/text_analysis.hpp"
#include "../../../core/text/unicode/unicode_grapheme.hpp"
#include "../../../core/types/restrict/restrict.hpp"
#include "gui_forms/typography/app_fonts/app_fonts.hpp"
#include "gui_forms/typography/font_families/font_families.hpp"

#include <stxlib/font_file.hpp>
#include <stxlib/glyph_face.hpp>
#include <stxlib/glyph_shape.hpp>
#include <stxlib/result.hpp>
#include <stxlib/text.hpp>
#include <stxlib/textlayout.hpp>
#include <stxlib/view.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace gui_forms::render::text {
namespace {

constexpr std::size_t maximum_face_bytes = 64U * 1024U * 1024U;
constexpr std::size_t maximum_faces = 64U;
constexpr std::uint32_t maximum_glyphs = 1'000'000U;

void account_array(std::size_t count, std::size_t element_bytes,
                   std::size_t limit, std::size_t& used) {
    if (used > limit || count > (limit - used) / element_bytes) {
        throw std::length_error("Controlled shaping storage exceeds its admitted limit");
    }
    const std::size_t bytes = count * element_bytes;
    used += bytes;
}

std::size_t decode_scalars_to(std::string_view utf8, Utf8Range range,
                              std::span<char32_t> output) {
    std::size_t offset = range.start.value();
    std::size_t written = 0;
    while (offset < range.end.value()) {
        const unsigned char first = static_cast<unsigned char>(utf8[offset]);
        char32_t value{};
        std::size_t count{};
        if (first < 0x80U) { value = first; count = 1U; }
        else if ((first & 0xe0U) == 0xc0U) { value = first & 0x1fU; count = 2U; }
        else if ((first & 0xf0U) == 0xe0U) { value = first & 0x0fU; count = 3U; }
        else { value = first & 0x07U; count = 4U; }
        for (std::size_t index = 1U; index < count; ++index) {
            value = (value << 6U) | (static_cast<unsigned char>(utf8[offset + index]) & 0x3fU);
        }
        if (written == output.size()) throw std::length_error("Scalar scratch capacity exceeded");
        output[written] = value;
        ++written;
        offset += count;
    }
    return written;
}

struct DirectionRun final { Utf8Range range{}; bool rtl{}; };

// Each paragraph of `utf8` (already validated) reordered as one line: the
// runs of every paragraph in visual order, paragraph after paragraph. The
// analysis is local: its storage is counted under text_layout while the
// call holds it.
std::vector<DirectionRun> visual_direction_runs(std::string_view utf8) {
    std::vector<DirectionRun> runs{};
    const stxlib::Result<stxlib::TextView> validated = stxlib::validate_utf8(utf8.data(), utf8.size());
    const stxlib::TextView* const text = validated.value();
    if (text == nullptr) throw std::runtime_error("Bidi analysis of invalid UTF-8");
    TextAnalysis analysis{};
    std::size_t offset = 0;
    while (offset < utf8.size()) {
        const std::size_t length = analyse_paragraph_line(*text, offset, analysis);
        const stxlib::View<stxlib::textlayout::BidiRun> line_runs = analysis.line.runs();
        const std::size_t count = line_runs.count();
        for (std::size_t index = 0; index < count; ++index) {
            const stxlib::textlayout::BidiRun& run = line_runs[index];
            const Utf8Range range{Utf8Offset(run.offset), Utf8Offset(run.offset + run.length)};
            const DirectionRun direction{range, (run.level & 1U) != 0U};
            runs.push_back(direction);
        }
        offset += length;
    }
    return runs;
}

// A process-unique identity for each engine (GlyphSource::engine): never
// zero, never reused.
[[nodiscard]] std::uint64_t next_engine_identity() noexcept {
    static std::atomic<std::uint64_t> next{1U};
    const std::uint64_t identity = next.fetch_add(1U, std::memory_order_relaxed);
    return identity;
}

// Throws for a failed STXlib call: std::bad_alloc when its storage ran out,
// else std::runtime_error naming `what` (STXlib refused the font, the run or
// one of its limits). Shaping's callers treat a runtime_error as the face's
// failure (an application family is quarantined) and bad_alloc as the call's.
void require_stxlib(const stxlib::Status& status, const char* const what) {
    if (status.succeeded()) return;
    const stxlib::ErrorRecord* const record = status.error();
    if (record != nullptr && (*record).kind() == stxlib::ErrorKind::out_of_memory) throw std::bad_alloc();
    throw std::runtime_error(what);
}

// A shaping size: the font's size clamped to 1 to 4096 pixels, in 26.6.
[[nodiscard]] std::int64_t shaping_size_of(const FontSpec& font) noexcept {
    const double bounded_size = std::clamp(font.size, 1.0, 4096.0);
    const long long fixed_size = std::llround(bounded_size * 64.0);
    const std::int64_t size = static_cast<std::int64_t>(fixed_size);
    return size;
}

// FreeType 2.14's 16.16 arithmetic, which its size metrics are made with:
// FT_DivFix (a * 65536 / b, rounded half away from zero; 0x7FFFFFFF for
// b = 0) and FT_MulFix (a * b / 65536, rounded half away from zero).
[[nodiscard]] std::int64_t fixed_divide(const std::int64_t dividend, const std::int64_t divisor) noexcept {
    const bool negative = (dividend < 0) != (divisor < 0);
    const std::uint64_t magnitude = dividend < 0 ? static_cast<std::uint64_t>(-dividend) : static_cast<std::uint64_t>(dividend);
    const std::uint64_t by = divisor < 0 ? static_cast<std::uint64_t>(-divisor) : static_cast<std::uint64_t>(divisor);
    if (by == 0U) return 0x7FFFFFFF;
    const std::uint64_t quotient = ((magnitude << 16U) + (by >> 1U)) / by;
    const std::int64_t signed_quotient = static_cast<std::int64_t>(quotient);
    const std::int64_t result = negative ? -signed_quotient : signed_quotient;
    return result;
}

[[nodiscard]] std::int64_t fixed_multiply(const std::int64_t value, const std::int64_t scale) noexcept {
    const bool negative = (value < 0) != (scale < 0);
    const std::uint64_t magnitude = value < 0 ? static_cast<std::uint64_t>(-value) : static_cast<std::uint64_t>(value);
    const std::uint64_t by = scale < 0 ? static_cast<std::uint64_t>(-scale) : static_cast<std::uint64_t>(scale);
    const std::uint64_t product = (magnitude * by + 0x8000U) >> 16U;
    const std::int64_t signed_product = static_cast<std::int64_t>(product);
    const std::int64_t result = negative ? -signed_product : signed_product;
    return result;
}

// A face's line metrics at a size, in 26.6 pixels, y upward (the descender
// is negative).
struct SizedLineMetrics final {
    std::int64_t ascender{};
    std::int64_t descender{};
    std::int64_t height{};
};

// The line metrics FreeType 2.14 gave a scalable face at a nominal size of
// `size` (26.6) at 72 dpi (FT_Request_Metrics, then
// ft_recompute_scaled_metrics with GRID_FIT_METRICS, which every size of the
// face's FT_Size_Metrics was): y_scale is size over units per em in 16.16
// (fixed_divide); each of the font's ascender, descender and height (STXlib
// picks them by FreeType's rule) times y_scale (fixed_multiply); then the
// ascender rounded up to a whole pixel, the descender down and the height to
// the nearest. Font units per em are never 0 in a shapeable font.
[[nodiscard]] SizedLineMetrics sized_line_metrics_of(const stxlib::glyphs::Font& font, const std::int64_t size) noexcept {
    const std::int64_t y_scale = fixed_divide(size, static_cast<std::int64_t>(font.units_per_em));
    const std::int64_t ascender = fixed_multiply(static_cast<std::int64_t>(font.ascender), y_scale);
    const std::int64_t descender = fixed_multiply(static_cast<std::int64_t>(font.descender), y_scale);
    const std::int64_t height = fixed_multiply(static_cast<std::int64_t>(font.height), y_scale);
    SizedLineMetrics result{};
    result.ascender = (ascender + 63) & -64;
    result.descender = descender & -64;
    result.height = (height + 32) & -64;
    return result;
}

// Whether the engine shapes with `font`: outlines STXlib reads, a glyph count
// from 1 to maximum_glyphs, units per em, and a Unicode character map (the
// faces FreeType's Unicode charmap selection admitted before).
[[nodiscard]] bool shapeable_font(const stxlib::glyphs::Font& font) noexcept {
    const bool shapeable = font.outlines != stxlib::glyphs::OutlineKind::none && font.glyph_count != 0U &&
        font.glyph_count <= maximum_glyphs && font.units_per_em != 0U && font.character_map.length != 0U &&
        font.character_map_kind == stxlib::glyphs::character_map_unicode;
    return shapeable;
}

// utf8 is already validated; range contains complete scalars. The caller owns
// scratch storage and reserves at least range.length bytes as a scalar bound.
void decode_scalars(std::string_view utf8, Utf8Range range,
                    std::vector<char32_t>& scalars) {
    scalars.clear();
    std::size_t offset = range.start.value();
    while (offset < range.end.value()) {
        const unsigned char first = static_cast<unsigned char>(utf8[offset]);
        char32_t value{};
        std::size_t count{};
        if (first < 0x80U) {
            value = first;
            count = 1U;
        } else if ((first & 0xe0U) == 0xc0U) {
            value = first & 0x1fU;
            count = 2U;
        } else if ((first & 0xf0U) == 0xe0U) {
            value = first & 0x0fU;
            count = 3U;
        } else {
            value = first & 0x07U;
            count = 4U;
        }
        for (std::size_t index = 1U; index < count; ++index) {
            value = (value << 6U) |
                (static_cast<unsigned char>(utf8[offset + index]) & 0x3fU);
        }
        scalars.push_back(value);
        offset += count;
    }
}

[[nodiscard]] bool ignorable_for_coverage(char32_t scalar) noexcept {
    const bool ignorable = scalar == U'\u200c' || scalar == U'\u200d' ||
           (scalar >= U'\ufe00' && scalar <= U'\ufe0f') ||
           (scalar >= U'\U000e0100' && scalar <= U'\U000e01ef');
    return ignorable;
}

// A face's nominal glyph map, read from its open font's character map.
struct FontGlyphMap final {
    const stxlib::glyphs::Font* font{};
    [[nodiscard]] bool operator()(char32_t scalar) const noexcept {
        const bool mapped = stxlib::glyphs::glyph_of(*font, static_cast<std::uint32_t>(scalar)) != 0U;
        return mapped;
    }
};

// A lazy face's nominal glyph map, read from its compiled coverage table.
struct TableGlyphMap final {
    std::span<const gui_forms::detail::CodepointRange> ranges{};
    [[nodiscard]] bool operator()(char32_t scalar) const noexcept {
        const bool mapped = gui_forms::detail::coverage_contains(ranges, scalar);
        return mapped;
    }
};

// Whether a face whose nominal glyph map is `mapped` has a glyph for every
// scalar of the cluster `scalars` that coverage does not ignore.
template <typename GlyphMap>
[[nodiscard]] bool nominally_covered(const GlyphMap& mapped, std::span<const char32_t> scalars) {
    for (const char32_t scalar : scalars) {
        if (ignorable_for_coverage(scalar)) continue;
        if (!mapped(scalar)) return false;
    }
    return true;
}

// One face's glyph ink boxes at shaping sizes (Typeface::ink_box: the box of
// the unfitted outline, as hb-ft's extents were; STXlib keeps nothing of
// them, so a shaping that read them each time would read the glyph's outline
// each time). An open-addressed table keyed by glyph id and size (26.6),
// probed linearly; it records refusals as well as boxes. Growth doubles the table while it is more than half full, up to
// maximum_entries; a full table at that size is emptied and refilled. find
// never allocates; keep allocates only to grow, and a growth that fails
// leaves the table as it was (the extents are then simply not kept). The
// entries count under AllocationSubsystem::text_layout. One thread: the
// engine's.
class GlyphInkCache final {
public:
    struct Entry final {
        // The glyph id plus one; zero marks an empty slot.
        std::uint32_t glyph_plus_one{};
        std::int64_t size{};
        bool available{};
        stxlib::glyphs::InkBox box{};
    };

    // The kept entry for `glyph` at `size`, or null.
    [[nodiscard]] const Entry* find(const std::uint32_t glyph, const std::int64_t size) const noexcept {
        if (entries_.empty() || glyph == std::numeric_limits<std::uint32_t>::max()) return nullptr;
        const std::uint32_t wanted = static_cast<std::uint32_t>(glyph) + 1U;
        const std::size_t mask = entries_.size() - 1U;
        std::size_t slot = slot_of(wanted, size, mask);
        for (std::size_t probes = 0U; probes < entries_.size(); ++probes) {
            const Entry& entry = entries_[slot];
            if (entry.glyph_plus_one == 0U) return nullptr;
            if (entry.glyph_plus_one == wanted && entry.size == size) return &entry;
            slot = (slot + 1U) & mask;
        }
        return nullptr;
    }

    // Keeps `glyph`'s box (or its refusal) at `size`; not already kept.
    void keep(const std::uint32_t glyph, const std::int64_t size, const bool available,
              const stxlib::glyphs::InkBox box) noexcept {
        if (glyph == std::numeric_limits<std::uint32_t>::max()) return;
        if ((count_ + 1U) * 2U > entries_.size()) {
            if (entries_.size() < maximum_entries) {
                if (!grow()) return;
            } else {
                for (Entry& entry : entries_) entry = Entry{};
                count_ = 0U;
            }
        }
        Entry entry{};
        entry.glyph_plus_one = static_cast<std::uint32_t>(glyph) + 1U;
        entry.size = size;
        entry.available = available;
        entry.box = box;
        place(entries_, entry);
        ++count_;
    }

private:
    using Entries = std::vector<Entry, gui_forms::detail::CountedAllocator<Entry, AllocationSubsystem::text_layout>>;
    static constexpr std::size_t initial_entries = 64U;
    // 8192 entries of 40 bytes: 320 KiB a face at most.
    static constexpr std::size_t maximum_entries = 8192U;

    [[nodiscard]] static std::size_t slot_of(const std::uint32_t glyph_plus_one, const std::int64_t size,
                                             const std::size_t mask) noexcept {
        const std::uint64_t size_bits = static_cast<std::uint64_t>(size);
        const std::uint64_t mixed = (static_cast<std::uint64_t>(glyph_plus_one) * 0x9E3779B97F4A7C15ULL) ^
                                    (size_bits * 0xC2B2AE3D27D4EB4FULL);
        const std::size_t slot = static_cast<std::size_t>(mixed >> 32U) & mask;
        return slot;
    }

    // Places `entry` in the first empty slot of its probe sequence; the
    // table has one.
    static void place(Entries& table, const Entry& entry) noexcept {
        const std::size_t mask = table.size() - 1U;
        std::size_t slot = slot_of(entry.glyph_plus_one, entry.size, mask);
        while (table[slot].glyph_plus_one != 0U) slot = (slot + 1U) & mask;
        table[slot] = entry;
    }

    // Doubles the table (or creates it), moving the kept entries; false,
    // changing nothing, when the allocation fails.
    [[nodiscard]] bool grow() noexcept {
        const std::size_t capacity = entries_.empty() ? initial_entries : entries_.size() * 2U;
        Entries grown{};
        try {
            grown.resize(capacity);
        } catch (const std::bad_alloc&) {
            return false;
        }
        for (const Entry& entry : entries_) {
            if (entry.glyph_plus_one != 0U) place(grown, entry);
        }
        entries_.swap(grown);
        return true;
    }

    Entries entries_{};
    std::size_t count_{};
};

} // namespace

class HarfBuzzFontEngine::Impl final {
public:
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
    TextLayoutDiagnostics diagnostics{};
    TextLayoutFailure failure{TextLayoutFailure::none};
    std::uint64_t failure_run{};
    [[nodiscard]] bool take_failure(TextLayoutFailure point) noexcept {
        if (failure != point || failure_run != diagnostics.append_calls) { return false; }
        failure = TextLayoutFailure::none;
        return true;
    }
#endif
    struct Face final {
        FontFaceId id{};
        std::optional<FontRole> role;
        std::uint16_t weight{};
        bool italic{};
        bool fallback{};
        std::string family;
        std::shared_ptr<const void> encoded;
        // The program's typeface of the face's bytes (program_typeface: the
        // registry's when the bytes are a registered face's, else one shared
        // by every holder of these bytes), with what keeps it. Null until the
        // face is open. Declared before the shaper, which holds its typeface.
        gui_forms::detail::TypefaceBorrow typeface{};
        // The encoded face the typeface reads (kept alive by `encoded`).
        std::span<const std::byte> bytes{};
        std::uint32_t face_index{};
        // The shaper over the typeface, opened at the face's first shaping and
        // kept so it keeps the face's plans. One thread: the engine's.
        stxlib::glyphs::Shaper shaper{};
        bool shaper_open{};
        // A lazy face: typeface stays null until coverage first covers a needed cluster, when
        // loader runs and is then released. A lazy face with neither typeface nor loader
        // could not be opened and covers nothing.
        std::span<const gui_forms::detail::CodepointRange> coverage{};
        LazyFaceLoader loader{};
        // The family (FontSpec::family or a stack's) this face is registered for, or zero for
        // a role or fallback face. A family face serves only text whose family order lists its
        // family. A role face's family is its role's (family_of).
        FontFamilyId named_family{};
        // The language a fallback face is drawn for first (set_face_language), or none.
        TextLanguage language{};
        // A fallback face's glyph ink boxes at shaping sizes (append_run).
        GlyphInkCache ink_cache{};

        Face(FontFaceId id_value, std::optional<FontRole> role_value,
             std::uint16_t weight_value, bool italic_value,
             bool fallback_value, std::string family_value,
             std::shared_ptr<const void> bytes_owner)
            : id(id_value), role(role_value), weight(weight_value),
              italic(italic_value), fallback(fallback_value),
              family(std::move(family_value)), encoded(std::move(bytes_owner)) {}
        ~Face() = default;
        Face(const Face&) = delete;
        Face& operator=(const Face&) = delete;
        Face(Face&& other) noexcept
            : id(other.id), role(other.role), weight(other.weight),
              italic(other.italic), fallback(other.fallback),
              family(std::move(other.family)), encoded(std::move(other.encoded)),
              typeface(std::move(other.typeface)), bytes(other.bytes), face_index(other.face_index),
              shaper(std::move(other.shaper)), shaper_open(std::exchange(other.shaper_open, false)),
              coverage(other.coverage), loader(std::move(other.loader)), named_family(other.named_family),
              language(other.language), ink_cache(std::move(other.ink_cache)) {
            other.typeface.typeface = nullptr;
        }
        Face& operator=(Face&&) = delete;

        [[nodiscard]] bool open() const noexcept { return typeface.typeface != nullptr; }
        // The open face's font: its numbers and character map. Requires open().
        [[nodiscard]] const stxlib::glyphs::Font& font() const noexcept {
            const stxlib::Shared<stxlib::glyphs::Typeface>& shared = (*typeface.typeface).typeface();
            const stxlib::glyphs::Font& result = *(*shared).font();
            return result;
        }
    };

    // The family a face belongs to: its registered family, its role's family, or zero for a
    // fallback face.
    [[nodiscard]] static FontFamilyId family_of(const Face& face) noexcept {
        if (face.named_family.value != 0U) return face.named_family;
        if (face.role) return role_font_family(*face.role);
        return FontFamilyId{};
    }

    // Candidate order: the families of the font's order first (font_family_order: its family,
    // then its stack's), in that order; then the role's faces, the content faces and the
    // fallback faces. Within a tier the face nearest the weight and slant comes first
    // (font_face_nearer); a stable sort keeps registration order among equals.
    struct FacePreference final {
        FontSpec font{};
        FontFamilyOrder order{};

        explicit FacePreference(const FontSpec& requested) noexcept
            : font(requested), order(font_family_order(requested)) {}

        // Fallback faces drawn for the font's language (set_face_language) come before the
        // other fallback faces.
        [[nodiscard]] int tier(const Face& face) const noexcept {
            const std::optional<std::size_t> position = font_family_position(order, family_of(face));
            if (position) return static_cast<int>(*position) - static_cast<int>(order.families.size());
            if (face.named_family.value != 0U) return 4;
            if (face.role && *face.role == font.role) return 0;
            if (face.role && *face.role == FontRole::content) return 1;
            const bool language_face = font.language.tag != 0U && face.language == font.language;
            if (language_face) return 2;
            return 3;
        }

        [[nodiscard]] bool operator()(const Face* left,
                                      const Face* right) const noexcept {
            const int left_tier = tier(*left);
            const int right_tier = tier(*right);
            if (left_tier != right_tier) {
                const bool preferred = left_tier < right_tier;
                return preferred;
            }
            const bool nearer = font_face_nearer((*left).weight, (*left).italic, (*right).weight, (*right).italic,
                                                 font.weight, font.italic);
            return nearer;
        }
    };

    std::vector<Face> faces{};
    // The scratch coverage's composition checks shape in (covers), reused.
    ShapeScratch coverage_scratch{};
    std::uint64_t next_face_id{1U};
    std::size_t bounded_face_limit{};
    // The application families (app_fonts.hpp) open_app_families has met, opened or not: each
    // is registered once per engine.
    std::vector<FontFamilyId> app_families{};
    // This engine's GlyphSource::engine.
    std::uint64_t identity{next_engine_identity()};

    // Whether `face` takes part in shaping text in `font`, whose family order is `order`. A
    // family face takes part only when the order lists its family, and only once it is open,
    // so the front candidate always has a face; one that cannot be loaded leaves the next
    // family or the role's faces in charge. A role face takes part for its own role, as the
    // house text fallback (the content faces serve the control and monospace roles too: this
    // keeps Portsmouth the preferred control voice without turning punctuation, arrows or
    // localized text it does not own into missing-glyph boxes), or when the order lists its
    // role's family. Fallback faces (CJK, emoji) always take part, in the last tier.
    [[nodiscard]] bool admits(Face& face, const FontSpec& font, const FontFamilyOrder& order) {
        if (face.named_family.value != 0U) {
            if (!font_family_position(order, face.named_family)) return false;
            if (!face.open() && (!face.loader || !open_lazy(face))) return false;
            return true;
        }
        if (!face.role) return true;
        if (*face.role == font.role) return true;
        if (font.role != FontRole::content && *face.role == FontRole::content) return true;
        const bool listed = font_family_position(order, role_font_family(*face.role)).has_value();
        return listed;
    }

    // Registers the faces of each live application family in `font`'s family order the first
    // time this engine meets the family, as family faces holding the registry's owner of the
    // bytes (which defers an unload until this engine goes). A face STXlib cannot open
    // quarantines the family, so the next family or the role's faces draw; faces already
    // registered for it stay out of every order from then on. Bounded engines never register
    // application families (prepared text draws bundled families only).
    void open_app_families(const FontSpec& font) {
        if (bounded_face_limit != 0U) return;
        const FontFamilyOrder order = font_family_order(font);
        for (std::size_t position = 0U; position < order.count; ++position) {
            const FontFamilyId family = order.families[position];
            if (!app_font_family_number(family)) continue;
            if (std::find(app_families.begin(), app_families.end(), family) != app_families.end()) continue;
            app_families.push_back(family);
            std::array<AppFontFaceSource, maximum_app_font_faces> sources{};
            const std::size_t count = app_font_family_faces(family, sources);
            bool opened = count != 0U;
            for (std::size_t index = 0U; index < count && opened; ++index) {
                // A full engine is not the font's failure: the family is left unregistered.
                if (faces.size() >= maximum_faces) return;
                const AppFontFaceSource& source = sources[index];
                const std::optional<FontFaceId> id =
                    register_family_face(family, source.weight, source.italic, source.bytes, source.owner, 0U);
                opened = id.has_value();
            }
            if (count != 0U && !opened) quarantine_app_font_family(family, AppFontQuarantine::open_failed);
        }
    }

    [[nodiscard]] std::vector<Face*> candidates(FontSpec font) {
        open_app_families(font);
        std::vector<Face*> result{};
        result.reserve(faces.size());
        const FacePreference preference{font};
        for (Face& face : faces) {
            if (admits(face, font, preference.order)) result.push_back(&face);
        }
        std::stable_sort(result.begin(), result.end(), preference);
        return result;
    }

    // candidates()'s answer written into `output` instead of a new vector:
    // the same faces in the same order (FacePreference, stable, by
    // insertion), opening a family face the first time text names its
    // family as candidates() does. Returns the count; std::length_error when
    // `output` is too short.
    [[nodiscard]] std::size_t fill_candidates(FontSpec font, std::span<Face*> output) {
        open_app_families(font);
        std::size_t count = 0U;
        const FacePreference preference{font};
        for (Face& face : faces) {
            if (!admits(face, font, preference.order)) continue;
            if (count == output.size()) throw std::length_error("Face candidate capacity exceeded");
            std::size_t position = count;
            while (position != 0U && preference(&face, output[position - 1U])) {
                output[position] = output[position - 1U];
                --position;
            }
            output[position] = &face;
            ++count;
        }
        return count;
    }

    [[nodiscard]] bool has_primary(FontRole role) const noexcept {
        for (const Face& face : faces) {
            if (face.role && *face.role == role) return true;
        }
        return false;
    }

    // Whether `face` covers the cluster. A lazy face answers from its table and is opened
    // the first time the table covers a cluster; one that cannot be opened covers nothing.
    // Whether `face` covers the cluster: a glyph for every scalar coverage does not ignore,
    // or, for a cluster of two scalars, one the face's shaping composes them into (a base and
    // a combining mark the face has only precomposed). A lazy face answers from its table
    // and is opened the first time the table covers a cluster, or the base of a two-scalar
    // one; a face that cannot be opened covers nothing.
    [[nodiscard]] bool covers(Face& face, std::span<const char32_t> scalars) {
        if (!face.open()) {
            if (!face.loader) return false;
            const TableGlyphMap table{face.coverage};
            const bool tabled = nominally_covered(table, scalars) ||
                (scalars.size() == 2U && table(scalars[0]));
            if (!tabled) return false;
            if (!open_lazy(face)) return false;
        }
        const FontGlyphMap mapped{&face.font()};
        if (nominally_covered(mapped, scalars)) return true;
        if (scalars.size() != 2U) return false;
        const bool composed = composes(face, scalars);
        return composed;
    }

    // Whether shaping the two scalars in `face` gives no missing glyph (STXlib composes a
    // canonical pair where the face has the composed character). Keeps the original text and
    // offsets: only a bounded same-face coverage check.
    [[nodiscard]] bool composes(Face& face, std::span<const char32_t> scalars) {
        open_shaper(face);
        coverage_scratch.characters.resize(scalars.size());
        for (std::size_t index = 0U; index < scalars.size(); ++index) {
            coverage_scratch.characters[index] = static_cast<std::uint32_t>(scalars[index]);
        }
        stxlib::glyphs::TextRun run{};
        run.characters = stxlib::View<std::uint32_t>(coverage_scratch.characters.data(), coverage_scratch.characters.size());
        const stxlib::Status shaped = face.shaper.shape(run, coverage_scratch.glyphs);
        require_stxlib(shaped, "STXlib refused a coverage shaping");
        for (const stxlib::glyphs::ShapedGlyph& glyph : coverage_scratch.glyphs) {
            if (glyph.glyph == 0U) return false;
        }
        return true;
    }

    // Opens the face's shaper if it is not open. Throws as require_stxlib does.
    void open_shaper(Face& face) {
        if (face.shaper_open) return;
        const stxlib::Status opened = face.shaper.open((*face.typeface.typeface).typeface());
        require_stxlib(opened, "STXlib refused a face's shaper");
        face.shaper_open = true;
    }

    // The program's typeface of face `face_index` of `bytes`, kept alive by `owner`, when
    // the engine can shape with it; an empty borrow when STXlib refuses the face or its font
    // is not one the engine shapes with (shapeable_font). std::bad_alloc when the typeface
    // table's storage runs out.
    [[nodiscard]] static gui_forms::detail::TypefaceBorrow open_typeface(
        std::span<const std::byte> bytes, const std::shared_ptr<const void>& owner, std::uint32_t face_index) {
        gui_forms::detail::TypefaceBorrow borrow = gui_forms::detail::program_typeface(bytes, owner, face_index);
        if (borrow.typeface == nullptr) return borrow;
        const stxlib::glyphs::Font* const font = (*(*borrow.typeface).typeface()).font();
        if (font == nullptr || !shapeable_font(*font)) {
            gui_forms::detail::TypefaceBorrow refused{};
            return refused;
        }
        return borrow;
    }

    // A face's name for resolve(): its registry family's name (a role face's role family, a
    // family face's family). STXlib does not yet read a font's name table, so a fallback
    // face, which has no registry family, is named by the empty string.
    [[nodiscard]] static std::string registry_family_name(const Face& face) {
        const FontFamilyRecord* const record = font_family_record(family_of(face));
        std::string name{};
        if (record != nullptr) name = std::string((*record).name);
        return name;
    }

    // Loads and opens a lazy face. The loader is released once it has answered, so a face
    // that cannot be opened is not loaded again; a throwing loader is kept for a retry.
    [[nodiscard]] bool open_lazy(Face& face) {
        LazyFaceBytes loaded = face.loader();
        gui_forms::detail::TypefaceBorrow typeface{};
        if (loaded.owner && !loaded.bytes.empty() && loaded.bytes.size() <= maximum_face_bytes) {
            typeface = open_typeface(loaded.bytes, loaded.owner, 0U);
        }
        if (typeface.typeface == nullptr) {
            face.loader = nullptr;
            return false;
        }
        std::string family{};
        if (bounded_face_limit == 0) family = registry_family_name(face);
        // Nothing below throws.
        face.loader = nullptr;
        face.family = std::move(family);
        face.encoded = std::move(loaded.owner);
        face.bytes = loaded.bytes;
        face.face_index = 0U;
        face.typeface = std::move(typeface);
        return true;
    }

    [[nodiscard]] std::optional<FontFaceId> register_face(
        std::optional<FontRole> role, std::uint16_t weight, bool italic,
        std::span<const std::byte> bytes, std::shared_ptr<const void> owned,
        std::uint32_t face_index) {
        if (bounded_face_limit != 0 && faces.size() == bounded_face_limit) return std::nullopt;
        if (!owned || bytes.empty() || bytes.size() > maximum_face_bytes ||
            faces.size() >= maximum_faces || face_index > 255U) {
            return std::nullopt;
        }
        gui_forms::detail::TypefaceBorrow typeface = open_typeface(bytes, owned, face_index);
        if (typeface.typeface == nullptr) return std::nullopt;
        // Family-name allocation and vector growth can both throw; the borrow
        // is released with this frame until the face holds it.
        const FontFaceId id{next_face_id++};
        Face& face = faces.emplace_back(id, role, weight, italic, !role.has_value(), std::string{}, std::move(owned));
        // Nothing below throws but the name's allocation, which leaves an
        // unnamed face.
        face.bytes = bytes;
        face.face_index = face_index;
        face.typeface = std::move(typeface);
        if (bounded_face_limit == 0) face.family = registry_family_name(face);
        return id;
    }

    [[nodiscard]] std::optional<FontFaceId> register_lazy_face(
        std::uint16_t weight, bool italic, std::span<const gui_forms::detail::CodepointRange> coverage,
        LazyFaceLoader load) {
        if (bounded_face_limit != 0 && faces.size() == bounded_face_limit) return std::nullopt;
        if (!load || coverage.empty() || faces.size() >= maximum_faces) return std::nullopt;
        const FontFaceId id{next_face_id++};
        Face& face = faces.emplace_back(id, std::nullopt, weight, italic, true, std::string{},
                                        std::shared_ptr<const void>{});
        face.coverage = coverage;
        face.loader = std::move(load);
        return id;
    }

    [[nodiscard]] std::optional<FontFaceId> register_lazy_family_face(
        FontFamilyId family, std::uint16_t weight, bool italic, LazyFaceLoader load) {
        if (bounded_face_limit != 0) return std::nullopt;
        if (family.value == 0U || !valid_font_family(family) || !load || faces.size() >= maximum_faces) {
            return std::nullopt;
        }
        const FontFaceId id{next_face_id++};
        Face& face = faces.emplace_back(id, std::nullopt, weight, italic, false, std::string{},
                                        std::shared_ptr<const void>{});
        face.loader = std::move(load);
        face.named_family = family;
        return id;
    }

    // An open face of `family`, admitted by bounded engines too (prepared text's font banks),
    // except for an application family, which only an unbounded engine registers.
    [[nodiscard]] std::optional<FontFaceId> register_family_face(
        FontFamilyId family, std::uint16_t weight, bool italic, std::span<const std::byte> bytes,
        std::shared_ptr<const void> owned, std::uint32_t face_index) {
        if (family.value == 0U || !known_font_family(family)) return std::nullopt;
        if (app_font_family_number(family) && bounded_face_limit != 0U) return std::nullopt;
        const std::optional<FontFaceId> id =
            register_face(std::nullopt, weight, italic, bytes, std::move(owned), face_index);
        if (!id) return id;
        // Nothing below throws: the face register_face appended becomes a family face.
        Face& face = faces.back();
        face.fallback = false;
        face.named_family = family;
        if (bounded_face_limit == 0) face.family = registry_family_name(face);
        return id;
    }

    // One synchronous shape call's working storage: the scratch each run is shaped in (a
    // reusable workspace lends its own). Registration cannot run concurrently; the face
    // vector stays unchanged for the call.
    struct ShapeCall final {
        ShapeScratch scratch{};
    };

    // A face's ink box of `glyph` at `size`: from the face's cache, or read once from its
    // typeface (Typeface::ink_box) and kept. False when the typeface has no box for the
    // glyph.
    static bool glyph_ink_box(Face& face, const std::uint32_t glyph, const std::int64_t size,
                              stxlib::glyphs::InkBox& box) {
        const GlyphInkCache::Entry* const kept = face.ink_cache.find(glyph, size);
        if (kept != nullptr) {
            box = (*kept).box;
            return (*kept).available;
        }
        stxlib::glyphs::Typeface& typeface = *(*face.typeface.typeface).typeface();
        const stxlib::Status measured = typeface.ink_box(glyph, size, box);
        const stxlib::ErrorRecord* const record = measured.error();
        if (record != nullptr && (*record).kind() == stxlib::ErrorKind::out_of_memory) throw std::bad_alloc();
        const bool available = measured.succeeded();
        if (!available) box = stxlib::glyphs::InkBox{};
        face.ink_cache.keep(glyph, size, available, box);
        return available;
    }

    // Bounded: the run is written into `bounded`'s arrays (Output is
    // BoundedShapedText or StagedShaping, which share these members) and
    // never grows them.
    template <bool Bounded = false, typename Output = BoundedShapedText>
    void append_run(ShapeCall& call, ShapedText& result, Face& face, std::string_view utf8,
                    Utf8Range range, FontSpec font, double run_origin,
                    bool add_trailing_spacing, bool rtl, Output* bounded = nullptr) {
        if constexpr (Bounded) {
            if ((*bounded).run_count == (*bounded).run_capacity) {
                throw std::length_error("Prepared run capacity exceeded");
            }
        }
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
        ++diagnostics.append_calls;
        TextLayoutPhaseTimer append_timer(diagnostics, TextLayoutPhase::append_total);
#endif
        const std::int64_t size = shaping_size_of(font);
        {
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
            TextLayoutPhaseTimer open_timer(diagnostics, TextLayoutPhase::shaper_open);
            if (take_failure(TextLayoutFailure::shaper_open)) throw std::bad_alloc();
#endif
            open_shaper(face);
        }
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
        TextLayoutPhaseTimer buffer_timer(diagnostics, TextLayoutPhase::buffer_setup);
        if (take_failure(TextLayoutFailure::glyph_storage)) throw std::bad_alloc();
#endif
        // The run's characters and, for each, its cluster: the byte offset of its first byte
        // in `utf8` (graphemes keep their offsets in either direction).
        ShapeScratch& scratch = call.scratch;
        scratch.characters.clear();
        scratch.clusters.clear();
        std::size_t offset = range.start.value();
        while (offset < range.end.value()) {
            const unsigned char first = static_cast<unsigned char>(utf8[offset]);
            std::uint32_t value{};
            std::size_t count{};
            if (first < 0x80U) { value = first; count = 1U; }
            else if ((first & 0xe0U) == 0xc0U) { value = first & 0x1fU; count = 2U; }
            else if ((first & 0xf0U) == 0xe0U) { value = first & 0x0fU; count = 3U; }
            else { value = first & 0x07U; count = 4U; }
            for (std::size_t index = 1U; index < count; ++index) {
                value = (value << 6U) | (static_cast<unsigned char>(utf8[offset + index]) & 0x3fU);
            }
            scratch.characters.push_back(value);
            scratch.clusters.push_back(static_cast<std::uint32_t>(offset));
            offset += count;
        }
        stxlib::glyphs::TextRun run_text{};
        run_text.characters = stxlib::View<std::uint32_t>(scratch.characters.data(), scratch.characters.size());
        run_text.clusters = stxlib::View<std::uint32_t>(scratch.clusters.data(), scratch.clusters.size());
        run_text.direction = rtl ? stxlib::glyphs::TextDirection::right_to_left
                                 : stxlib::glyphs::TextDirection::left_to_right;
        // The script is the first character's that has one (as HarfBuzz guessed it); the
        // language is the paragraph's.
        run_text.script = 0U;
        run_text.language = font.language.tag;
        // ADR-004: advances are the outlines' own, unhinted at the logical size, so glyph
        // positions agree with the light-hinted glyphs drawn at any device scale. Hinted
        // logical-size advances drifted lines by up to 12 logical px.
        run_text.size = size;
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
        buffer_timer.stop();
        TextLayoutPhaseTimer shape_timer(diagnostics, TextLayoutPhase::shape);
        if (take_failure(TextLayoutFailure::shape_refused)) throw std::runtime_error("Diagnostic shaping refusal");
#endif
        const stxlib::Status shaped = face.shaper.shape(run_text, scratch.glyphs);
        require_stxlib(shaped, "STXlib shaping failed");
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
        if (take_failure(TextLayoutFailure::after_shape)) { throw std::bad_alloc(); }
        shape_timer.stop();
        TextLayoutPhaseTimer output_timer(diagnostics, TextLayoutPhase::glyph_output);
#endif
        const std::size_t count = scratch.glyphs.count();
        ShapedFontRun run{};
        run.face = face.id;
        run.source_range = range;
#if defined(GUI_FORMS_TEXT_LAYOUT_GEOMETRY_TRACE)
        run.diagnostic_rtl = rtl;
#endif
        if constexpr (Bounded) {
            if (count > (*bounded).glyph_capacity - (*bounded).glyph_count) {
                throw std::length_error("Prepared glyph capacity exceeded");
            }
        } else {
            run.glyphs.reserve(count);
        }
        double pen_x = run_origin;
        double pen_y{};
        double ink_ascent = font.size * 0.8;
        double ink_descent = font.size * 0.2;
        for (std::size_t index = 0U; index < count; ++index) {
            const stxlib::glyphs::ShapedGlyph& shaped_glyph = scratch.glyphs[index];
            const float offset_x = static_cast<float>(shaped_glyph.x_offset) / 64.0F;
            const std::int64_t inverted_y_offset = -static_cast<std::int64_t>(shaped_glyph.y_offset);
            const float offset_y = static_cast<float>(inverted_y_offset) / 64.0F;
            float advance_x = static_cast<float>(shaped_glyph.x_advance) / 64.0F;
            const std::int64_t inverted_y_advance = -static_cast<std::int64_t>(shaped_glyph.y_advance);
            const float advance_y = static_cast<float>(inverted_y_advance) / 64.0F;
            const bool cluster_end = index + 1U == count || scratch.glyphs[index + 1U].cluster != shaped_glyph.cluster;
            if (cluster_end && (index + 1U != count || add_trailing_spacing)) {
                advance_x += static_cast<float>(font.letter_spacing);
            }
            const ShapedGlyph glyph{GlyphId{shaped_glyph.glyph},
                                  Utf8Offset(shaped_glyph.cluster),
                                  static_cast<float>(pen_x) + offset_x,
                                  static_cast<float>(pen_y) + offset_y,
                                  advance_x, advance_y};
            if constexpr (Bounded) {
                (*bounded).glyphs[(*bounded).glyph_count + index] = glyph;
            } else {
                run.glyphs.push_back(glyph);
            }
            // A fallback face's line takes the glyphs' ink: the box is y upward from the
            // baseline, the line y downward.
            stxlib::glyphs::InkBox box{};
            if (!face.role && glyph_ink_box(face, shaped_glyph.glyph, size, box)) {
                const double top = pen_y + offset_y - static_cast<double>(box.top) / 64.0;
                const double bottom = pen_y + offset_y - static_cast<double>(box.bottom) / 64.0;
                ink_ascent = std::max(ink_ascent, -top);
                ink_descent = std::max(ink_descent, bottom);
            }
            pen_x += advance_x;
            pen_y += advance_y;
        }
        result.width = std::max(result.width, pen_x);
        // Fallback families reserve space for every mark in their repertoire. Use the actual
        // shaped ink (including positioned marks) so an Arabic or CJK fallback does not double
        // a compact control's line box; a role face takes its sized line metrics.
        double ascent = ink_ascent;
        double descent = ink_descent;
        if (face.role) {
            const SizedLineMetrics sized = sized_line_metrics_of(face.font(), size);
            ascent = static_cast<double>(sized.ascender) / 64.0;
            descent = -static_cast<double>(sized.descender) / 64.0;
        }
        result.ascent = std::max(result.ascent, ascent);
        result.descent = std::max(result.descent, descent);
        result.height = result.ascent + result.descent;
        if constexpr (Bounded) {
            (*bounded).runs[(*bounded).run_count] = BoundedFontRun{
                .face = face.id, .source_range = range,
                .glyph_begin = (*bounded).glyph_count, .glyph_count = count, .right_to_left = rtl};
            ++(*bounded).run_count;
            (*bounded).glyph_count += count;
        } else {
            result.runs.push_back(std::move(run));
        }
    }
    std::size_t workspace_external_bytes(const std::size_t limit) const {
        if (bounded_face_limit == 0U) {
            throw std::invalid_argument("Reusable workspace requires bounded font registration");
        }
        std::size_t bytes = 0U;
        account_array(1U, sizeof(HarfBuzzFontEngine), limit, bytes);
        account_array(1U, sizeof(Impl), limit, bytes);
        account_array(faces.capacity(), sizeof(Face), limit, bytes);
        account_array(maximum_faces, sizeof(Face*), limit, bytes);
        account_array(1U, sizeof(ShapeCall), limit, bytes);
        account_array(1U, sizeof(BidiOwner), limit, bytes);
        account_array(1U, sizeof(ShapedText), limit, bytes);
        return bytes;
    }

    std::size_t bounded_candidates(const FontSpec font, const std::span<Face*> candidates) {
        std::size_t count = 0U;
        const FacePreference preference{font};
        for (Face& face : faces) {
            // Bounded (prepared) engines hold open faces only, family faces among them
            // (register_family_face), so admission opens nothing here.
            if (!admits(face, font, preference.order)) continue;
            if (count == candidates.size()) throw std::length_error("Face candidate capacity exceeded");
            std::size_t position = count;
            while (position != 0U && preference(&face, candidates[position - 1U])) {
                candidates[position] = candidates[position - 1U];
                --position;
            }
            candidates[position] = &face;
            ++count;
        }
        return count;
    }

    // Validated input and complete grapheme edges; nonempty candidates all
    // borrow this engine's unchanged face table. Scratch arrays are disjoint,
    // scalar/segment/direction extents cover the validated scalar count and
    // visual covers twice that count. No array grows; native owners stay local.
    // Overlap rule: the text, edges and the four scratch arrays share no
    // element (asserted in debug and sanitizer builds), the promise
    // fill_bounded_parts's restricted parameters make. `call` holds the
    // call's fonts and shaping scratch (a scratch lent to it is reused);
    // `analysis` is the bidi analysis to reuse (the workspace's, or the
    // bounded call's own). Output is BoundedShapedText or StagedShaping.
    template <typename Output>
    void fill_bounded_runs(const std::string_view utf8, const FontSpec font, ShapeCall& call,
        TextAnalysis& analysis,
        const std::span<Face* const> candidates, const std::span<const std::size_t> edges,
        const std::span<char32_t> scalar_scratch, const std::span<WorkspaceFontSegment> segments,
        const std::span<WorkspaceDirectionRun> directions, const std::span<WorkspaceVisualSegment> visual,
        Output& output) {
        namespace overlap = gui_forms::detail;
        const std::array<overlap::ByteRange, 4> written{overlap::byte_range(scalar_scratch),
            overlap::byte_range(segments), overlap::byte_range(directions), overlap::byte_range(visual)};
        const std::array<overlap::ByteRange, 2> read{
            overlap::ByteRange{utf8.data(), utf8.size()}, overlap::byte_range(edges)};
        overlap::require_disjoint_writes(written, read);
        fill_bounded_parts(utf8.data(), utf8.size(), font, call, analysis, candidates, edges.data(), edges.size(),
            scalar_scratch.data(), scalar_scratch.size(), segments.data(), segments.size(), directions.data(),
            directions.size(), visual.data(), visual.size(), output);
    }

    // fill_bounded_runs's work. Restricted parameters: no element is reached
    // through two of them (fill_bounded_runs checks it).
    template <typename Output>
    void fill_bounded_parts(const char* GUI_FORMS_RESTRICT text, const std::size_t text_size, const FontSpec font,
        ShapeCall& call, TextAnalysis& analysis, const std::span<Face* const> candidates, const std::size_t* GUI_FORMS_RESTRICT edge_data,
        const std::size_t edge_count, char32_t* GUI_FORMS_RESTRICT scalar_data, const std::size_t scalar_size,
        WorkspaceFontSegment* GUI_FORMS_RESTRICT segment_data, const std::size_t segment_size,
        WorkspaceDirectionRun* GUI_FORMS_RESTRICT direction_data, const std::size_t direction_size,
        WorkspaceVisualSegment* GUI_FORMS_RESTRICT visual_data, const std::size_t visual_size,
        Output& output) {
        const std::string_view utf8(text, text_size);
        const std::span<const std::size_t> edges(edge_data, edge_count);
        const std::span<char32_t> scalar_scratch(scalar_data, scalar_size);
        const std::span<WorkspaceFontSegment> segments(segment_data, segment_size);
        const std::span<WorkspaceDirectionRun> directions(direction_data, direction_size);
        const std::span<WorkspaceVisualSegment> visual(visual_data, visual_size);
        const std::size_t candidate_count = candidates.size();
        const std::size_t scalar_capacity = scalar_scratch.size();
        const std::size_t visual_capacity = visual.size();
        std::size_t segment_count = 0;
        for (std::size_t index = 1; index < edges.size(); ++index) {
            const Utf8Range range{Utf8Offset(edges[index - 1U]), Utf8Offset(edges[index])};
            const std::size_t scalar_count = decode_scalars_to(utf8, range, scalar_scratch);
            const std::span<const char32_t> cluster(scalar_scratch.data(), scalar_count);
            Impl::Face* selected = nullptr;
            for (std::size_t face_index = 0; face_index < candidate_count; ++face_index) {
                if (covers(*candidates[face_index], cluster)) { selected = candidates[face_index]; break; }
            }
            if (selected == nullptr) { selected = candidates[0]; ++output.missing_clusters; }
            const std::size_t selected_index = static_cast<std::size_t>(selected - faces.data());
            if (segment_count != 0 && segments[segment_count - 1U].face_index == selected_index) {
                segments[segment_count - 1U].range.end = range.end;
            } else {
                if (segment_count == scalar_capacity) throw std::length_error("Font segment capacity exceeded");
                segments[segment_count] = WorkspaceFontSegment{.face_index = selected_index, .range = range};
                ++segment_count;
            }
        }

        // Each paragraph reordered as one line (stxlib::textlayout); the runs'
        // offsets are bytes of the whole text.
        const stxlib::Result<stxlib::TextView> validated = stxlib::validate_utf8(utf8.data(), utf8.size());
        const stxlib::TextView* const text = validated.value();
        if (text == nullptr) throw std::runtime_error("Bounded bidi analysis of invalid UTF-8");
        std::size_t offset = 0;
        std::size_t direction_count = 0;
        while (offset < utf8.size()) {
            const std::size_t length = analyse_paragraph_line(*text, offset, analysis);
            const stxlib::View<stxlib::textlayout::BidiRun> runs = analysis.line.runs();
            const std::size_t count = runs.count();
            if (count > scalar_capacity - direction_count) throw std::length_error("Direction capacity exceeded");
            for (std::size_t index = 0; index < count; ++index) {
                const stxlib::textlayout::BidiRun& run = runs[index];
                directions[direction_count] = WorkspaceDirectionRun{
                    .range = {Utf8Offset(run.offset), Utf8Offset(run.offset + run.length)}, .rtl = (run.level & 1U) != 0};
                ++direction_count;
            }
            offset += length;
        }
        std::size_t visual_count = 0;
        for (std::size_t index = 0; index < direction_count; ++index) {
            const WorkspaceDirectionRun& direction = directions[index];
            const std::size_t begin = visual_count;
            for (std::size_t segment_index = 0; segment_index < segment_count; ++segment_index) {
                const WorkspaceFontSegment& segment = segments[segment_index];
                const std::size_t start = std::max(segment.range.start.value(), direction.range.start.value());
                const std::size_t end = std::min(segment.range.end.value(), direction.range.end.value());
                if (start < end) {
                    if (visual_count == visual_capacity) throw std::length_error("Visual run capacity exceeded");
                    visual[visual_count] = WorkspaceVisualSegment{.face_index = segment.face_index,
                        .range = {Utf8Offset(start), Utf8Offset(end)}, .rtl = direction.rtl};
                    ++visual_count;
                }
            }
            if (direction.rtl) std::reverse(visual.data() + begin, visual.data() + visual_count);
        }
        if (visual_count > output.run_capacity) throw std::length_error("Prepared run capacity exceeded");
        ShapedText metrics{};
        double origin = 0;
        for (std::size_t index = 0; index < visual_count; ++index) {
            const WorkspaceVisualSegment& segment = visual[index];
            const double before = metrics.width;
            append_run<true>(call, metrics, faces[segment.face_index], utf8, segment.range, font,
                origin, index + 1U != visual_count, segment.rtl, &output);
            origin += std::max(0.0, metrics.width - before);
        }
        output.width = metrics.width;
        output.height = metrics.height;
        output.ascent = metrics.ascent;
        output.descent = metrics.descent;
    }
};

HarfBuzzFontEngine::HarfBuzzFontEngine() : impl_(std::make_unique<Impl>()) {}
HarfBuzzFontEngine::~HarfBuzzFontEngine() = default;

std::size_t HarfBuzzFontEngine::configure_bounded_registration(std::size_t face_limit, std::size_t byte_limit) {
    Impl& implementation = *impl_;
    if (!implementation.faces.empty() || implementation.bounded_face_limit != 0 ||
        face_limit == 0 || face_limit > maximum_faces) throw std::invalid_argument("Bounded face initialization order");
    std::size_t bytes = sizeof(HarfBuzzFontEngine) + sizeof(Impl);
    account_array(face_limit, sizeof(Impl::Face), byte_limit, bytes);
    implementation.faces.reserve(face_limit);
    implementation.bounded_face_limit = face_limit;
    // Count actual capacity too; no face insertion may grow this table.
    bytes = sizeof(HarfBuzzFontEngine) + sizeof(Impl);
    account_array(implementation.faces.capacity(), sizeof(Impl::Face), byte_limit, bytes);
    return bytes;
}

std::optional<FontFaceId> HarfBuzzFontEngine::register_shared_typeface(
    std::optional<FontRole> role, std::uint16_t weight, bool italic,
    std::shared_ptr<const std::vector<std::byte>> encoded, std::uint32_t face_index) {
    if (!encoded) return std::nullopt;
    const std::span<const std::byte> bytes(*encoded);
    const std::optional<FontFaceId> result =
        register_owned_typeface(role, weight, italic, bytes, std::move(encoded), face_index);
    return result;
}

std::optional<FontFaceId> HarfBuzzFontEngine::register_owned_typeface(
    std::optional<FontRole> role, std::uint16_t weight, bool italic,
    std::span<const std::byte> encoded, std::shared_ptr<const void> owner,
    std::uint32_t face_index) {
    const std::optional<FontFaceId> result =
        (*impl_).register_face(role, weight, italic, encoded, std::move(owner), face_index);
    return result;
}

std::optional<FontFaceId> HarfBuzzFontEngine::register_lazy_fallback_typeface(
    std::uint16_t weight, bool italic, std::span<const gui_forms::detail::CodepointRange> coverage,
    LazyFaceLoader load) {
    const std::optional<FontFaceId> id =
        (*impl_).register_lazy_face(weight, italic, coverage, std::move(load));
    return id;
}

std::optional<FontFaceId> HarfBuzzFontEngine::register_lazy_family_typeface(
    FontFamilyId family, std::uint16_t weight, bool italic, LazyFaceLoader load) {
    const std::optional<FontFaceId> id =
        (*impl_).register_lazy_family_face(family, weight, italic, std::move(load));
    return id;
}

std::optional<FontFaceId> HarfBuzzFontEngine::register_owned_family_typeface(
    FontFamilyId family, std::uint16_t weight, bool italic, std::span<const std::byte> encoded,
    std::shared_ptr<const void> owner, std::uint32_t face_index) {
    const std::optional<FontFaceId> id =
        (*impl_).register_family_face(family, weight, italic, encoded, std::move(owner), face_index);
    return id;
}

bool HarfBuzzFontEngine::set_face_language(const FontFaceId id, const TextLanguage language) noexcept {
    for (Impl::Face& face : (*impl_).faces) {
        if (face.id != id) continue;
        if (face.role || face.named_family.value != 0U) return false;
        face.language = language;
        return true;
    }
    return false;
}

std::size_t HarfBuzzFontEngine::opened_face_count() const noexcept {
    std::size_t count = 0U;
    for (const Impl::Face& face : (*impl_).faces) {
        if (face.open()) ++count;
    }
    return count;
}

std::optional<FontFaceId> HarfBuzzFontEngine::register_typeface(
    FontRole role, std::uint16_t weight, bool italic,
    std::span<const std::byte> encoded, std::uint32_t face_index) {
    if (encoded.empty() || encoded.size() > maximum_face_bytes ||
        (*impl_).faces.size() >= maximum_faces || face_index > 255U) return std::nullopt;
    std::shared_ptr<const std::vector<std::byte>> owner =
        std::make_shared<const std::vector<std::byte>>(encoded.begin(), encoded.end());
    const std::optional<FontFaceId> result =
        register_shared_typeface(role, weight, italic, std::move(owner), face_index);
    return result;
}

std::optional<FontFaceId> HarfBuzzFontEngine::register_fallback_typeface(
    std::uint16_t weight, bool italic, std::span<const std::byte> encoded,
    std::uint32_t face_index) {
    if (encoded.empty() || encoded.size() > maximum_face_bytes ||
        (*impl_).faces.size() >= maximum_faces || face_index > 255U) return std::nullopt;
    std::shared_ptr<const std::vector<std::byte>> owner =
        std::make_shared<const std::vector<std::byte>>(encoded.begin(), encoded.end());
    const std::optional<FontFaceId> result =
        register_shared_typeface(std::nullopt, weight, italic, std::move(owner), face_index);
    return result;
}

ShapedText HarfBuzzFontEngine::shape(std::string_view utf8, FontSpec font) {
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
    TextLayoutDiagnostics& counters = (*impl_).diagnostics;
    counters = TextLayoutDiagnostics{};
#endif
    ShapedText result{};
    if (utf8.empty()) {
        result.height = font.size;
        return result;
    }
    if (!validate_utf8(utf8).valid() || !valid_font_spec(font)) {
        return result;
    }
    std::vector<Impl::Face*> candidates = (*impl_).candidates(font);
    if (!(*impl_).has_primary(font.role) || candidates.empty()) {
        result.missing_primary_face = true;
        return result;
    }

#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
    TextLayoutPhaseTimer store_timer(counters, TextLayoutPhase::store_graphemes);
#endif
    TextStore store(utf8);
    struct Segment final { Impl::Face* face{}; Utf8Range range{}; };
    const std::size_t graphemes = store.grapheme_count().value();
    std::size_t maximum_cluster_bytes = 0U;
    for (std::size_t index = 0U; index < graphemes; ++index) {
        const Utf8Range range = store.grapheme_range(GraphemeIndex(index));
        const std::size_t bytes = range.end.value() - range.start.value();
        maximum_cluster_bytes = std::max(maximum_cluster_bytes, bytes);
    }
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
    store_timer.stop();
    counters.graphemes = static_cast<std::uint64_t>(graphemes);
    TextLayoutPhaseTimer fallback_timer(counters, TextLayoutPhase::fallback);
#endif
    std::vector<char32_t> scalars{};
    scalars.reserve(maximum_cluster_bytes);
    std::vector<Segment> segments{};
    // Grow only when a distinct font run is published. Most text coalesces
    // into one run; reserving one Segment per grapheme wastes input-sized space.
    for (std::size_t index = 0U; index < graphemes; ++index) {
        const Utf8Range range = store.grapheme_range(GraphemeIndex(index));
        decode_scalars(utf8, range, scalars);
        Impl::Face* selected = nullptr;
        for (Impl::Face* candidate : candidates) {
            if ((*impl_).covers(*candidate, scalars)) {
                selected = candidate;
                break;
            }
        }
        if (selected == nullptr) {
            selected = candidates.front();
            ++result.missing_clusters;
        }
        if (!segments.empty() && segments.back().face == selected &&
            segments.back().range.end == range.start) {
            segments.back().range.end = range.end;
        } else {
            segments.push_back({selected, range});
        }
    }

#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
    fallback_timer.stop();
    counters.segments = static_cast<std::uint64_t>(segments.size());
    TextLayoutPhaseTimer bidi_timer(counters, TextLayoutPhase::bidi);
#endif
    // Unicode bidi ordering is independent of font fallback. Reorder whole
    // directional runs, then shape font fragments with the resolved direction;
    // never reverse UTF-8 bytes or reorder stored application text.
    const std::vector<DirectionRun> directions = visual_direction_runs(utf8);
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
    bidi_timer.stop();
    counters.directions = static_cast<std::uint64_t>(directions.size());
    TextLayoutPhaseTimer intersection_timer(counters, TextLayoutPhase::intersections);
#endif
    struct VisualSegment final { Impl::Face* face{}; Utf8Range range{}; bool rtl{}; };
    std::vector<VisualSegment> visual{};
    std::vector<VisualSegment> parts{};
    parts.reserve(segments.size());
    for (const DirectionRun& direction : directions) {
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
        counters.intersection_pairs += counters.segments;
#endif
        parts.clear();
        for (const Segment& segment : segments) {
            const std::size_t start = std::max(segment.range.start.value(), direction.range.start.value());
            const std::size_t end = std::min(segment.range.end.value(), direction.range.end.value());
            if (start < end) { parts.push_back({segment.face, {Utf8Offset(start), Utf8Offset(end)}, direction.rtl}); }
        }
        if (direction.rtl) { std::reverse(parts.begin(), parts.end()); }
        visual.insert(visual.end(), parts.begin(), parts.end());
    }
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
    intersection_timer.stop();
    counters.visual_runs = static_cast<std::uint64_t>(visual.size());
#endif
    double origin{};
    Impl::ShapeCall call{};
    std::size_t index = 0U;
    try {
        for (; index < visual.size(); ++index) {
            const VisualSegment& segment = visual[index];
            const double before = result.width;
            (*impl_).append_run(call, result, *segment.face, utf8, segment.range, font, origin,
                              index + 1U != visual.size(), segment.rtl);
            origin += std::max(0.0, result.width - before);
        }
    } catch (const std::runtime_error&) {
        // A run an application family's face failed to shape (STXlib refused it)
        // quarantines that family, and the text is shaped again without it. Each retry
        // quarantines one more family of the font's order, so retries end; a failure in a
        // bundled face is the engine's and propagates.
        const FontFamilyId failed = Impl::family_of(*visual[index].face);
        if (!app_font_family_number(failed)) throw;
        quarantine_app_font_family(failed, AppFontQuarantine::shaping_failed);
        ShapedText retried = shape(utf8, font);
        return retried;
    }
    return result;
}

std::unique_ptr<BoundedShapedText> HarfBuzzFontEngine::shape_bounded(
    std::string_view utf8, FontSpec font, ShapeStorageLimits limits) {
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
    (*impl_).diagnostics = TextLayoutDiagnostics{};
#endif
    if (limits.input_bytes == 0 || limits.runs == 0 || limits.glyphs == 0 ||
        utf8.size() > limits.input_bytes || utf8.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("Bounded shaping input capacity exceeded");
    }
    if (!valid_font_spec(font)) throw std::invalid_argument("Invalid bounded shaping font");
    const gui_forms::detail::GraphemeStorageRequirement grapheme_requirement =
        gui_forms::detail::grapheme_storage_requirement(utf8);
    if (grapheme_requirement.status == gui_forms::detail::GraphemeStorageStatus::invalid_utf8) {
        throw std::invalid_argument("Invalid bounded shaping UTF-8");
    }
    if (grapheme_requirement.status != gui_forms::detail::GraphemeStorageStatus::success) {
        throw std::length_error("Grapheme storage arithmetic overflow");
    }
    using Segment = WorkspaceFontSegment;
    using VisualSegment = WorkspaceVisualSegment;
    using DirectionRun = WorkspaceDirectionRun;
    using FaceCandidates = std::array<Impl::Face*, maximum_faces>;
    const std::size_t scalar_capacity = grapheme_requirement.scalar_count;
    if (scalar_capacity > std::numeric_limits<std::size_t>::max() / 2U) {
        throw std::length_error("Visual intersection capacity overflow");
    }
    const std::size_t visual_capacity = scalar_capacity * 2U;
    std::size_t workspace = 0;
    account_array(1, sizeof(FaceCandidates), limits.workspace_bytes, workspace);
    account_array(1, sizeof(Impl::ShapeCall), limits.workspace_bytes, workspace);
    const std::size_t fixed_workspace = workspace;
    account_array(grapheme_requirement.boundary_capacity, sizeof(std::size_t), limits.workspace_bytes, workspace);
    account_array(scalar_capacity, sizeof(char32_t), limits.workspace_bytes, workspace);
    account_array(scalar_capacity, sizeof(Segment), limits.workspace_bytes, workspace);
    account_array(scalar_capacity, sizeof(DirectionRun), limits.workspace_bytes, workspace);
    account_array(visual_capacity, sizeof(VisualSegment), limits.workspace_bytes, workspace);
    std::size_t segmentation_peak = fixed_workspace;
    account_array(grapheme_requirement.peak_bytes, 1, limits.workspace_bytes, segmentation_peak);
    const std::size_t workspace_peak = std::max(workspace, segmentation_peak);
    std::size_t output_bytes = sizeof(BoundedShapedText);
    account_array(limits.runs, sizeof(BoundedFontRun), limits.output_bytes, output_bytes);
    account_array(limits.glyphs, sizeof(ShapedGlyph), limits.output_bytes, output_bytes);

    std::unique_ptr<BoundedShapedText> owner = std::make_unique<BoundedShapedText>();
    BoundedShapedText& output = *owner;
    output.runs = std::make_unique<BoundedFontRun[]>(limits.runs);
    output.glyphs = std::make_unique<ShapedGlyph[]>(limits.glyphs);
    output.run_capacity = limits.runs;
    output.glyph_capacity = limits.glyphs;
    output.controlled_output_bytes = output_bytes;
    output.controlled_workspace_peak = workspace_peak;
    if (utf8.empty()) { output.height = font.size; return owner; }

    FaceCandidates candidates{};
    const std::size_t candidate_count = (*impl_).bounded_candidates(font, candidates);
    if (!(*impl_).has_primary(font.role) || candidate_count == 0) {
        output.missing_primary_face = true;
        return owner;
    }
    gui_forms::detail::GraphemeBoundaryBuffer boundaries{};
    const gui_forms::detail::GraphemeStorageStatus segmented =
        gui_forms::detail::bounded_grapheme_boundaries(utf8,
            limits.workspace_bytes - fixed_workspace, boundaries);
    if (segmented == gui_forms::detail::GraphemeStorageStatus::resource_failure) throw std::bad_alloc();
    if (segmented != gui_forms::detail::GraphemeStorageStatus::success) {
        throw std::length_error("Bounded grapheme preparation failed");
    }
    std::unique_ptr<char32_t[]> scalars = std::make_unique<char32_t[]>(scalar_capacity);
    std::unique_ptr<Segment[]> segments = std::make_unique<Segment[]>(scalar_capacity);
    std::unique_ptr<DirectionRun[]> directions = std::make_unique<DirectionRun[]>(scalar_capacity);
    std::unique_ptr<VisualSegment[]> visual = std::make_unique<VisualSegment[]>(visual_capacity);
    const std::span<const std::size_t> edges = boundaries.boundaries();
    const std::span<char32_t> scalar_scratch(scalars.get(), scalar_capacity);
    const std::span<WorkspaceFontSegment> segment_scratch(segments.get(), scalar_capacity);
    const std::span<WorkspaceDirectionRun> direction_scratch(directions.get(), scalar_capacity);
    const std::span<WorkspaceVisualSegment> visual_scratch(visual.get(), visual_capacity);
    const std::span<Impl::Face* const> selected_faces(candidates.data(), candidate_count);
    Impl::ShapeCall call{};
    TextAnalysis analysis{};
    (*impl_).fill_bounded_runs(utf8, font, call, analysis, selected_faces, edges, scalar_scratch,
        segment_scratch, direction_scratch, visual_scratch, output);
    return owner;
}

void HarfBuzzFontEngine::prepare_workspace(BoundedShapeWorkspace& workspace,
    const ShapeStorageLimits& limits) {
    const std::size_t external_bytes = (*impl_).workspace_external_bytes(limits.workspace_bytes);
    workspace.prepare(limits, external_bytes);
}

std::unique_ptr<BoundedShapedText> HarfBuzzFontEngine::shape_with_workspace(
    const std::string_view utf8, const FontSpec font, const ShapeStorageLimits& limits,
    BoundedShapeWorkspace& workspace) {
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
    (*impl_).diagnostics = TextLayoutDiagnostics{};
#endif
    const ShapeStorageLimits ceiling{};
    if (limits.input_bytes == 0U || limits.input_bytes > ceiling.input_bytes ||
        (!utf8.empty() && (limits.runs == 0U || limits.glyphs == 0U)) ||
        limits.runs > ceiling.runs || limits.glyphs > ceiling.glyphs ||
        limits.output_bytes > ceiling.output_bytes || limits.workspace_bytes > ceiling.workspace_bytes ||
        utf8.size() > limits.input_bytes || limits.input_bytes > workspace.scalar_capacity_ ||
        limits.runs > workspace.staging_.run_capacity || limits.glyphs > workspace.staging_.glyph_capacity) {
        throw std::length_error("Reusable shaping input or prepared capacity exceeded");
    }
    if (!valid_font_spec(font)) throw std::invalid_argument("Invalid reusable shaping font");
    const Utf8ValidationResult validation = validate_utf8(utf8);
    if (!validation.valid()) throw std::invalid_argument("Invalid reusable shaping UTF-8");
    std::size_t peak = (*impl_).workspace_external_bytes(limits.workspace_bytes);
    const std::size_t retained = workspace.controlled_bytes();
    account_array(retained, 1U, limits.workspace_bytes, peak);
    workspace.clear_result();
    BoundedShapedText& staged = workspace.staging_;
    if (utf8.empty()) {
        staged.height = font.size;
    } else {
        std::array<Impl::Face*, maximum_faces> candidates{};
        const std::size_t candidate_count = (*impl_).bounded_candidates(font, candidates);
        if (!(*impl_).has_primary(font.role) || candidate_count == 0U) {
            staged.missing_primary_face = true;
        } else {
            const detail::GraphemeStorageStatus segmented = workspace.graphemes_.fill(utf8);
            if (segmented != detail::GraphemeStorageStatus::success) {
                throw std::length_error("Reusable grapheme capacity exceeded");
            }
            const std::span<const std::size_t> edges = workspace.graphemes_.boundaries();
            const std::span<char32_t> scalars(workspace.scalars_.get(), validation.scalar_count);
            const std::span<WorkspaceFontSegment> segments(workspace.segments_.get(), validation.scalar_count);
            const std::span<WorkspaceDirectionRun> directions(workspace.directions_.get(), validation.scalar_count);
            const std::size_t visual_count = validation.scalar_count * 2U;
            const std::span<WorkspaceVisualSegment> visual(workspace.visual_.get(), visual_count);
            const std::span<Impl::Face* const> selected(candidates.data(), candidate_count);
            // Per-call limits may be smaller than prepared capacities. Restore
            // capacities even when native shaping or a resource check throws.
            const std::size_t run_capacity = staged.run_capacity;
            const std::size_t glyph_capacity = staged.glyph_capacity;
            staged.run_capacity = limits.runs;
            staged.glyph_capacity = limits.glyphs;
            try {
                Impl::ShapeCall call{};
                TextAnalysis analysis{};
                (*impl_).fill_bounded_runs(utf8, font, call, analysis, selected, edges, scalars, segments, directions,
                                           visual, staged);
            } catch (...) {
                staged.run_capacity = run_capacity;
                staged.glyph_capacity = glyph_capacity;
                workspace.clear_result();
                throw;
            }
            staged.run_capacity = run_capacity;
            staged.glyph_capacity = glyph_capacity;
        }
    }
    std::unique_ptr<BoundedShapedText> result = workspace.copy_result(limits.output_bytes, peak);
    return result;
}

std::unique_ptr<ReusableShapeWorkspace> HarfBuzzFontEngine::create_reusable_workspace() const {
    std::unique_ptr<ReusableShapeWorkspace> workspace = std::make_unique<ReusableShapeWorkspace>((*impl_).identity);
    return workspace;
}

GlyphSource HarfBuzzFontEngine::glyph_source() const noexcept {
    GlyphSource source{};
    source.engine = (*impl_).identity;
    source.generation = static_cast<std::uint64_t>((*impl_).faces.size());
    return source;
}

namespace {

// Defined with the line metrics below.
PrimaryLineMetrics sized_line_metrics(const stxlib::glyphs::Font& font, FontFaceId id, FontSpec font_spec);

// Lends a reusable workspace's shaping scratch to one shape call and takes
// it back when the call ends, also when shaping throws. Moving a scratch
// moves its storage: nothing is allocated.
class ScratchLoan final {
public:
    ScratchLoan(ShapeScratch& home, ShapeScratch& borrower) noexcept
        : home_(home), borrower_(borrower) {
        borrower_ = std::move(home_);
    }
    ~ScratchLoan() { home_ = std::move(borrower_); }
    ScratchLoan(const ScratchLoan&) = delete;
    ScratchLoan& operator=(const ScratchLoan&) = delete;

private:
    ShapeScratch& home_;
    ShapeScratch& borrower_;
};

[[nodiscard]] TextExtent refused_extent() noexcept {
    TextExtent refused{};
    refused.status = TextResolutionStatus::invalid_request;
    return refused;
}

} // namespace

TextExtent HarfBuzzFontEngine::shape_in_workspace(const std::string_view utf8, const FontSpec font,
                                                  ReusableShapeWorkspace& workspace) {
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
    (*impl_).diagnostics = TextLayoutDiagnostics{};
#endif
    workspace.forget_shaping();
    // Checks, before anything is shaped.
    if (workspace.engine_ != (*impl_).identity || utf8.size() > workspace.capacity_ || !valid_font_spec(font) ||
        utf8.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return refused_extent();
    }
    const Utf8ValidationResult validation = validate_utf8(utf8);
    if (!validation.valid()) return refused_extent();
    std::array<Impl::Face*, maximum_faces> candidates{};
    const std::size_t candidate_count = (*impl_).fill_candidates(font, candidates);
    const std::uint64_t generation = static_cast<std::uint64_t>((*impl_).faces.size());
    TextExtent extent{};
    // As resolve(): without a primary face nothing is measured.
    if (!(*impl_).has_primary(font.role) || candidate_count == 0U) {
        extent.status = TextResolutionStatus::missing_primary_face;
        workspace.keep_shaping(utf8.size(), generation);
        return extent;
    }
    StagedShaping& staged = workspace.staged_;
    if (utf8.empty()) {
        staged.height = font.size;
    } else {
        const detail::GraphemeStorageStatus segmented = workspace.graphemes_.fill(utf8);
        if (segmented != detail::GraphemeStorageStatus::success) return refused_extent();
        const std::span<const std::size_t> edges = workspace.graphemes_.boundaries();
        const std::size_t scalar_count = validation.scalar_count;
        const std::span<char32_t> scalars(workspace.scalars_.data(), scalar_count);
        const std::span<WorkspaceFontSegment> segments(workspace.segments_.data(), scalar_count);
        const std::span<WorkspaceDirectionRun> directions(workspace.directions_.data(), scalar_count);
        const std::span<WorkspaceVisualSegment> visual(workspace.visual_.data(), scalar_count * 2U);
        const std::span<Impl::Face* const> selected(candidates.data(), candidate_count);
        Impl::ShapeCall call{};
        const ScratchLoan loan(workspace.scratch_, call.scratch);
        try {
            (*impl_).fill_bounded_runs(utf8, font, call, workspace.analysis_, selected, edges, scalars, segments,
                                       directions, visual, staged);
        } catch (const std::length_error&) {
            // More runs or glyphs than the workspace stages.
            workspace.forget_shaping();
            return refused_extent();
        } catch (...) {
            workspace.forget_shaping();
            throw;
        }
    }
    const Impl::Face& primary = *candidates[0];
    extent.logical_size = {staged.width, staged.height};
    extent.ascent = staged.ascent;
    extent.descent = staged.descent;
    extent.line_gap = sized_line_metrics(primary.font(), primary.id, font).line_gap_device;
    extent.status = staged.missing_primary_face
        ? TextResolutionStatus::missing_primary_face
        : staged.missing_clusters != 0U
            ? TextResolutionStatus::missing_cluster_coverage
            : TextResolutionStatus::exact;
    workspace.keep_shaping(utf8.size(), generation);
    return extent;
}

std::size_t HarfBuzzFontEngine::face_count() const noexcept {
    const std::size_t count = (*impl_).faces.size();
    return count;
}

std::optional<FontFaceSource> HarfBuzzFontEngine::face_source(const FontFaceId id) const {
    for (const Impl::Face& face : (*impl_).faces) {
        if (face.id != id) continue;
        if (!face.open()) return std::nullopt;
        FontFaceSource source{};
        source.bytes = face.bytes;
        source.owner = face.encoded;
        source.face_index = face.face_index;
        source.family = face.named_family;
        return source;
    }
    return std::nullopt;
}

#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
TextLayoutDiagnostics HarfBuzzFontEngine::diagnostics() const noexcept {
    const TextLayoutDiagnostics result = (*impl_).diagnostics;
    return result;
}
void HarfBuzzFontEngine::set_diagnostic_failure(TextLayoutFailure failure, std::uint64_t run) {
    if (failure != TextLayoutFailure::none && run == 0) {
        throw std::invalid_argument("Diagnostic failure run must be nonzero");
    }
    (*impl_).failure = failure;
    (*impl_).failure_run = run;
}
#endif

namespace {

// The one line-height rule's inputs for a face at `font_spec`'s size: its
// ascender, descender and line gap from the face's sized line metrics
// (sized_line_metrics_of: FreeType's, ascender rounded up, descender down,
// line height to nearest). The gap is what the rounded line height adds to
// the rounded ascender and descender, never negative. Prepared text and
// resolve() (TextBox's line box) both read it, so they agree for faces with
// a line gap.
PrimaryLineMetrics sized_line_metrics(const stxlib::glyphs::Font& font, FontFaceId id, const FontSpec font_spec) {
    const SizedLineMetrics metrics = sized_line_metrics_of(font, shaping_size_of(font_spec));
    PrimaryLineMetrics result{};
    result.face = id;
    result.ascent_device = static_cast<double>(metrics.ascender) / 64.0;
    result.descent_device = -static_cast<double>(metrics.descender) / 64.0;
    const double native_height = static_cast<double>(metrics.height) / 64.0;
    const double gap = native_height - result.ascent_device - result.descent_device;
    result.line_gap_device = std::max(0.0, gap);
    return result;
}

} // namespace

std::optional<PrimaryLineMetrics> HarfBuzzFontEngine::primary_line_metrics(const FontSpec font) {
    if (!valid_font_spec(font)) throw std::invalid_argument("Invalid primary line font");
    // The first family of the font's order with an openable face: its face nearest the weight
    // and slant (stage F decision 7), opened if it has not been. Otherwise the role's exact face.
    const FontFamilyOrder order = font_family_order(font);
    for (std::size_t index = 0U; index < order.count; ++index) {
        Impl::Face* nearest = nullptr;
        for (Impl::Face& face : (*impl_).faces) {
            if (!(Impl::family_of(face) == order.families[index])) continue;
            if (!face.open() && (!face.loader || !(*impl_).open_lazy(face))) continue;
            if (nearest == nullptr ||
                font_face_nearer(face.weight, face.italic, (*nearest).weight, (*nearest).italic, font.weight,
                                 font.italic)) {
                nearest = &face;
            }
        }
        if (nearest == nullptr) continue;
        const PrimaryLineMetrics result = sized_line_metrics((*nearest).font(), (*nearest).id, font);
        return result;
    }
    for (Impl::Face& face : (*impl_).faces) {
        if (!face.role || *face.role != font.role || face.weight != font.weight || face.italic != font.italic) continue;
        if (!face.open()) continue;
        const PrimaryLineMetrics result = sized_line_metrics(face.font(), face.id, font);
        return result;
    }
    return std::nullopt;
}

namespace {

// The face's design metrics at `font_spec`'s size: the font's ascender,
// descender and height (STXlib picks them by FreeType's rule: OS/2's
// typographic values when the font says to use them, else hhea's, else
// OS/2's) and OS/2's x-height (0 before version 2), in font units, scaled by
// size over units per em, unrounded. The gap is what the height adds to the
// ascender and descender, never negative.
FontVerticalMetrics design_line_metrics(const stxlib::glyphs::Font& font, const FontSpec font_spec) {
    const double units_per_em = static_cast<double>(font.units_per_em);
    FontVerticalMetrics result{};
    if (units_per_em <= 0.0) return result;
    const double scale = std::clamp(font_spec.size, 1.0, 4096.0) / units_per_em;
    const double ascent = static_cast<double>(font.ascender);
    const double descent = -static_cast<double>(font.descender);
    const double height = static_cast<double>(font.height);
    result.ascent = ascent * scale;
    result.descent = descent * scale;
    result.line_gap = std::max(0.0, height - ascent - descent) * scale;
    result.x_height = std::max(0.0, static_cast<double>(font.x_height) * scale);
    return result;
}

// The opportunity a stxlib::textlayout decision names (compared by name:
// the two enumerations need not share values).
[[nodiscard]] LineBreakOpportunity line_break_opportunity(const stxlib::textlayout::LineBreak decision) noexcept {
    switch (decision) {
    case stxlib::textlayout::LineBreak::mandatory: return LineBreakOpportunity::mandatory;
    case stxlib::textlayout::LineBreak::allowed: return LineBreakOpportunity::allowed;
    case stxlib::textlayout::LineBreak::inside_character: return LineBreakOpportunity::inside_character;
    case stxlib::textlayout::LineBreak::prohibited:
    default: return LineBreakOpportunity::prohibited;
    }
}

} // namespace

TextResolutionStatus HarfBuzzFontEngine::fill_line_break_opportunities_utf8(
    std::string_view utf8, std::span<LineBreakOpportunity> opportunities) noexcept {
    if (opportunities.size() != utf8.size()) return TextResolutionStatus::invalid_request;
    const stxlib::Result<stxlib::TextView> validated = stxlib::validate_utf8(utf8.data(), utf8.size());
    const stxlib::TextView* const text = validated.value();
    if (text == nullptr) return TextResolutionStatus::invalid_request;
    if (utf8.empty()) return TextResolutionStatus::exact;
    // The caller's storage holds the library's decisions during the call
    // (no allocation): its bytes are reused as LineBreak objects, which
    // find_line_breaks writes, and each is then read and replaced by the
    // LineBreakOpportunity it names. Both are one byte, trivially
    // destructible, so the reuse ends no lifetime that needed ending; the
    // replacements transparently replace the caller's objects.
    using Decision = stxlib::textlayout::LineBreak;
    static_assert(sizeof(Decision) == sizeof(LineBreakOpportunity));
    static_assert(alignof(Decision) == alignof(LineBreakOpportunity));
    static_assert(std::is_trivially_destructible_v<Decision>);
    static_assert(std::is_trivially_destructible_v<LineBreakOpportunity>);
    const std::size_t size = utf8.size();
    std::byte* const storage = reinterpret_cast<std::byte*>(opportunities.data());
    Decision* const decisions = ::new (static_cast<void*>(storage)) Decision[size];
    const stxlib::MutableView<Decision> output(decisions, size);
    // The whole text is one piece, as libunibreak read it: its end is
    // mandatory (LB3), so no forcing is needed. CJ is NS, which is what
    // libunibreak's "-strict" gave.
    const stxlib::Status found = stxlib::textlayout::find_line_breaks(output, *text);
    // find_line_breaks fails only when the counts differ, which they do not;
    // were it to fail, nothing it wrote is read and every entry is
    // prohibited.
    const bool written = !found.failed();
    for (std::size_t index = 0U; index < size; ++index) {
        Decision decision = Decision::prohibited;
        if (written) decision = decisions[index];
        const LineBreakOpportunity opportunity = line_break_opportunity(decision);
        ::new (static_cast<void*>(storage + index)) LineBreakOpportunity(opportunity);
    }
    if (!written) return TextResolutionStatus::invalid_request;
    return TextResolutionStatus::exact;
}

TextResolutionStatus HarfBuzzFontEngine::fill_cluster_advances_utf8(
    std::string_view utf8, FontSpec font, std::span<double> advances) {
    if (advances.size() != utf8.size() || !validate_utf8(utf8).valid() || !valid_font_spec(font)) {
        return TextResolutionStatus::invalid_request;
    }
    std::fill(advances.begin(), advances.end(), 0.0);
    // Without a primary face resolve() measures nothing, so every advance
    // stays zero, matching its zero width.
    const std::vector<Impl::Face*> candidates = (*impl_).candidates(font);
    if (!(*impl_).has_primary(font.role) || candidates.empty()) return TextResolutionStatus::missing_primary_face;
    if (utf8.empty()) return TextResolutionStatus::exact;
    const ShapedText shaped = shape(utf8, font);
    // A glyph's cluster is the byte offset in `utf8` of its cluster's first
    // byte (shape adds the whole text to each run's buffer), in either
    // direction; the clusters' order does not matter to a sum.
    const std::size_t size = utf8.size();
    for (const ShapedFontRun& run : shaped.runs) {
        for (const ShapedGlyph& glyph : run.glyphs) {
            const std::size_t cluster = glyph.cluster.value();
            if (cluster >= size) continue;
            advances[cluster] += static_cast<double>(glyph.advance_x);
        }
    }
    const TextResolutionStatus status = shaped.missing_primary_face
        ? TextResolutionStatus::missing_primary_face
        : shaped.missing_clusters != 0U
            ? TextResolutionStatus::missing_cluster_coverage
            : TextResolutionStatus::exact;
    return status;
}

ResolvedTextLayout HarfBuzzFontEngine::resolve(std::string_view utf8,
                                               FontSpec font) {
    ResolvedTextLayout result{};
    result.effective_font = font;
    if (!validate_utf8(utf8).valid() || !valid_font_spec(font)) {
        result.status = TextResolutionStatus::invalid_request;
        return result;
    }
    const std::vector<Impl::Face*> candidates = (*impl_).candidates(font);
    if (!(*impl_).has_primary(font.role) || candidates.empty()) {
        result.status = TextResolutionStatus::missing_primary_face;
        return result;
    }
    result.primary_family = (*candidates.front()).family;
    const FontFamilyOrder order = font_family_order(font);
    const ShapedText shaped = shape(utf8, font);
    result.logical_size = {shaped.width, shaped.height};
    result.ascent = shaped.ascent;
    result.descent = shaped.descent;
    // The line gap is the preferred primary face's, by the rule prepared
    // text uses (sized_line_metrics); the shaped height carries none.
    const Impl::Face& primary = *candidates.front();
    result.line_gap = sized_line_metrics(primary.font(), primary.id, font).line_gap_device;
    result.design_metrics = design_line_metrics(primary.font(), font);
    result.missing_clusters = shaped.missing_clusters;
    result.status = shaped.missing_primary_face
        ? TextResolutionStatus::missing_primary_face
        : shaped.missing_clusters != 0U
            ? TextResolutionStatus::missing_cluster_coverage
            : TextResolutionStatus::exact;
    result.runs.reserve(shaped.runs.size());
    for (const ShapedFontRun& run : shaped.runs) {
        const Impl::Face* resolved = nullptr;
        for (const Impl::Face& face : (*impl_).faces) {
            if (face.id == run.face) {
                resolved = &face;
                break;
            }
        }
        if (resolved == nullptr) continue;
        const bool family_face = font_family_position(order, Impl::family_of(*resolved)).has_value();
        result.runs.push_back({
            run.source_range.start.value(),
            run.source_range.end.value() - run.source_range.start.value(),
            (*resolved).family, (*resolved).weight, (*resolved).italic,
            !family_face && ((*resolved).fallback || !(*resolved).role ||
                *(*resolved).role != font.role)});
    }
    return result;
}

} // namespace gui_forms::render::text
