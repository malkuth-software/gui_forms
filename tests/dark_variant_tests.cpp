// The dark colour rule (stage T4 phase 5a): the fit to the accepted dark
// studies, the order surfaces keep, the fixed Phosphor colours, relief
// alpha and the contrast of derived ink on derived faces.
#include "../src/core/theme/contrast/theme_contrast.hpp"
#include "../src/core/theme/dark_variant/dark_variant.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>

using namespace gui_forms;
using gui_forms::detail::DarkColourClass;

namespace {

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

double lightness(Color colour) {
    const detail::OklabColour lab = detail::oklab_of(colour);
    return lab.lightness;
}

double distance(Color first, Color second) {
    const detail::OklabColour a = detail::oklab_of(first);
    const detail::OklabColour b = detail::oklab_of(second);
    const double dl = a.lightness - b.lightness;
    const double da = a.a - b.a;
    const double db = a.b - b.b;
    const double result = std::sqrt(dl * dl + da * da + db * db);
    return result;
}

// One token of the studies: its light value, its dark value and its class.
struct StudyPair final {
    const char* name{};
    Color light{};
    Color dark{};
    DarkColourClass kind{DarkColourClass::chrome_surface};
};

// theme_studies_source.html, `.t-default` against `.t-dark` (the last
// value of each token in its rule).
constexpr std::array<StudyPair, 34> study_pairs{{
    {"plane-top", Color::rgba(238, 247, 255), Color::rgba(38, 48, 42), DarkColourClass::chrome_surface},
    {"plane-mid", Color::rgba(219, 233, 252), Color::rgba(30, 39, 33), DarkColourClass::chrome_surface},
    {"plane-bottom", Color::rgba(188, 211, 244), Color::rgba(22, 29, 24), DarkColourClass::chrome_surface},
    {"plane-foot", Color::rgba(201, 219, 246), Color::rgba(44, 55, 47), DarkColourClass::chrome_surface},
    {"rail", Color::rgba(147, 173, 208), Color::rgba(27, 34, 29), DarkColourClass::chrome_surface},
    {"tab-face", Color::rgba(155, 182, 218), Color::rgba(38, 48, 42), DarkColourClass::chrome_surface},
    {"tab-face-hot", Color::rgba(172, 198, 231), Color::rgba(47, 59, 51), DarkColourClass::chrome_surface},
    {"tab-face-on", Color::rgba(232, 242, 255), Color::rgba(47, 61, 51), DarkColourClass::chrome_surface},
    {"t-top", Color::rgba(210, 228, 251), Color::rgba(58, 72, 64), DarkColourClass::chrome_surface},
    {"t-bottom", Color::rgba(163, 193, 236), Color::rgba(38, 48, 42), DarkColourClass::chrome_surface},
    {"p-top", Color::rgba(152, 181, 218), Color::rgba(29, 39, 27), DarkColourClass::chrome_surface},
    {"p-bottom", Color::rgba(181, 204, 235), Color::rgba(44, 59, 39), DarkColourClass::chrome_surface},
    {"h-top", Color::rgba(218, 233, 252), Color::rgba(67, 87, 60), DarkColourClass::chrome_surface},
    {"h-bottom", Color::rgba(177, 204, 242), Color::rgba(47, 64, 41), DarkColourClass::chrome_surface},
    {"sel", Color::rgba(217, 232, 252), Color::rgba(36, 54, 31), DarkColourClass::chrome_surface},
    {"ivory", Color::rgba(244, 244, 245), Color::rgba(22, 28, 24), DarkColourClass::stock_surface},
    {"status", Color::rgba(237, 242, 248), Color::rgba(18, 23, 19), DarkColourClass::stock_surface},
    {"disabled-face", Color::rgba(238, 242, 247), Color::rgba(26, 32, 28), DarkColourClass::stock_surface},
    {"paper", Color::rgba(255, 255, 255), Color::rgba(11, 15, 12), DarkColourClass::well_surface},
    {"a-top", Color::rgba(255, 237, 186), Color::rgba(124, 73, 100), DarkColourClass::accent_surface},
    {"a-bottom", Color::rgba(255, 207, 102), Color::rgba(90, 48, 72), DarkColourClass::accent_surface},
    {"ink", Color::rgba(21, 55, 101), Color::rgba(220, 234, 210), DarkColourClass::ink},
    {"muted", Color::rgba(91, 111, 140), Color::rgba(157, 176, 143), DarkColourClass::ink},
    {"group-ink", Color::rgba(72, 91, 112), Color::rgba(169, 189, 154), DarkColourClass::ink},
    {"tab-ink", Color::rgba(35, 68, 111), Color::rgba(199, 220, 184), DarkColourClass::ink},
    {"sel-ink", Color::rgba(21, 55, 101), Color::rgba(227, 242, 214), DarkColourClass::ink},
    {"disabled-ink", Color::rgba(98, 121, 154), Color::rgba(111, 125, 102), DarkColourClass::disabled_ink},
    {"t-border", Color::rgba(99, 132, 178), Color::rgba(79, 97, 85), DarkColourClass::line},
    {"divider", Color::rgba(169, 192, 224), Color::rgba(59, 74, 64), DarkColourClass::line},
    {"seam", Color::rgba(204, 211, 220), Color::rgba(43, 53, 46), DarkColourClass::line},
    {"disabled-edge", Color::rgba(198, 209, 223), Color::rgba(43, 53, 46), DarkColourClass::line},
    {"tab-edge-inner", Color::rgba(209, 224, 245), Color::rgba(58, 74, 63), DarkColourClass::line},
    {"focus", Color::rgba(20, 60, 116), Color::rgba(130, 179, 97), DarkColourClass::cue},
    {"a-border", Color::rgba(196, 147, 50), Color::rgba(179, 97, 130), DarkColourClass::accent_line},
}};

// The fit: every token's derived colour within `maximum_distance` of the
// studies' (OKLab), and the root mean square within `maximum_rms`. The
// studies are sketches, not tokens (rendering reference 6), so the bound
// is loose; the measured values are printed for the stage record. Lines
// and accent faces are left out of the bound: the contrast gate moved them
// (lines up to 3:1 against the stock, accent faces down so the focus ring
// keeps 3:1), and their distance is printed apart.
constexpr double maximum_distance = 0.13;
constexpr double maximum_rms = 0.06;

bool moved_by_gate(DarkColourClass kind) noexcept {
    const bool result = kind == DarkColourClass::line || kind == DarkColourClass::accent_surface;
    return result;
}

void study_fit() {
    double sum = 0.0;
    double worst = 0.0;
    std::size_t fitted = 0U;
    double gated_worst = 0.0;
    const char* worst_name = "";
    for (const StudyPair& pair : study_pairs) {
        const Color derived = detail::dark_colour(pair.light, pair.kind);
        const double error = distance(derived, pair.dark);
        if (moved_by_gate(pair.kind)) {
            if (error > gated_worst) gated_worst = error;
            continue;
        }
        ++fitted;
        sum += error * error;
        if (error > worst) {
            worst = error;
            worst_name = pair.name;
        }
    }
    const double rms = std::sqrt(sum / static_cast<double>(fitted));
    std::cout << "study fit: " << fitted << " tokens, OKLab rms " << rms << ", worst " << worst << " ("
              << worst_name << "); lines and accent faces (moved by the gate): worst " << gated_worst << '\n';
    require(worst <= maximum_distance, "every study token lies near its derived colour");
    require(rms <= maximum_rms, "the study tokens fit the rule");
}

// A surface gradient keeps its direction (the light stays upper left), and
// a well stays below the chrome it sits in (inset stays inset).
void order_kept() {
    const Color top = Color::rgba(238, 247, 255);
    const Color foot = Color::rgba(188, 211, 244);
    require(lightness(detail::dark_colour(top, DarkColourClass::chrome_surface)) >
                lightness(detail::dark_colour(foot, DarkColourClass::chrome_surface)),
            "a chrome gradient's lit top stays lighter than its foot");
    const Color paper = Color::rgba(255, 255, 255);
    require(lightness(detail::dark_colour(paper, DarkColourClass::well_surface)) <
                lightness(detail::dark_colour(foot, DarkColourClass::chrome_surface)),
            "a well stays darker than the chrome around it");
    const Color ink = Color::rgba(27, 39, 51);
    const Color muted = Color::rgba(74, 84, 95);
    require(lightness(detail::dark_colour(ink, DarkColourClass::ink)) >
                lightness(detail::dark_colour(muted, DarkColourClass::ink)),
            "ink stays stronger than muted ink");
}

void fixed_colours() {
    const Color cue = detail::dark_colour(Color::rgba(20, 60, 116, 200), DarkColourClass::cue);
    require(cue == Color::rgba(130, 179, 97, 200), "a cue is the Phosphor green, keeping its alpha");
    const Color accent = detail::dark_colour(Color::rgba(196, 147, 50), DarkColourClass::accent_line);
    require(accent == detail::phosphor_complement, "an accent's edge is the complement");
    const Color deep = detail::dark_colour(Color::rgba(73, 108, 159), DarkColourClass::deep_line);
    require(deep == detail::phosphor_black, "a deep line is black");
    const Color highlight = detail::dark_colour(Color::rgba(255, 255, 255, 230), DarkColourClass::highlight);
    require(highlight.red == 255 && highlight.alpha < 60, "a highlight dims");
    const Color shade = detail::dark_colour(Color::rgba(49, 86, 134, 135), DarkColourClass::shade);
    require(shade.red == 0 && shade.green == 0 && shade.blue == 0 && shade.alpha > 135,
            "a shade deepens toward black");
    const Color translucent = detail::dark_colour(Color::rgba(255, 255, 255, 89), DarkColourClass::chrome_surface);
    require(translucent.alpha == 89, "a surface keeps its alpha");
}

// Derived ink on derived faces meets the text gate (4.5:1) over the light
// range the default's faces use. Disabled ink is exempt from the gate
// (WCAG's inactive components, owner 2026-11-08); its ratio is printed.
void contrast_kept() {
    const std::array<Color, 3> inks{Color::rgba(27, 39, 51), Color::rgba(21, 55, 101), Color::rgba(74, 84, 95)};
    const std::array<Color, 4> faces{Color::rgba(244, 244, 245), Color::rgba(255, 255, 255),
                                     Color::rgba(202, 222, 251), Color::rgba(118, 159, 223)};
    const std::array<DarkColourClass, 4> face_classes{DarkColourClass::stock_surface, DarkColourClass::well_surface,
                                                      DarkColourClass::chrome_surface,
                                                      DarkColourClass::chrome_surface};
    double worst = 21.0;
    for (const Color ink : inks) {
        for (std::size_t index = 0; index < faces.size(); ++index) {
            const Color face = detail::dark_colour(faces[index], face_classes[index]);
            const Color text = detail::dark_colour(ink, DarkColourClass::ink);
            const double ratio = detail::contrast_ratio(text, face);
            if (ratio < worst) worst = ratio;
        }
    }
    std::cout << "derived ink on derived faces: worst " << worst << ":1\n";
    require(worst >= 4.5, "derived ink meets 4.5:1 on derived faces");
    const Color disabled = detail::dark_colour(Color::rgba(82, 99, 122), DarkColourClass::disabled_ink);
    const Color disabled_face = detail::dark_colour(Color::rgba(238, 242, 247), DarkColourClass::stock_surface);
    std::cout << "derived disabled ink on the derived stock: " << detail::contrast_ratio(disabled, disabled_face)
              << ":1 (exempt)\n";
}

// A light material darkened as a whole (the rules' fields in place on the
// light floor): the face by its family, the edge as a line, a white top
// keyline as a dimmed highlight, a cast shadow as a deeper shade; an
// accent family's edge is the complement.
void materials_darken() {
    SurfaceMaterial material{};
    material.fills = {MaterialFillLayer::solid(Color::rgba(155, 182, 218))};
    material.border = MaterialBorder{Color::rgba(73, 108, 159), 1.0};
    MaterialKeyline top{};
    top.edge = MaterialEdge::top;
    top.color = Color::rgba(255, 255, 255, 125);
    material.keylines = {top};
    MaterialShadow cast{};
    cast.color = Color::rgba(24, 56, 97, 56);
    material.shadows = {cast};
    SurfaceMaterial dark = material;
    detail::darken_material(dark, ControlVisualRole::button);
    require(dark.fills[0].color == detail::dark_colour(Color::rgba(155, 182, 218), DarkColourClass::chrome_surface),
            "a button face darkens as chrome");
    require((*dark.border).color == detail::dark_colour(Color::rgba(73, 108, 159), DarkColourClass::line),
            "an edge darkens as a line");
    require(dark.keylines[0].color.red == 255 && dark.keylines[0].color.alpha < 125, "a light keyline dims");
    require(dark.shadows[0].color.red == 0 && dark.shadows[0].color.alpha > 56, "a cast shadow deepens");
    SurfaceMaterial accent = material;
    detail::darken_material(accent, ControlVisualRole::accent_button);
    require((*accent.border).color == detail::phosphor_complement, "an accent's edge is the complement");
}

void parameters_darken() {
    ColourOrigin focus{};
    focus.source = ColourSource::role;
    focus.role = ColourRole::focus;
    require(detail::dark_parameter_colour(Color::rgba(27, 39, 51), focus, ControlVisualRole::button) ==
                detail::phosphor_green,
            "a focus role becomes the Phosphor green");
    ColourOrigin literal{};
    literal.source = ColourSource::literal;
    const Color ink = detail::dark_parameter_colour(Color::rgba(27, 39, 51), literal, ControlVisualRole::button);
    require(lightness(ink) > 0.6, "a dark literal becomes light ink");
    const Color face = detail::dark_parameter_colour(Color::rgba(244, 244, 245), literal, ControlVisualRole::panel);
    require(lightness(face) < 0.35, "a light literal becomes a dark face");
    ColourOrigin instrument{};
    instrument.source = ColourSource::role;
    instrument.role = ColourRole::phosphor_on_instrument_black;
    require(detail::dark_parameter_colour(Color::rgba(130, 179, 97), instrument, ControlVisualRole::panel) ==
                Color::rgba(130, 179, 97),
            "the instrument's phosphor is kept");
}

} // namespace

int main() {
    study_fit();
    order_kept();
    fixed_colours();
    contrast_kept();
    materials_darken();
    parameters_darken();
    std::cout << "dark colour rule: study fit, order, fixed colours, contrast, materials and parameters passed\n";
    return 0;
}
