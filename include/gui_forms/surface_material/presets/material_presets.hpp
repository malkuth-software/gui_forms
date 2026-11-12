#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/surface_material/types/surface_material_types.hpp"

namespace gui_forms {

// Material presets (E9-C): named looks built from the ordinary material
// layers, for a theme's recipes. No theme must carry them (design language
// §9.3 item 5), and no built-in theme does.

// Where the gloss gradient breaks, as fractions of the face's height from
// its top: the colour steps from above_break to below_break between them.
inline constexpr double gloss_break_start = 0.48;
inline constexpr double gloss_break_end = 0.52;

// The colours of a gloss material, top to bottom. The defaults are a pale
// blue gloss.
struct GUI_FORMS_API GlossColours final {
    // The gradient's four stops: at the top (0), just above the break
    // (gloss_break_start), just below it (gloss_break_end) and at the
    // bottom (1).
    Color top{Color::rgba(236, 244, 252)};
    Color above_break{Color::rgba(196, 218, 242)};
    Color below_break{Color::rgba(150, 186, 228)};
    Color bottom{Color::rgba(186, 214, 244)};
    // The 1-unit shine under the flat top, inside the border.
    Color shine{Color::rgba(255, 255, 255, 176)};
    // The 1-unit border around the face.
    Color border{Color::rgba(62, 96, 140)};
};

// The gloss material (design language §4.3): one vertical four-stop
// gradient with a hard break near mid-face, a 1-unit shine keyline just
// under the top edge (inside the border, clipped to the corners), and a
// 1-unit border, with `corner_radius` in logical units. Throws
// std::invalid_argument for a corner radius that is not finite or lies
// outside [0, 4096].
[[nodiscard]] GUI_FORMS_API SurfaceMaterial gloss_material(const GlossColours& colours, double corner_radius);

// The surface finish's constants (E9.6, owner 2026-10-23), provisional
// until the owner's eye at the gate: at finish f, the face's top half is
// lifted towards white by finish_lift * f, the colour just below the break
// is darkened by finish_break_darkening * f, and the shine keyline's alpha
// is finish_shine_alpha * f.
inline constexpr double finish_lift = 0.25;
inline constexpr double finish_break_darkening = 0.15;
inline constexpr double finish_shine_alpha = 176.0;

// The material `material.finish` describes, with a finish of 0 (E9.6):
// the first fill's colours at 0, 0.5 and 1 along it (a solid fill's colour
// throughout) are rebuilt as the gloss's four vertical stops, keeping the
// fill's blend mode, its top half lifted and its break darkened, and a white shine keyline is added
// under the top edge, inside the border. A material whose finish is 0 is
// returned as it is. `material` must be valid (valid_surface_material);
// gloss_material is unchanged and is not finish 1 over its colours.
[[nodiscard]] GUI_FORMS_API SurfaceMaterial finished_surface_material(const SurfaceMaterial& material);

} // namespace gui_forms
