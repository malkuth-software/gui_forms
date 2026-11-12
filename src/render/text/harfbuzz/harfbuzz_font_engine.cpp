#include "harfbuzz_font_engine.hpp"
#include "bounded_shape_workspace.hpp"
#include "reusable_shape_workspace.hpp"
#include "../../../core/text/unicode/unicode_grapheme.hpp"
#include "../../../core/types/restrict/restrict.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include FT_TRUETYPE_TABLES_H
#include <hb-ft.h>
#include <hb.h>
#include <SheenBidi/SheenBidi.h>
#include <linebreak.h>
#include <stx_text_allocation.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace gui_forms::render::text {
namespace {

constexpr std::size_t maximum_face_bytes = 64U * 1024U * 1024U;
constexpr std::size_t maximum_faces = 64U;
constexpr FT_Long maximum_glyphs = 1'000'000L;

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

struct BidiOwner final {
    SBAlgorithmRef algorithm = nullptr;
    SBParagraphRef paragraph = nullptr;
    SBLineRef line = nullptr;
    BidiOwner() = default;
    BidiOwner(const BidiOwner&) = delete;
    BidiOwner& operator=(const BidiOwner&) = delete;
    ~BidiOwner() {
        if (line) { SBLineRelease(line); }
        if (paragraph) { SBParagraphRelease(paragraph); }
        if (algorithm) { SBAlgorithmRelease(algorithm); }
    }
    void clear_line() {
        if (line) { SBLineRelease(line); line = nullptr; }
        if (paragraph) { SBParagraphRelease(paragraph); paragraph = nullptr; }
    }
};
struct DirectionRun final { Utf8Range range{}; bool rtl{}; };
std::vector<DirectionRun> visual_direction_runs(std::string_view utf8) {
    std::vector<DirectionRun> runs{};
    const SBCodepointSequence sequence{SBStringEncodingUTF8, utf8.data(), utf8.size()};
    BidiOwner owner{};
    owner.algorithm = SBAlgorithmCreate(&sequence);
    if (!owner.algorithm) { throw std::bad_alloc(); }
    std::size_t offset = 0;
    while (offset < utf8.size()) {
        owner.paragraph = SBAlgorithmCreateParagraph(owner.algorithm, offset, utf8.size() - offset, SBLevelDefaultLTR);
        if (!owner.paragraph) { throw std::bad_alloc(); }
        const std::size_t length = SBParagraphGetLength(owner.paragraph);
        if (length == 0 || length > utf8.size() - offset) { throw std::runtime_error("Invalid bidi paragraph length"); }
        owner.line = SBParagraphCreateLine(owner.paragraph, offset, length);
        if (!owner.line) { throw std::bad_alloc(); }
        const SBRun* values = SBLineGetRunsPtr(owner.line);
        const std::size_t count = SBLineGetRunCount(owner.line);
        for (std::size_t i = 0; i < count; ++i) {
            runs.push_back({{Utf8Offset(values[i].offset), Utf8Offset(values[i].offset + values[i].length)}, (values[i].level & 1) != 0});
        }
        offset += length;
        owner.clear_line();
    }
    return runs;
}

// The engine's FreeType heap calls on this thread (allocations and
// reallocations, not frees): HarfBuzzFontEngine::thread_native_allocations.
thread_local std::uint64_t freetype_allocations = 0U;

// The engine's FreeType memory: the C heap, as FreeType's default, with each
// allocating call counted.
void* counted_freetype_allocate(FT_Memory, long size) {
    ++freetype_allocations;
    void* const block = std::malloc(static_cast<std::size_t>(size));
    return block;
}

void counted_freetype_release(FT_Memory, void* block) {
    std::free(block);
}

void* counted_freetype_reallocate(FT_Memory, long, long new_size, void* block) {
    ++freetype_allocations;
    void* const moved = std::realloc(block, static_cast<std::size_t>(new_size));
    return moved;
}

// The callbacks take their types from FreeType's declarations.
static_assert(std::is_same_v<decltype(&counted_freetype_allocate), FT_Alloc_Func>);
static_assert(std::is_same_v<decltype(&counted_freetype_release), FT_Free_Func>);
static_assert(std::is_same_v<decltype(&counted_freetype_reallocate), FT_Realloc_Func>);

// A FreeType library over the counted memory, set up as FT_Init_FreeType
// sets one up (default modules, then properties from FREETYPE_PROPERTIES).
// Not movable: the library keeps the address of `memory`.
struct LibraryOwner final {
    FT_MemoryRec_ memory{};
    FT_Library value{};
    LibraryOwner() {
        memory.user = nullptr;
        memory.alloc = counted_freetype_allocate;
        memory.free = counted_freetype_release;
        memory.realloc = counted_freetype_reallocate;
        const FT_Error initialized = FT_New_Library(&memory, &value);
        if (initialized != 0 || value == nullptr) {
            throw std::runtime_error("FreeType initialization failed");
        }
        FT_Add_Default_Modules(value);
        FT_Set_Default_Properties(value);
    }
    LibraryOwner(const LibraryOwner&) = delete;
    LibraryOwner& operator=(const LibraryOwner&) = delete;
    ~LibraryOwner() {
        if (value != nullptr) FT_Done_Library(value);
    }
};

// A process-unique identity for each engine (GlyphSource::engine): never
// zero, never reused.
[[nodiscard]] std::uint64_t next_engine_identity() noexcept {
    static std::atomic<std::uint64_t> next{1U};
    const std::uint64_t identity = next.fetch_add(1U, std::memory_order_relaxed);
    return identity;
}

// Installs a SheenBidi arena for one bidi analysis (none: the C heap) and
// restores the one installed before. Declared before the BidiOwner whose
// objects it serves, so that they are released while it is installed.
class SheenBidiArenaScope final {
public:
    explicit SheenBidiArenaScope(StxSheenBidiArena* arena) noexcept : active_(arena != nullptr) {
        if (active_) previous_ = stx_sheenbidi_use_arena(arena);
    }
    ~SheenBidiArenaScope() {
        if (active_) static_cast<void>(stx_sheenbidi_use_arena(previous_));
    }
    SheenBidiArenaScope(const SheenBidiArenaScope&) = delete;
    SheenBidiArenaScope& operator=(const SheenBidiArenaScope&) = delete;

private:
    StxSheenBidiArena* previous_{};
    bool active_{};
};

struct ReleaseFace final {
    void operator()(FT_Face face) const noexcept {
        if (face != nullptr) FT_Done_Face(face);
    }
};
using FreeTypeFaceOwner = std::unique_ptr<FT_FaceRec, ReleaseFace>;
using HarfBuzzFontOwner = std::unique_ptr<hb_font_t, void (*)(hb_font_t*)>;
using HarfBuzzBufferOwner = std::unique_ptr<hb_buffer_t, void (*)(hb_buffer_t*)>;

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

// A face's nominal glyph map, read from its open native face.
struct NativeGlyphMap final {
    FT_Face face{};
    [[nodiscard]] bool operator()(char32_t scalar) const {
        const bool mapped = FT_Get_Char_Index(face, static_cast<FT_ULong>(scalar)) != 0U;
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

// Whether a face whose nominal glyph map is `mapped` covers the cluster `scalars`.
template <typename GlyphMap>
[[nodiscard]] bool cluster_covered(const GlyphMap& mapped, std::span<const char32_t> scalars) {
    bool nominal = true;
    for (const char32_t scalar : scalars) {
        if (ignorable_for_coverage(scalar)) continue;
        if (!mapped(scalar)) {
            nominal = false;
            break;
        }
    }
    if (nominal) { return true; }
    // HarfBuzz can canonically compose a two-scalar cluster even when the
    // face has no nominal glyph for its combining mark. Keep original text
    // and offsets; this is only a bounded same-face coverage check.
    if (scalars.size() != 2) { return false; }
    const hb_codepoint_t first = static_cast<hb_codepoint_t>(scalars[0]);
    const hb_codepoint_t second = static_cast<hb_codepoint_t>(scalars[1]);
    hb_codepoint_t composed{};
    hb_unicode_funcs_t* unicode = hb_unicode_funcs_get_default();
    const hb_bool_t combined = hb_unicode_compose(unicode, first, second, &composed);
    if (!combined) { return false; }
    const bool covered = mapped(static_cast<char32_t>(composed));
    return covered;
}

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
        FT_Face face{};
        // The HarfBuzz font shaping this face, kept across calls so HarfBuzz
        // keeps the face's parsed tables and shape plans. It references face;
        // its scale is the one set at shaping_size.
        HarfBuzzFontOwner shaping_font{nullptr, hb_font_destroy};
        FT_F26Dot6 shaping_size{};
        // A lazy face: face stays null until coverage first covers a needed cluster, when
        // loader runs and is then released. A lazy face with neither face nor loader could
        // not be opened and covers nothing.
        std::span<const gui_forms::detail::CodepointRange> coverage{};
        LazyFaceLoader loader{};
        // The numbered bundled family (FontSpec::family) this face belongs to, or zero for a
        // role or fallback face. A family face serves only text that names its family.
        std::uint8_t named_family{};

        Face(FontFaceId id_value, std::optional<FontRole> role_value,
             std::uint16_t weight_value, bool italic_value,
             bool fallback_value, std::string family_value,
             std::shared_ptr<const void> bytes, FT_Face face_value)
            : id(id_value), role(role_value), weight(weight_value),
              italic(italic_value), fallback(fallback_value),
              family(std::move(family_value)), encoded(std::move(bytes)),
              face(face_value) {}
        ~Face() {
            shaping_font.reset();
            if (face != nullptr) FT_Done_Face(face);
        }
        Face(const Face&) = delete;
        Face& operator=(const Face&) = delete;
        Face(Face&& other) noexcept
            : id(other.id), role(other.role), weight(other.weight),
              italic(other.italic), fallback(other.fallback),
              family(std::move(other.family)), encoded(std::move(other.encoded)),
              face(std::exchange(other.face, nullptr)), shaping_font(std::move(other.shaping_font)),
              shaping_size(other.shaping_size), coverage(other.coverage),
              loader(std::move(other.loader)), named_family(other.named_family) {}
        Face& operator=(Face&&) = delete;
    };

    struct FacePreference final {
        FontSpec font{};

        [[nodiscard]] int tier(const Face& face) const noexcept {
            if (face.named_family != 0U && face.named_family == font.family.value) return -1;
            if (face.role && *face.role == font.role) return 0;
            if (face.role && *face.role == FontRole::content) return 1;
            return 2;
        }

        [[nodiscard]] bool operator()(const Face* left,
                                      const Face* right) const noexcept {
            const int left_tier = tier(*left);
            const int right_tier = tier(*right);
            if (left_tier != right_tier) {
                const bool preferred = left_tier < right_tier;
                return preferred;
            }
            const int left_italic = (*left).italic == font.italic ? 0 : 1;
            const int right_italic = (*right).italic == font.italic ? 0 : 1;
            if (left_italic != right_italic) {
                const bool preferred = left_italic < right_italic;
                return preferred;
            }
            const int left_distance = std::abs(static_cast<int>((*left).weight) - font.weight);
            const int right_distance = std::abs(static_cast<int>((*right).weight) - font.weight);
            const bool preferred = left_distance < right_distance;
            return preferred;
        }
    };

    LibraryOwner library{};
    std::vector<Face> faces{};
    std::uint64_t next_face_id{1U};
    std::size_t bounded_face_limit{};
    // This engine's GlyphSource::engine.
    std::uint64_t identity{next_engine_identity()};

    [[nodiscard]] std::vector<Face*> candidates(FontSpec font) {
        std::vector<Face*> result{};
        result.reserve(faces.size());
        for (Face& face : faces) {
            // A family face takes part only when the font names its family, and only once it
            // is open, so the front candidate always has a face; one that cannot be loaded
            // leaves the role's faces in charge.
            if (face.named_family != 0U) {
                if (face.named_family != font.family.value) continue;
                if (face.face == nullptr && (!face.loader || !open_lazy(face))) continue;
                result.push_back(&face);
                continue;
            }
            // The body face is the house text fallback for control and
            // monospace roles. This keeps Portsmouth as the preferred control
            // voice without turning punctuation, arrows, or localized text
            // it does not own into missing-glyph boxes. Global CJK/emoji faces
            // remain the last tier.
            if (!face.role || *face.role == font.role ||
                (font.role != FontRole::content &&
                 *face.role == FontRole::content)) {
                result.push_back(&face);
            }
        }
        std::stable_sort(result.begin(), result.end(), FacePreference{font});
        return result;
    }

    // candidates()'s answer written into `output` instead of a new vector:
    // the same faces in the same order (FacePreference, stable, by
    // insertion), opening a family face the first time text names its
    // family as candidates() does. Returns the count; std::length_error when
    // `output` is too short.
    [[nodiscard]] std::size_t fill_candidates(FontSpec font, std::span<Face*> output) {
        std::size_t count = 0U;
        const FacePreference preference{font};
        for (Face& face : faces) {
            bool candidate = false;
            if (face.named_family != 0U) {
                if (face.named_family != font.family.value) continue;
                if (face.face == nullptr && (!face.loader || !open_lazy(face))) continue;
                candidate = true;
            } else {
                candidate = !face.role || *face.role == font.role ||
                            (font.role != FontRole::content && *face.role == FontRole::content);
            }
            if (!candidate) continue;
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
    [[nodiscard]] bool covers(Face& face, std::span<const char32_t> scalars) {
        if (face.face == nullptr) {
            if (!face.loader) return false;
            if (!cluster_covered(TableGlyphMap{face.coverage}, scalars)) return false;
            if (!open_lazy(face)) return false;
        }
        const bool covered = cluster_covered(NativeGlyphMap{face.face}, scalars);
        return covered;
    }

    // Opens `bytes` as a face the engine can shape with, or returns null.
    [[nodiscard]] FreeTypeFaceOwner open_native(std::span<const std::byte> bytes,
                                                std::uint32_t face_index) const {
        FT_Face native{};
        const FT_Error opened = FT_New_Memory_Face(
            library.value, reinterpret_cast<const FT_Byte*>(bytes.data()),
            static_cast<FT_Long>(bytes.size()), static_cast<FT_Long>(face_index),
            &native);
        FreeTypeFaceOwner owner{native};
        if (opened != 0 || native == nullptr || (*native).num_glyphs <= 0 ||
            (*native).num_glyphs > maximum_glyphs ||
            ((*native).face_flags & FT_FACE_FLAG_SCALABLE) == 0) {
            owner.reset();
            return owner;
        }
        const FT_Error charmap_selected = FT_Select_Charmap(native, FT_ENCODING_UNICODE);
        if (charmap_selected != 0) owner.reset();
        return owner;
    }

    // Loads and opens a lazy face. The loader is released once it has answered, so a face
    // that cannot be opened is not loaded again; a throwing loader is kept for a retry.
    [[nodiscard]] bool open_lazy(Face& face) {
        LazyFaceBytes loaded = face.loader();
        FreeTypeFaceOwner native{};
        if (loaded.owner && !loaded.bytes.empty() && loaded.bytes.size() <= maximum_face_bytes) {
            native = open_native(loaded.bytes, 0U);
        }
        if (!native) {
            face.loader = nullptr;
            return false;
        }
        std::string family{};
        if (bounded_face_limit == 0) {
            family = (*native).family_name != nullptr ? (*native).family_name : "unknown bundled face";
        }
        // Nothing below throws.
        face.loader = nullptr;
        face.family = std::move(family);
        face.encoded = std::move(loaded.owner);
        face.face = native.release();
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
        FreeTypeFaceOwner pending_face = open_native(bytes, face_index);
        if (!pending_face) return std::nullopt;
        FT_Face native = pending_face.get();
        // Retain native cleanup until the owning Face has been constructed.
        // Family-name allocation and vector growth can both throw.
        const FontFaceId id{next_face_id++};
        std::string family{};
        if (bounded_face_limit == 0) {
            family = (*native).family_name != nullptr ? (*native).family_name : "unknown bundled face";
        }
        faces.emplace_back(id, role, weight, italic, !role.has_value(),
                           std::move(family), std::move(owned), native);
        static_cast<void>(pending_face.release());
        return id;
    }

    [[nodiscard]] std::optional<FontFaceId> register_lazy_face(
        std::uint16_t weight, bool italic, std::span<const gui_forms::detail::CodepointRange> coverage,
        LazyFaceLoader load) {
        if (bounded_face_limit != 0 && faces.size() == bounded_face_limit) return std::nullopt;
        if (!load || coverage.empty() || faces.size() >= maximum_faces) return std::nullopt;
        const FontFaceId id{next_face_id++};
        Face& face = faces.emplace_back(id, std::nullopt, weight, italic, true, std::string{},
                                        std::shared_ptr<const void>{}, nullptr);
        face.coverage = coverage;
        face.loader = std::move(load);
        return id;
    }

    [[nodiscard]] std::optional<FontFaceId> register_lazy_family_face(
        BundledFontFamily family, std::uint16_t weight, bool italic, LazyFaceLoader load) {
        if (bounded_face_limit != 0) return std::nullopt;
        if (family.value == 0U || !load || faces.size() >= maximum_faces) return std::nullopt;
        const FontFaceId id{next_face_id++};
        Face& face = faces.emplace_back(id, std::nullopt, weight, italic, false, std::string{},
                                        std::shared_ptr<const void>{}, nullptr);
        face.loader = std::move(load);
        face.named_family = family.value;
        return id;
    }

    struct CallFont final {
        Face* face{};
        hb_font_t* font{};
    };
    // Face and font borrows live for one synchronous shape call.
    // Registration cannot run concurrently; the face vector stays unchanged.
    struct ShapeCall final {
        std::array<CallFont, maximum_faces> fonts{};
        std::size_t font_count{};
        HarfBuzzBufferOwner buffer{nullptr, hb_buffer_destroy};
    };

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
        hb_font_t* hb_font = nullptr;
        const std::size_t font_count = call.font_count;
        for (std::size_t index = 0; index < font_count; ++index) {
            const CallFont& entry = call.fonts[index];
            if (entry.face == &face) {
                hb_font = entry.font;
                break;
            }
        }
        if (hb_font == nullptr) {
            if (font_count >= maximum_faces) { throw std::runtime_error("Shape face bound exceeded"); }
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
            TextLayoutPhaseTimer size_timer(diagnostics, TextLayoutPhase::ft_size);
#endif
            const double bounded_size = std::clamp(font.size, 1.0, 4096.0);
            const long long fixed_size = std::llround(bounded_size * 64.0);
            const FT_F26Dot6 size = static_cast<FT_F26Dot6>(fixed_size);
            const FT_Error size_set = FT_Set_Char_Size(face.face, 0, size, 72U, 72U);
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
            size_timer.stop();
#endif
            if (size_set != 0) { throw std::runtime_error("FreeType size setup failed"); }
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
            TextLayoutPhaseTimer font_timer(diagnostics, TextLayoutPhase::hb_font);
#endif
            // A diagnostic acquisition failure replaces the font this call
            // binds, kept or not, and is never kept.
            HarfBuzzFontOwner font_owner{nullptr, hb_font_destroy};
            bool injected = false;
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
            if (take_failure(TextLayoutFailure::empty_font)) {
                font_owner.reset(hb_font_get_empty());
                injected = true;
            } else if (take_failure(TextLayoutFailure::unbound_font)) {
                font_owner.reset(hb_font_create(hb_face_get_empty()));
                injected = true;
            }
#endif
            if (injected || !face.shaping_font) {
                if (!injected) font_owner.reset(hb_ft_font_create_referenced(face.face));
                hb_font_t* const created = font_owner.get();
                if (created == nullptr || created == hb_font_get_empty() ||
                    hb_ft_font_get_ft_face(created) != face.face) { throw std::bad_alloc(); }
                // ADR-004: advances are the outlines' own, unhinted at the logical
                // size, so glyph positions agree with the light-hinted glyphs drawn
                // at any device scale. Hinted logical-size advances drifted lines by
                // up to 12 logical px. Vertical metrics are unchanged.
                hb_ft_font_set_load_flags(created, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP);
                face.shaping_font = std::move(font_owner);
                face.shaping_size = size;
            } else if (face.shaping_size != size) {
                // The kept font takes its scale and advance cache from the
                // FreeType size, which was just set.
                hb_ft_font_changed(face.shaping_font.get());
                face.shaping_size = size;
            }
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
            font_timer.stop();
#endif
            hb_font = face.shaping_font.get();
            CallFont& entry = call.fonts[font_count];
            entry.face = &face;
            entry.font = hb_font;
            ++call.font_count;
        }
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
        TextLayoutPhaseTimer buffer_timer(diagnostics, TextLayoutPhase::buffer_setup);
#endif
        if (!call.buffer) {
            hb_buffer_t* acquired = nullptr;
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
            if (take_failure(TextLayoutFailure::empty_buffer)) {
                acquired = hb_buffer_get_empty();
            } else
#endif
            {
                acquired = hb_buffer_create();
            }
            call.buffer.reset(acquired);
        }
        hb_buffer_t* buffer = call.buffer.get();
        if (buffer == nullptr || !hb_buffer_allocation_successful(buffer)) { throw std::bad_alloc(); }
        // Full reset restores flags, script/language, context and Unicode
        // functions to newly-created defaults before establishing this run.
        hb_buffer_reset(buffer);
        hb_buffer_set_cluster_level(buffer,
            HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
        hb_buffer_add_utf8(buffer, utf8.data(), static_cast<int>(utf8.size()),
                           static_cast<unsigned>(range.start.value()),
                           static_cast<int>(range.end.value() -
                                            range.start.value()));
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
        if (take_failure(TextLayoutFailure::after_add)) { throw std::bad_alloc(); }
#endif
        if (!hb_buffer_allocation_successful(buffer)) { throw std::bad_alloc(); }
        hb_buffer_set_direction(buffer, rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
        hb_buffer_guess_segment_properties(buffer);
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
        buffer_timer.stop();
        TextLayoutPhaseTimer shape_timer(diagnostics, TextLayoutPhase::hb_shape);
#endif
        const char* const* shapers = nullptr;
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
        constexpr const char* unavailable_shapers[]{"diagnostic-unavailable-shaper", nullptr};
        if (take_failure(TextLayoutFailure::no_shaper)) { shapers = unavailable_shapers; }
#endif
        const hb_bool_t shaped = hb_shape_full(hb_font, buffer, nullptr, 0U, shapers);
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
        if (take_failure(TextLayoutFailure::after_shape)) { throw std::bad_alloc(); }
#endif
        if (!hb_buffer_allocation_successful(buffer)) { throw std::bad_alloc(); }
        if (!shaped) { throw std::runtime_error("HarfBuzz shaping failed"); }
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
        shape_timer.stop();
        TextLayoutPhaseTimer output_timer(diagnostics, TextLayoutPhase::glyph_output);
#endif

        unsigned count{};
        const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &count);
        unsigned position_count{};
        const hb_glyph_position_t* positions =
            hb_buffer_get_glyph_positions(buffer, &position_count);
        if (position_count != count || (count != 0U && (infos == nullptr || positions == nullptr))) {
            throw std::runtime_error("Incomplete HarfBuzz glyph records");
        }
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
        double ink_ascent = font.size * 0.8, ink_descent = font.size * 0.2;
        for (unsigned index = 0U; index < count; ++index) {
            const float offset_x = static_cast<float>(positions[index].x_offset) / 64.0F;
            const std::int64_t inverted_y_offset = -static_cast<std::int64_t>(positions[index].y_offset);
            const float offset_y = static_cast<float>(inverted_y_offset) / 64.0F;
            float advance_x = static_cast<float>(positions[index].x_advance) / 64.0F;
            const std::int64_t inverted_y_advance = -static_cast<std::int64_t>(positions[index].y_advance);
            const float advance_y = static_cast<float>(inverted_y_advance) / 64.0F;
            const bool cluster_end = index + 1U == count ||
                infos[index + 1U].cluster != infos[index].cluster;
            if (cluster_end && (index + 1U != count || add_trailing_spacing)) {
                advance_x += static_cast<float>(font.letter_spacing);
            }
            const ShapedGlyph glyph{GlyphId{infos[index].codepoint},
                                  Utf8Offset(infos[index].cluster),
                                  static_cast<float>(pen_x) + offset_x,
                                  static_cast<float>(pen_y) + offset_y,
                                  advance_x, advance_y};
            if constexpr (Bounded) {
                (*bounded).glyphs[(*bounded).glyph_count + index] = glyph;
            } else {
                run.glyphs.push_back(glyph);
            }
            hb_glyph_extents_t extents{};
            if (!face.role && hb_font_get_glyph_extents(hb_font, infos[index].codepoint, &extents)) {
                const double top = pen_y + offset_y - extents.y_bearing / 64.0;
                const double bottom = top - extents.height / 64.0;
                ink_ascent = std::max(ink_ascent, -top);
                ink_descent = std::max(ink_descent, bottom);
            }
            pen_x += advance_x;
            pen_y += advance_y;
        }
        result.width = std::max(result.width, pen_x);
        if ((*face.face).size != nullptr) {
            // Fallback families reserve space for every mark in their repertoire.
            // Use the actual shaped ink (including positioned marks) so an Arabic
            // or CJK fallback does not double a compact control's line box.
            const double ascent = face.role ? (*(*face.face).size).metrics.ascender / 64.0 : ink_ascent;
            const double descent = face.role ? -(*(*face.face).size).metrics.descender / 64.0 : ink_descent;
            result.ascent = std::max(result.ascent, ascent);
            result.descent = std::max(result.descent, descent);
            result.height = result.ascent + result.descent;
        }
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
            // Bounded (prepared) engines shape with the role's faces only; family faces are
            // never registered with them.
            if (face.named_family != 0U) continue;
            if (!face.role || *face.role == font.role ||
                (font.role != FontRole::content && *face.role == FontRole::content)) {
                if (count == candidates.size()) throw std::length_error("Face candidate capacity exceeded");
                std::size_t position = count;
                while (position != 0U && preference(&face, candidates[position - 1U])) {
                    candidates[position] = candidates[position - 1U];
                    --position;
                }
                candidates[position] = &face;
                ++count;
            }
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
    // call's fonts and HarfBuzz buffer (a buffer lent to it is reused);
    // `arena`, when not null, serves the bidi analysis's SheenBidi
    // allocations. Output is BoundedShapedText or StagedShaping.
    template <typename Output>
    void fill_bounded_runs(const std::string_view utf8, const FontSpec font, ShapeCall& call,
        StxSheenBidiArena* const arena,
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
        fill_bounded_parts(utf8.data(), utf8.size(), font, call, arena, candidates, edges.data(), edges.size(),
            scalar_scratch.data(), scalar_scratch.size(), segments.data(), segments.size(), directions.data(),
            directions.size(), visual.data(), visual.size(), output);
    }

    // fill_bounded_runs's work. Restricted parameters: no element is reached
    // through two of them (fill_bounded_runs checks it).
    template <typename Output>
    void fill_bounded_parts(const char* GUI_FORMS_RESTRICT text, const std::size_t text_size, const FontSpec font,
        ShapeCall& call, StxSheenBidiArena* const arena, const std::span<Face* const> candidates, const std::size_t* GUI_FORMS_RESTRICT edge_data,
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

        const SheenBidiArenaScope arena_scope(arena);
        BidiOwner bidi{};
        const SBCodepointSequence sequence{SBStringEncodingUTF8, utf8.data(), utf8.size()};
        bidi.algorithm = SBAlgorithmCreate(&sequence);
        if (bidi.algorithm == nullptr) throw std::bad_alloc();
        std::size_t offset = 0;
        std::size_t direction_count = 0;
        while (offset < utf8.size()) {
            bidi.paragraph = SBAlgorithmCreateParagraph(bidi.algorithm, offset, utf8.size() - offset, SBLevelDefaultLTR);
            if (bidi.paragraph == nullptr) throw std::bad_alloc();
            const std::size_t length = SBParagraphGetLength(bidi.paragraph);
            if (length == 0 || length > utf8.size() - offset) throw std::runtime_error("Invalid bounded bidi paragraph");
            bidi.line = SBParagraphCreateLine(bidi.paragraph, offset, length);
            if (bidi.line == nullptr) throw std::bad_alloc();
            const std::size_t count = SBLineGetRunCount(bidi.line);
            if (count > scalar_capacity - direction_count) throw std::length_error("Direction capacity exceeded");
            const SBRun* runs = SBLineGetRunsPtr(bidi.line);
            for (std::size_t index = 0; index < count; ++index) {
                const SBRun& run = runs[index];
                if (run.offset > utf8.size() || run.length > utf8.size() - run.offset) {
                    throw std::runtime_error("Invalid bounded bidi run");
                }
                directions[direction_count] = WorkspaceDirectionRun{
                    .range = {Utf8Offset(run.offset), Utf8Offset(run.offset + run.length)}, .rtl = (run.level & 1U) != 0};
                ++direction_count;
            }
            offset += length;
            bidi.clear_line();
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
    BundledFontFamily family, std::uint16_t weight, bool italic, LazyFaceLoader load) {
    const std::optional<FontFaceId> id =
        (*impl_).register_lazy_family_face(family, weight, italic, std::move(load));
    return id;
}

std::size_t HarfBuzzFontEngine::opened_face_count() const noexcept {
    std::size_t count = 0U;
    for (const Impl::Face& face : (*impl_).faces) {
        if (face.face != nullptr) ++count;
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
    for (std::size_t index = 0U; index < visual.size(); ++index) {
        const VisualSegment& segment = visual[index];
        const double before = result.width;
        (*impl_).append_run(call, result, *segment.face, utf8, segment.range, font, origin,
                          index + 1U != visual.size(), segment.rtl);
        origin += std::max(0.0, result.width - before);
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
    (*impl_).fill_bounded_runs(utf8, font, call, nullptr, selected_faces, edges, scalar_scratch,
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
                (*impl_).fill_bounded_runs(utf8, font, call, nullptr, selected, edges, scalars, segments, directions,
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
PrimaryLineMetrics sized_line_metrics(FT_Face face, FontFaceId id, FontSpec font);

// Lends a reusable workspace's HarfBuzz buffer to one shape call and takes
// it back when the call ends, also when shaping throws.
class BufferLoan final {
public:
    BufferLoan(HarfBuzzBufferOwner& home, HarfBuzzBufferOwner& borrower) noexcept
        : home_(home), borrower_(borrower) {
        borrower_ = std::move(home_);
    }
    ~BufferLoan() { home_ = std::move(borrower_); }
    BufferLoan(const BufferLoan&) = delete;
    BufferLoan& operator=(const BufferLoan&) = delete;

private:
    HarfBuzzBufferOwner& home_;
    HarfBuzzBufferOwner& borrower_;
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
        const BufferLoan loan(workspace.buffer_, call.buffer);
        try {
            (*impl_).fill_bounded_runs(utf8, font, call, workspace.arena_.get(), selected, edges, scalars, segments,
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
    extent.line_gap = sized_line_metrics(primary.face, primary.id, font).line_gap_device;
    extent.status = staged.missing_primary_face
        ? TextResolutionStatus::missing_primary_face
        : staged.missing_clusters != 0U
            ? TextResolutionStatus::missing_cluster_coverage
            : TextResolutionStatus::exact;
    workspace.keep_shaping(utf8.size(), generation);
    return extent;
}

NativeAllocationCounts HarfBuzzFontEngine::thread_native_allocations() noexcept {
    NativeAllocationCounts counts{};
    counts.freetype = freetype_allocations;
    counts.harfbuzz = stx_text_harfbuzz_allocations();
    counts.sheenbidi_heap = stx_text_sheenbidi_heap_allocations();
    counts.sheenbidi_arena = stx_text_sheenbidi_arena_allocations();
    return counts;
}

std::size_t HarfBuzzFontEngine::face_count() const noexcept {
    const std::size_t count = (*impl_).faces.size();
    return count;
}

std::optional<FontFaceSource> HarfBuzzFontEngine::face_source(const FontFaceId id) const {
    for (const Impl::Face& face : (*impl_).faces) {
        if (face.id != id) continue;
        const FT_Face native = face.face;
        // Every face is opened from memory, so its stream base is the bytes.
        if (native == nullptr || (*native).stream == nullptr || (*(*native).stream).base == nullptr) return std::nullopt;
        FontFaceSource source{};
        source.bytes = std::span<const std::byte>(reinterpret_cast<const std::byte*>((*(*native).stream).base),
                                                  static_cast<std::size_t>((*(*native).stream).size));
        source.owner = face.encoded;
        source.face_index = static_cast<std::uint32_t>((*native).face_index & 0xFFFF);
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

// The one line-height rule's inputs for a face at `font`'s size: its
// ascender, descender and line gap from FreeType's size metrics, which round
// the face's hhea values to whole pixels (ascender up, descender down, line
// height to nearest). The gap is what the rounded line height adds to the
// rounded ascender and descender, never negative. Prepared text and
// resolve() (TextBox's line box) both read it, so they agree for faces with
// a line gap. Throws when FreeType cannot size the face.
PrimaryLineMetrics sized_line_metrics(FT_Face face, FontFaceId id, const FontSpec font) {
    const double bounded_size = std::clamp(font.size, 1.0, 4096.0);
    const long long fixed_size = std::llround(bounded_size * 64.0);
    const FT_F26Dot6 size = static_cast<FT_F26Dot6>(fixed_size);
    const FT_Error sized = FT_Set_Char_Size(face, 0, size, 72U, 72U);
    if (sized != 0 || (*face).size == nullptr) throw std::runtime_error("Primary line size setup failed");
    const FT_Size_Metrics& metrics = (*(*face).size).metrics;
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
    // A named family's exact face first, opened if it has not been; otherwise the role's.
    if (font.family.value != 0U) {
        for (Impl::Face& face : (*impl_).faces) {
            if (face.named_family != font.family.value || face.weight != font.weight ||
                face.italic != font.italic) continue;
            if (face.face == nullptr && (!face.loader || !(*impl_).open_lazy(face))) continue;
            const PrimaryLineMetrics result = sized_line_metrics(face.face, face.id, font);
            return result;
        }
    }
    for (Impl::Face& face : (*impl_).faces) {
        if (!face.role || *face.role != font.role || face.weight != font.weight || face.italic != font.italic) continue;
        const PrimaryLineMetrics result = sized_line_metrics(face.face, face.id, font);
        return result;
    }
    return std::nullopt;
}

namespace {

// The face's design metrics at `font`'s size: FreeType's face ascender,
// descender and height (from hhea, or OS/2's typographic values where hhea
// has none) and OS/2's x-height, in font units, scaled by size over units
// per em, unrounded. The gap is what the height adds to the ascender and
// descender, never negative.
FontVerticalMetrics design_line_metrics(FT_Face face, const FontSpec font) {
    const double units_per_em = static_cast<double>((*face).units_per_EM);
    FontVerticalMetrics result{};
    if (units_per_em <= 0.0) return result;
    const double scale = std::clamp(font.size, 1.0, 4096.0) / units_per_em;
    const double ascent = static_cast<double>((*face).ascender);
    const double descent = -static_cast<double>((*face).descender);
    const double height = static_cast<double>((*face).height);
    result.ascent = ascent * scale;
    result.descent = descent * scale;
    result.line_gap = std::max(0.0, height - ascent - descent) * scale;
    const TT_OS2* const os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(face, FT_SFNT_OS2));
    if (os2 != nullptr && (*os2).version != 0xFFFFU && (*os2).version >= 2U) {
        result.x_height = std::max(0.0, static_cast<double>((*os2).sxHeight) * scale);
    }
    return result;
}

[[nodiscard]] LineBreakOpportunity line_break_opportunity(unsigned char decision) noexcept {
    switch (decision) {
    case LINEBREAK_MUSTBREAK: return LineBreakOpportunity::mandatory;
    case LINEBREAK_ALLOWBREAK: return LineBreakOpportunity::allowed;
    case LINEBREAK_INSIDEACHAR: return LineBreakOpportunity::inside_character;
    default: return LineBreakOpportunity::prohibited;
    }
}

} // namespace

TextResolutionStatus HarfBuzzFontEngine::fill_line_break_opportunities_utf8(
    std::string_view utf8, std::span<LineBreakOpportunity> opportunities) noexcept {
    if (opportunities.size() != utf8.size() || !validate_utf8(utf8).valid()) {
        return TextResolutionStatus::invalid_request;
    }
    if (utf8.empty()) return TextResolutionStatus::exact;
    // libunibreak writes one char per input byte. The caller's storage is
    // one byte per entry, so it takes the decisions in place, and each is
    // then read back as a byte and replaced by its opportunity.
    static_assert(sizeof(LineBreakOpportunity) == sizeof(char));
    char* const decisions = reinterpret_cast<char*>(opportunities.data());
    const utf8_t* const source = reinterpret_cast<const utf8_t*>(utf8.data());
    set_linebreaks_utf8(source, utf8.size(), "-strict", decisions);
    const std::size_t size = utf8.size();
    for (std::size_t index = 0U; index < size; ++index) {
        const unsigned char decision = static_cast<unsigned char>(decisions[index]);
        opportunities[index] = line_break_opportunity(decision);
    }
    // The end of the text ends its last line, whether or not it ends in a
    // line terminator (libunibreak reports LINEBREAK_INDETERMINATE there).
    opportunities[size - 1U] = LineBreakOpportunity::mandatory;
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
    const ShapedText shaped = shape(utf8, font);
    result.logical_size = {shaped.width, shaped.height};
    result.ascent = shaped.ascent;
    result.descent = shaped.descent;
    // The line gap is the preferred primary face's, by the rule prepared
    // text uses (sized_line_metrics); the shaped height carries none.
    const Impl::Face& primary = *candidates.front();
    result.line_gap = sized_line_metrics(primary.face, primary.id, font).line_gap_device;
    result.design_metrics = design_line_metrics(primary.face, font);
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
        const bool family_face =
            (*resolved).named_family != 0U && (*resolved).named_family == font.family.value;
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
