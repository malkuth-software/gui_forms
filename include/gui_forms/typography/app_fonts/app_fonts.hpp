#pragma once

// Application-loaded font families (owner decision of 2026-11-15, "App-loaded
// fonts: inside composites and subsurfaces"). An application may load its own
// font bytes into GUI.Forms' text engine as a family of its own, for text
// drawn inside composite controls and inside sub-surfaces (content), and
// nowhere else.
//
// Where an application family applies (admit_font, FontContext):
// - a composite's look (`.control` documents' <look>) and the composite's
//   parts and children, which carry that look, may name it; a look's
//   `font-family` resolves application family names, a stock theme's does
//   not;
// - every control of a sub-surface (its whole interface is the program's
//   content) draws it;
// - stock controls, themes applied to them and GUI.Forms' own interface (the
//   window manager, the capsule control, dialogs, the business card, the
//   overlay, help and tooltips) refuse it: the family is dropped from the
//   font, one warning line per family is logged, and the font's bundled
//   stack and role draw. The interface therefore never depends on an
//   application font.
//
// Safety:
// - Bytes are validated on load (validate_app_font: the table checks listed
//   there); a refusal is an explicit result, and nothing is registered.
// - An application family has its own family number (above
//   maximum_bundled_font_family), so its faces have their own engine faces
//   and glyph cache entries; it never replaces a bundled face.
// - A family whose face fails to open, shape or rasterise at run time is
//   quarantined for the process (quarantine_app_font_family): from then on it
//   takes part in no text, and the font's stack and role faces draw.
// - Glyphs a family lacks are drawn by the font's stack, its role's faces and
//   the bundled fallback faces, as for any family.
//
// Lifetime: the application owns a family through its AppFontFamily handle.
// Unloading (AppFontFamily::unload, or the handle's destruction) stops new
// text from resolving the family at once; the bytes are released when the
// last text engine face that opened them goes, so text already shaped with
// the family stays drawable (AppFontUnload::deferred). A family number is
// never reused within a process.
//
// Threads: every function here may be called from any thread. Loading and
// unloading are serialized by the registry's lock; the queries text engines
// make per shaped run read atomics and take the lock only to copy a family's
// face list the first time an engine meets the family.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace gui_forms {

// The numbers application families take: from minimum_app_font_family, one
// per load, never reused; at most maximum_app_font_families loads per
// process.
inline constexpr std::uint16_t minimum_app_font_family = 256U;
inline constexpr std::size_t maximum_app_font_families = 256U;
// The most faces one family holds, and the largest face accepted.
inline constexpr std::size_t maximum_app_font_faces = 4U;
inline constexpr std::size_t maximum_app_font_bytes = 64U * 1024U * 1024U;
// The longest family name, in bytes of UTF-8.
inline constexpr std::size_t maximum_app_font_name_bytes = 64U;

// Why a load was refused, or `loaded`. A refusal registers nothing.
enum class AppFontStatus : std::uint8_t {
    loaded,
    // The family: no faces, more than maximum_app_font_faces, a weight
    // outside 1 to 1000, an empty or over-long name or one holding a control
    // character, a quote, a comma or a semicolon (it could not be named in a
    // look's `font-family` list), a name a bundled family, a CSS generic or a
    // loaded application family already has, or no number left.
    no_faces,
    too_many_faces,
    weight_invalid,
    name_invalid,
    name_taken,
    capacity_exhausted,
    // A face's bytes (validate_app_font): empty or larger than
    // maximum_app_font_bytes; not an sfnt (TrueType 0x00010000 or 'true', or
    // OpenType 'OTTO'); a collection ('ttcf'), which is refused; a table
    // directory that does not fit, overlaps itself or lists a table twice; a
    // required table missing; or a required table whose contents fail its
    // check. A face whose bytes pointer is null is `empty`.
    empty,
    too_large,
    not_sfnt,
    collection_refused,
    table_directory_invalid,
    table_missing,
    table_invalid,
};

// The checks' answer for one face: the status, and for table_missing or
// table_invalid the table's tag (big-endian, 'cmap' is 0x636D6170).
struct GUI_FORMS_API AppFontValidation final {
    AppFontStatus status{AppFontStatus::loaded};
    std::uint32_t table{};
    friend constexpr bool operator==(const AppFontValidation& left, const AppFontValidation& right) noexcept {
        return left.status == right.status && left.table == right.table;
    }
};

// Checks `bytes` as one font face, reading only the bytes (no engine). The
// stated set of checks:
// 1. size: 1 to maximum_app_font_bytes;
// 2. the sfnt version: 0x00010000 or 'true' (TrueType outlines) or 'OTTO'
//    (CFF outlines); 'ttcf' collections are refused;
// 3. the table directory: numTables 1 to 64, the directory inside the bytes,
//    each table's offset and length inside the bytes, no tag twice;
// 4. required tables: head, hhea, hmtx, maxp, cmap, name and post; glyf and
//    loca for TrueType outlines, CFF or CFF2 for 'OTTO';
// 5. head: at least 54 bytes, magic number 0x5F0F3CF5, unitsPerEm 16 to
//    16384, indexToLocFormat 0 or 1;
// 6. maxp: at least 6 bytes, numGlyphs 1 to 65535;
// 7. hhea: at least 36 bytes, numberOfHMetrics 1 to numGlyphs;
// 8. hmtx: at least 4 * numberOfHMetrics + 2 * (numGlyphs -
//    numberOfHMetrics) bytes;
// 9. loca (TrueType): exactly (numGlyphs + 1) offsets of the head's format;
//    offsets ascending and the last no larger than glyf's length;
// 10. cmap: version 0, at least one encoding record inside the table, and a
//    Unicode subtable (platform 0, or platform 3 with encoding 1 or 10) whose
//    offset lies inside the table.
// Passing them does not prove a face draws: what fails later is quarantined.
[[nodiscard]] GUI_FORMS_API AppFontValidation validate_app_font(std::span<const std::byte> bytes) noexcept;

// One face of a family to load: its bytes, which the registry shares (the
// application may drop its reference after the load), its weight (1 to 1000)
// and slant.
struct GUI_FORMS_API AppFontFace final {
    std::shared_ptr<const std::vector<std::byte>> bytes{};
    std::uint16_t weight{400};
    bool italic{};
};

// What unloading did: the family was released (no engine had opened it),
// deferred (new text no longer resolves it, and its bytes go with the last
// engine face that opened them), or there was no live family to unload.
enum class AppFontUnload : std::uint8_t {
    released,
    deferred,
    not_loaded,
};

struct AppFontLoad;

// Loads `faces` as one family called `name`. Validates the name and every
// face first and registers nothing on a refusal. Thread-safe.
[[nodiscard]] GUI_FORMS_API AppFontLoad load_app_font_family(std::string_view name,
                                                             std::span<const AppFontFace> faces);

// The application's ownership of one loaded family. Move-only; destroying a
// handle that still holds a family unloads it. Only load_app_font_family
// makes a handle that holds one.
class GUI_FORMS_API AppFontFamily final {
public:
    AppFontFamily() noexcept = default;
    ~AppFontFamily();
    AppFontFamily(const AppFontFamily&) = delete;
    AppFontFamily& operator=(const AppFontFamily&) = delete;
    AppFontFamily(AppFontFamily&& other) noexcept;
    AppFontFamily& operator=(AppFontFamily&& other) noexcept;

    // The family's number, zero for an empty handle. FontSpec::family takes
    // it.
    [[nodiscard]] FontFamilyId id() const noexcept { return id_; }
    // Unloads the family now and empties the handle.
    AppFontUnload unload() noexcept;

private:
    friend GUI_FORMS_API AppFontLoad load_app_font_family(std::string_view name, std::span<const AppFontFace> faces);
    explicit AppFontFamily(FontFamilyId id) noexcept : id_(id) {}
    FontFamilyId id_{};
};

// A load's answer: the validation (the first face refused, with face_index,
// or `loaded`) and, when loaded, the handle.
struct GUI_FORMS_API AppFontLoad final {
    AppFontValidation validation{};
    std::size_t face_index{};
    AppFontFamily family{};
};

// Whether `family` is in the application families' number range.
[[nodiscard]] constexpr bool app_font_family_number(FontFamilyId family) noexcept {
    const bool in_range = family.value >= minimum_app_font_family &&
                          family.value < minimum_app_font_family + maximum_app_font_families;
    return in_range;
}

// Where an application family stands.
enum class AppFontState : std::uint8_t {
    // No load ever gave the number.
    absent,
    // Loaded and drawing.
    live,
    // Failed at run time; draws nothing for the rest of the process.
    quarantined,
    // Unloaded while an engine still holds its bytes.
    unloading,
    // Unloaded, bytes released.
    released,
};

[[nodiscard]] GUI_FORMS_API AppFontState app_font_family_state(FontFamilyId family) noexcept;

// Whether text may resolve `family` now: a bundled family, or a live
// application family. Lock-free.
[[nodiscard]] GUI_FORMS_API bool font_family_drawable(FontFamilyId family) noexcept;

// Whether `family` is zero, a bundled family or a number a load gave (in any
// state): the check a stored font's family passes (a C ABI gf_font, a theme
// declaration).
[[nodiscard]] GUI_FORMS_API bool known_font_family(FontFamilyId family) noexcept;

// The live application family called `name` (ASCII case ignored), or zero.
[[nodiscard]] GUI_FORMS_API FontFamilyId app_font_family_named(std::string_view name);

// Why a family was quarantined.
enum class AppFontQuarantine : std::uint8_t {
    open_failed,
    shaping_failed,
    rasterising_failed,
};

// Quarantines a live application family: logs one line, counts it, and
// from then on font_family_drawable is false for it. Anything else is
// ignored. Thread-safe, never throws.
GUI_FORMS_API void quarantine_app_font_family(FontFamilyId family, AppFontQuarantine reason) noexcept;

// One face of a live family as a text engine registers it: the bytes and the
// owner that keeps them (the engine holds the owner for as long as its face
// lives, which is what defers an unload).
struct GUI_FORMS_API AppFontFaceSource final {
    std::span<const std::byte> bytes{};
    std::shared_ptr<const void> owner{};
    std::uint16_t weight{400};
    bool italic{};
};

// Writes the faces of `family` into `output` while the family is live and
// returns their count (zero otherwise, or when `output` is shorter than the
// family's faces). Thread-safe.
[[nodiscard]] GUI_FORMS_API std::size_t app_font_family_faces(FontFamilyId family,
                                                              std::span<AppFontFaceSource> output);

// The context a font is drawn in (admit_font).
enum class FontContext : std::uint8_t {
    // A stock control or GUI.Forms' own interface: bundled families only.
    interface,
    // A composite control's look, parts and children.
    composite,
    // A sub-surface's controls: the program's own content.
    content,
};

// `font` as `context` may draw it: unchanged in a composite or content
// context, and in the interface context with an application family dropped
// (FontSpec::family set to zero; a stack that lists one is dropped as a
// whole, so the role draws). Each refused family is counted, and logged once
// per family per process. Lock-free on the path that changes nothing.
[[nodiscard]] GUI_FORMS_API FontSpec admit_font(FontSpec font, FontContext context) noexcept;

// Counters (AGENTS.md: structured instrumentation), for the process.
//   loads / refused_loads: load_app_font_family's answers;
//   refused_uses: fonts admit_font changed;
//   quarantines: families quarantined;
//   unloads_released / unloads_deferred: unloads by outcome.
struct GUI_FORMS_API AppFontStatistics final {
    std::uint64_t loads{};
    std::uint64_t refused_loads{};
    std::uint64_t refused_uses{};
    std::uint64_t quarantines{};
    std::uint64_t unloads_released{};
    std::uint64_t unloads_deferred{};
};

[[nodiscard]] GUI_FORMS_API AppFontStatistics app_font_statistics() noexcept;

} // namespace gui_forms
