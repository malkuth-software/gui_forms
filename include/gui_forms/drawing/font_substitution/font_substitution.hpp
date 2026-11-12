#pragma once

#include "gui_forms/export/application_export.hpp"
#include <cstdint>
#include <span>
#include <string_view>

namespace gui_drawing {

// Stage F's font substitution table, generated at build time from
// assets/fonts/font-substitution.json. A requested family resolves to a
// bundled family by its own name, then a substitution, an alias or a generic
// name. Nothing here reads a file or asks a host: callers that render load the
// named face file from the bundled font directory.

// One of a family's four faces. The value is the bold and italic bits of
// FontStyle, so a request's style selects the face directly.
enum class FontFaceStyle : std::uint8_t {
    regular = 0,
    bold = 1,
    italic = 2,
    bold_italic = 3,
};

// How close a substitution is to the family it replaces (stage F decision 8).
// A bundled own name, a generic name and an unmapped family have none.
enum class FontMappingClass : std::uint8_t {
    none,
    // Identical advances in font units, identical line breaks and a line
    // height within 1 px, measured by our engine on both faces.
    metric_compatible,
    // A mean corpus line-width deviation within 5% at every measured size.
    nearest,
};

// Where a family request was answered. The table answers the first five;
// a renderer that asks the host turns `unmapped` into `host` or `content`.
enum class FontResolutionSource : std::uint8_t {
    bundled,
    substitution,
    // Another name for a substitution, sharing its class and measurement.
    alias,
    // A CSS generic name: sans-serif, serif or monospace.
    generic,
    // Named nowhere in the table. Host bytes are acceptable only when the
    // host's face has the requested name (decision 10).
    unmapped,
    // Unmapped, and the host supplied a face with the requested name.
    host,
    // Unmapped, and no host face has that name: the content family substitutes.
    content,
};

struct GUI_FORMS_API BundledFontFace final {
    std::string_view family;
    FontFaceStyle style{FontFaceStyle::regular};
    // A file name inside the bundled font directory.
    std::string_view file;
};

struct GUI_FORMS_API FontSubstitution final {
    // As written in the table; requests match it ignoring ASCII case.
    std::string_view requested;
    std::string_view family;
    // substitution, alias or generic.
    FontResolutionSource source{FontResolutionSource::substitution};
    FontMappingClass mapping{FontMappingClass::none};
    // The substitution whose measurement this record shares: the record's
    // own request, an alias's target, or empty for a generic name.
    std::string_view measured_as;
    // The differential reports that justify the class, relative to the
    // root of the gui_forms branch of malkuth-software/notes (the measurement
    // records moved there from this repository); empty for a generic name.
    std::string_view evidence;
};

// A criterion of the metric gate (stage F, step F4): the comparison of a
// bundled candidate against a reference face's committed snapshot.
enum class FontGateCriterion : std::uint8_t {
    // Code points whose advance differs, counted.
    advances,
    // Paragraph line-break sets that differ, summed over every gate size.
    line_breaks,
    // Line height difference in pixels at any gate size.
    line_height,
    // The largest absolute mean line-width deviation at any gate size, as a
    // fraction of the reference width.
    line_width,
};

// A named exception (decisions 16 and 17): one style of a substitution may
// fail one criterion, within `limit`, in the criterion's own unit. The gate
// fails an exception whose criterion passes, so stale exceptions surface.
struct GUI_FORMS_API FontGateException final {
    std::string_view requested;
    FontFaceStyle style{FontFaceStyle::regular};
    FontGateCriterion criterion{FontGateCriterion::advances};
    double limit{0.0};
    std::string_view note;
};

// One request's answer. Every view points into the static table except
// `requested`, which borrows the caller's string.
struct GUI_FORMS_API FontFamilyResolution final {
    std::string_view requested;
    FontResolutionSource source{FontResolutionSource::unmapped};
    FontMappingClass mapping{FontMappingClass::none};
    // The bundled family and the face file drawn; both empty for unmapped and
    // host answers.
    std::string_view family;
    std::string_view face_file;
    FontFaceStyle requested_style{FontFaceStyle::regular};
    // Differs from requested_style when the family lacks the requested face
    // and its nearest face is used instead (decision 7: no synthesis).
    FontFaceStyle face_style{FontFaceStyle::regular};
    std::string_view measured_as;
};

// The face style of a FontStyle value: its bold and italic bits.
[[nodiscard]] GUI_FORMS_API FontFaceStyle font_face_style(std::uint32_t font_style) noexcept;

// The table's answer for a family and style. Matching ignores ASCII case
// only. An empty or unknown name is `unmapped`.
[[nodiscard]] GUI_FORMS_API FontFamilyResolution resolve_font_family(std::string_view requested, FontFaceStyle style) noexcept;

// The content family's face for an unmapped request no host face answers,
// reported as `content`.
[[nodiscard]] GUI_FORMS_API FontFamilyResolution content_font_resolution(std::string_view requested, FontFaceStyle style) noexcept;

[[nodiscard]] GUI_FORMS_API std::string_view content_font_family() noexcept;
[[nodiscard]] GUI_FORMS_API std::span<const BundledFontFace> bundled_font_faces() noexcept;
[[nodiscard]] GUI_FORMS_API std::span<const FontSubstitution> font_substitutions() noexcept;
[[nodiscard]] GUI_FORMS_API const char* font_resolution_source_name(FontResolutionSource source) noexcept;
[[nodiscard]] GUI_FORMS_API const char* font_mapping_class_name(FontMappingClass mapping) noexcept;
[[nodiscard]] GUI_FORMS_API std::span<const FontGateException> font_gate_exceptions() noexcept;
[[nodiscard]] GUI_FORMS_API const char* font_gate_criterion_name(FontGateCriterion criterion) noexcept;

} // namespace gui_drawing
