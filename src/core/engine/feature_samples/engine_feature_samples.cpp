#include "engine_feature_samples.hpp"

#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/surface_material/presets/material_presets.hpp"

#include <chrono>
#include <string>

namespace gui_forms::engine_samples {
namespace {

using std::chrono::milliseconds;

constexpr std::size_t button_role = static_cast<std::size_t>(ControlVisualRole::button);
constexpr std::size_t window_role = static_cast<std::size_t>(ControlVisualRole::window);
constexpr std::size_t hot_state = static_cast<std::size_t>(ControlSurfaceState::hot);

constexpr Color ink = Color::rgba(40, 52, 66);
constexpr Color pale = Color::rgba(232, 238, 246);
constexpr Color sky = Color::rgba(92, 156, 222);
constexpr Color rose = Color::rgba(214, 92, 118);
constexpr Color leaf = Color::rgba(96, 176, 104);
constexpr Color amber = Color::rgba(236, 184, 64);

std::vector<GradientStop> two_stops(Color from, Color to) {
    std::vector<GradientStop> stops{GradientStop{0.0, from}, GradientStop{1.0, to}};
    return stops;
}

std::vector<GradientStop> three_stops() {
    std::vector<GradientStop> stops{GradientStop{0.0, sky}, GradientStop{0.5, amber}, GradientStop{1.0, rose}};
    return stops;
}

// A material of no fills yet, bordered in ink, with the part's radius.
SurfaceMaterial bordered() {
    SurfaceMaterial material{};
    material.fills.clear();
    material.border = MaterialBorder{ink, 1.0};
    material.corner_radius = feature_sample_corner_radius;
    return material;
}

// A pale face: one solid fill, bordered.
SurfaceMaterial pale_face() {
    SurfaceMaterial material = bordered();
    material.fills.push_back(MaterialFillLayer::solid(pale));
    return material;
}

// A face lit top to bottom: one linear gradient, bordered.
SurfaceMaterial lit_face() {
    SurfaceMaterial material = bordered();
    material.fills.push_back(MaterialFillLayer::linear(Point{0.0, 0.0}, Point{0.0, 1.0}, two_stops(pale, sky)));
    return material;
}

// Every button recipe the part may read, ordinary and selected in every
// state, is `material`.
void use_material(ThemeDefinition& definition, const SurfaceMaterial& material) {
    ControlRoleRecipes& button = definition.roles[button_role];
    for (ControlVisualRecipe& recipe : button.ordinary) recipe.material = material;
    for (ControlVisualRecipe& recipe : button.selected) recipe.material = material;
}

// A rule selecting the sample's part: the subject's part of `role`.
ThemeRule subject_rule(std::string_view label, PartRole role) {
    ThemeRule rule{};
    rule.label = std::string(label);
    rule.level = CascadeLevel::theme_sheet;
    rule.selector.stable_id = std::string(feature_sample_subject);
    rule.selector.role = role;
    return rule;
}

// The rule drawing the subject's face with the material primitive, the
// button recipe and `shape` (one of part_shape_names).
ThemeRule material_rule(std::string_view label, std::string_view shape) {
    ThemeRule rule = subject_rule(label, PartRole::face);
    rule.declarations.push_back(RuleDeclaration::primitive("material"));
    rule.declarations.push_back(RuleDeclaration::choice_value("recipe", "button"));
    rule.declarations.push_back(RuleDeclaration::choice_value("shape", shape));
    return rule;
}

// The usual material sample: `material` on every button recipe, drawn as a
// rounded rectangle.
void draw_material(ThemeDefinition& definition, const SurfaceMaterial& material, std::string_view label) {
    use_material(definition, material);
    definition.rules.rules.push_back(material_rule(label, "rounded_rectangle"));
}

// -- fills -----------------------------------------------------------------------

void fill_conic(const FeatureSampleInputs&, ThemeDefinition& definition) {
    SurfaceMaterial material = bordered();
    material.fills.push_back(MaterialFillLayer::conic(Point{0.5, 0.5}, 30.0, three_stops()));
    draw_material(definition, material, "fill_conic");
}

void fill_repeating_conic(const FeatureSampleInputs&, ThemeDefinition& definition) {
    SurfaceMaterial material = bordered();
    material.fills.push_back(MaterialFillLayer::repeating_conic(Point{0.3, 0.5}, 0.0, 90.0, two_stops(sky, pale)));
    draw_material(definition, material, "fill_repeating_conic");
}

void fill_repeating_radial(const FeatureSampleInputs&, ThemeDefinition& definition) {
    SurfaceMaterial material = bordered();
    material.fills.push_back(
        MaterialFillLayer::repeating_radial(Point{0.5, 0.5}, Size{0.2, 0.4}, two_stops(leaf, pale)));
    draw_material(definition, material, "fill_repeating_radial");
}

// A reflected linear gradient in logical units, under a translucent one
// given by a CSS angle.
void fill_reflected_linear(const FeatureSampleInputs&, ThemeDefinition& definition) {
    SurfaceMaterial material = bordered();
    material.fills.push_back(MaterialFillLayer::linear(Point{0.0, 0.0}, Point{12.0, 6.0}, two_stops(amber, rose),
                                                       MaterialCoordinateSpace::logical,
                                                       GradientSpreadMode::reflect));
    const std::vector<GradientStop> veil = two_stops(Color::rgba(255, 255, 255, 160), Color::rgba(0, 0, 0, 0));
    material.fills.push_back(MaterialFillLayer::linear_css_angle(135.0, veil));
    draw_material(definition, material, "fill_reflected_linear");
}

// A solid, a linear gradient of three stops and a padded radial gradient,
// the last at half opacity.
void fill_layers(const FeatureSampleInputs&, ThemeDefinition& definition) {
    SurfaceMaterial material = bordered();
    material.fills.push_back(MaterialFillLayer::solid(pale));
    material.fills.push_back(MaterialFillLayer::linear(Point{0.0, 0.0}, Point{1.0, 0.0}, three_stops()));
    MaterialFillLayer spot = MaterialFillLayer::radial(Point{0.25, 0.3}, Size{0.3, 0.6}, two_stops(pale, sky));
    spot.opacity = 0.5;
    material.fills.push_back(spot);
    draw_material(definition, material, "fill_layers");
}

// The sample image's size in its own pixels.
constexpr double image_side = static_cast<double>(feature_sample_image_side);
constexpr Size image_size{image_side, image_side};

void image_stretched(const FeatureSampleInputs& inputs, ThemeDefinition& definition) {
    SurfaceMaterial material = pale_face();
    material.fills.push_back(MaterialFillLayer::stretched_image(inputs.image, image_size));
    draw_material(definition, material, "image_stretched");
}

// A tiled image, its tiles anchored at the box's centre and moved by an
// offset (CSS background-position).
void image_tiled_positioned(const FeatureSampleInputs& inputs, ThemeDefinition& definition) {
    SurfaceMaterial material = pale_face();
    MaterialFillLayer tiles = MaterialFillLayer::tiled_image(inputs.image, image_size, 1.0);
    tiles.image_anchor = Point{0.5, 0.5};
    tiles.image_offset = Point{3.0, 1.0};
    material.fills.push_back(tiles);
    draw_material(definition, material, "image_tiled_positioned");
}

// One tile alone (CSS background-repeat: no-repeat), at the box's centre.
void image_single(const FeatureSampleInputs& inputs, ThemeDefinition& definition) {
    SurfaceMaterial material = pale_face();
    MaterialFillLayer tile = MaterialFillLayer::tiled_image(inputs.image, image_size, 1.0);
    tile.image_mode = MaterialImageMode::single;
    tile.image_anchor = Point{0.5, 0.5};
    material.fills.push_back(tile);
    draw_material(definition, material, "image_single");
}

void image_nine_patch(const FeatureSampleInputs& inputs, ThemeDefinition& definition) {
    SurfaceMaterial material = pale_face();
    material.fills.push_back(MaterialFillLayer::nine_patch(inputs.image, image_size, Insets{2.0, 2.0, 2.0, 2.0}, 1.0));
    draw_material(definition, material, "image_nine_patch");
}

// One procedural pattern, as the only fill.
void draw_procedural(ThemeDefinition& definition, ProceduralPattern pattern, std::uint32_t seed,
                     std::string_view label) {
    ProceduralFill fill{};
    fill.pattern = pattern;
    fill.seed = seed;
    fill.tile = 32.0;
    fill.ground = Color::rgba(150, 128, 104);
    fill.figure = Color::rgba(112, 88, 64);
    fill.accent = Color::rgba(176, 156, 128);
    fill.bloom = Color::rgba(255, 255, 255, 48);
    SurfaceMaterial material = bordered();
    material.fills.push_back(MaterialFillLayer::procedural_pattern(fill));
    draw_material(definition, material, label);
}

void procedural_felt(const FeatureSampleInputs&, ThemeDefinition& definition) {
    draw_procedural(definition, ProceduralPattern::felt, 0x0E11F001U, "procedural_felt");
}

void procedural_wood_grain(const FeatureSampleInputs&, ThemeDefinition& definition) {
    draw_procedural(definition, ProceduralPattern::wood_grain, 0x0E11F002U, "procedural_wood_grain");
}

void procedural_paper_fibre(const FeatureSampleInputs&, ThemeDefinition& definition) {
    draw_procedural(definition, ProceduralPattern::paper_fibre, 0x0E11F003U, "procedural_paper_fibre");
}

void procedural_pinstripe(const FeatureSampleInputs&, ThemeDefinition& definition) {
    draw_procedural(definition, ProceduralPattern::pinstripe, 0x0E11F004U, "procedural_pinstripe");
}

void procedural_brushed_metal(const FeatureSampleInputs&, ThemeDefinition& definition) {
    draw_procedural(definition, ProceduralPattern::brushed_metal, 0x0E11F005U, "procedural_brushed_metal");
}

void procedural_fresco(const FeatureSampleInputs&, ThemeDefinition& definition) {
    draw_procedural(definition, ProceduralPattern::fresco, 0x0E11F006U, "procedural_fresco");
}

void procedural_checker(const FeatureSampleInputs&, ThemeDefinition& definition) {
    draw_procedural(definition, ProceduralPattern::checker, 0x0E11F007U, "procedural_checker");
}

// One surface finish (skeudoindustrial surfaces, phase 1), as the only
// fill: the study's paint and dust, a seed of its own.
void draw_finish(ThemeDefinition& definition, FinishKind kind, std::int32_t seed, std::string_view label) {
    FinishFill finish{};
    finish.kind = kind;
    finish.seed = seed;
    if (kind == FinishKind::cloth_weave) finish.secondary = Color::rgba(0xc8, 0xb9, 0x8f);
    SurfaceMaterial material = bordered();
    material.fills.push_back(MaterialFillLayer::finish_fill(finish));
    draw_material(definition, material, label);
}

void finish_dusty_matte(const FeatureSampleInputs&, ThemeDefinition& definition) {
    draw_finish(definition, FinishKind::dusty_matte, 101, "finish_dusty_matte");
}

void finish_orange_peel(const FeatureSampleInputs&, ThemeDefinition& definition) {
    draw_finish(definition, FinishKind::orange_peel, 102, "finish_orange_peel");
}

void finish_hammertone(const FeatureSampleInputs&, ThemeDefinition& definition) {
    draw_finish(definition, FinishKind::hammertone, 103, "finish_hammertone");
}

void finish_wrinkle(const FeatureSampleInputs&, ThemeDefinition& definition) {
    draw_finish(definition, FinishKind::wrinkle, 104, "finish_wrinkle");
}

void finish_bead_blast(const FeatureSampleInputs&, ThemeDefinition& definition) {
    draw_finish(definition, FinishKind::bead_blast, 105, "finish_bead_blast");
}

void finish_sand_cast(const FeatureSampleInputs&, ThemeDefinition& definition) {
    draw_finish(definition, FinishKind::sand_cast, 106, "finish_sand_cast");
}

void finish_flat(const FeatureSampleInputs&, ThemeDefinition& definition) {
    draw_finish(definition, FinishKind::flat, 107, "finish_flat");
}

void finish_cloth_weave(const FeatureSampleInputs&, ThemeDefinition& definition) {
    draw_finish(definition, FinishKind::cloth_weave, 108, "finish_cloth_weave");
}

// A face with a second fill blended onto it by `blend`.
void draw_fill_blend(ThemeDefinition& definition, MaterialBlendMode blend, std::string_view label) {
    SurfaceMaterial material = lit_face();
    MaterialFillLayer over = MaterialFillLayer::linear(Point{0.0, 0.0}, Point{1.0, 0.0}, two_stops(rose, leaf));
    over.blend = blend;
    material.fills.push_back(over);
    draw_material(definition, material, label);
}

void fill_blend_multiply(const FeatureSampleInputs&, ThemeDefinition& definition) {
    draw_fill_blend(definition, MaterialBlendMode::multiply, "fill_blend_multiply");
}

void fill_blend_screen(const FeatureSampleInputs&, ThemeDefinition& definition) {
    draw_fill_blend(definition, MaterialBlendMode::screen, "fill_blend_screen");
}

// -- shapes, borders and keylines -------------------------------------------------

void shape_corner_radii(const FeatureSampleInputs&, ThemeDefinition& definition) {
    SurfaceMaterial material = lit_face();
    material.corner_radii = MaterialCornerRadii{10.0, 0.0, 6.0, 2.0};
    draw_material(definition, material, "shape_corner_radii");
}

// The lit face drawn as `shape`.
void draw_shape(ThemeDefinition& definition, std::string_view shape, std::string_view label) {
    use_material(definition, lit_face());
    definition.rules.rules.push_back(material_rule(label, shape));
}

void shape_pill(const FeatureSampleInputs&, ThemeDefinition& definition) {
    draw_shape(definition, "pill", "shape_pill");
}

void shape_circle(const FeatureSampleInputs&, ThemeDefinition& definition) {
    draw_shape(definition, "circle", "shape_circle");
}

void shape_cap_start(const FeatureSampleInputs&, ThemeDefinition& definition) {
    draw_shape(definition, "cap_start", "shape_cap_start");
}

void shape_cap_end(const FeatureSampleInputs&, ThemeDefinition& definition) {
    draw_shape(definition, "cap_end", "shape_cap_end");
}

void border_dashed(const FeatureSampleInputs&, ThemeDefinition& definition) {
    SurfaceMaterial material = pale_face();
    material.border = MaterialBorder{ink, 2.0, MaterialBorderStyle::dashed};
    draw_material(definition, material, "border_dashed");
}

void border_dotted(const FeatureSampleInputs&, ThemeDefinition& definition) {
    SurfaceMaterial material = pale_face();
    material.border = MaterialBorder{ink, 2.0, MaterialBorderStyle::dotted};
    draw_material(definition, material, "border_dotted");
}

// Borders on two edges only, of two widths and colours.
void border_edges(const FeatureSampleInputs&, ThemeDefinition& definition) {
    SurfaceMaterial material = pale_face();
    material.border.reset();
    material.corner_radius = 0.0;
    material.border_edges.top = MaterialBorder{rose, 2.0};
    material.border_edges.bottom = MaterialBorder{ink, 1.0};
    draw_material(definition, material, "border_edges");
}

// A shine keyline under the top edge and a shade keyline over the bottom.
void keylines(const FeatureSampleInputs&, ThemeDefinition& definition) {
    SurfaceMaterial material = lit_face();
    material.keylines.push_back(MaterialKeyline{MaterialEdge::top, Color::rgba(255, 255, 255, 200), 1.0, 1.0});
    material.keylines.push_back(MaterialKeyline{MaterialEdge::bottom, Color::rgba(0, 0, 0, 72), 1.0, 1.0});
    material.keylines.push_back(MaterialKeyline{MaterialEdge::left, Color::rgba(255, 255, 255, 96), 1.0, 0.0});
    material.keylines.push_back(MaterialKeyline{MaterialEdge::right, Color::rgba(0, 0, 0, 48), 1.0, 0.0});
    draw_material(definition, material, "keylines");
}

// -- shadows, glows and the gloss --------------------------------------------------

void shadows_outer_inset(const FeatureSampleInputs&, ThemeDefinition& definition) {
    SurfaceMaterial material = pale_face();
    material.shadows.push_back(MaterialShadow{Point{0.0, 2.0}, 4.0, 0.0, Color::rgba(20, 30, 42, 96), false});
    material.shadows.push_back(MaterialShadow{Point{0.0, 1.0}, 2.0, 1.0, Color::rgba(20, 30, 42, 80), true});
    draw_material(definition, material, "shadows_outer_inset");
}

// A glow placed `placement`: under the face (a halo beside it) or over it.
void draw_glow(ThemeDefinition& definition, MaterialGlowPlacement placement, std::string_view label) {
    SurfaceMaterial material = lit_face();
    MaterialGlow glow{};
    glow.color = Color::rgba(255, 236, 160, 160);
    glow.blur_radius = 3.0;
    glow.spread = 1.0;
    glow.placement = placement;
    material.glows.push_back(glow);
    draw_material(definition, material, label);
}

void glow_under(const FeatureSampleInputs&, ThemeDefinition& definition) {
    draw_glow(definition, MaterialGlowPlacement::under, "glow_under");
}

void glow_over(const FeatureSampleInputs&, ThemeDefinition& definition) {
    draw_glow(definition, MaterialGlowPlacement::over, "glow_over");
}

void gloss_preset(const FeatureSampleInputs&, ThemeDefinition& definition) {
    draw_material(definition, gloss_material(GlossColours{}, feature_sample_corner_radius), "gloss_preset");
}

// -- filters, blends, the finish and masks -----------------------------------------

void filter_colour_matrix(const FeatureSampleInputs&, ThemeDefinition& definition) {
    SurfaceMaterial material = lit_face();
    material.fills.push_back(MaterialFillLayer::linear(Point{0.0, 0.0}, Point{1.0, 0.0}, three_stops()));
    material.colour_filter = MaterialColourMatrix::sepia(0.8).then(MaterialColourMatrix::hue_rotate(90.0));
    draw_material(definition, material, "filter_colour_matrix");
}

void filter_drop_shadow(const FeatureSampleInputs&, ThemeDefinition& definition) {
    SurfaceMaterial material = lit_face();
    material.drop_shadow = MaterialDropShadow{Point{2.0, 3.0}, 2.0, Color::rgba(0, 0, 0, 128)};
    draw_material(definition, material, "filter_drop_shadow");
}

void filter_blur(const FeatureSampleInputs&, ThemeDefinition& definition) {
    SurfaceMaterial material = lit_face();
    material.fills.push_back(MaterialFillLayer::repeating_linear(Point{0.0, 0.0}, Point{8.0, 0.0},
                                                                 two_stops(rose, pale)));
    material.filter_blur = 3.0;
    draw_material(definition, material, "filter_blur");
}

// The part drawn through a paint group of `blend` over what lies beneath.
void draw_part_blend(ThemeDefinition& definition, std::string_view blend, std::string_view label) {
    use_material(definition, lit_face());
    ThemeRule rule = material_rule(label, "rounded_rectangle");
    rule.declarations.push_back(RuleDeclaration::choice_value(blend_property, blend));
    definition.rules.rules.push_back(rule);
}

void part_blend_multiply(const FeatureSampleInputs&, ThemeDefinition& definition) {
    draw_part_blend(definition, "multiply", "part_blend_multiply");
}

void part_blend_screen(const FeatureSampleInputs&, ThemeDefinition& definition) {
    draw_part_blend(definition, "screen", "part_blend_screen");
}

// The finish derived from the face (E9.6, owner option A1) at 0.75.
void finish(const FeatureSampleInputs&, ThemeDefinition& definition) {
    SurfaceMaterial material = bordered();
    material.fills.push_back(MaterialFillLayer::linear(Point{0.0, 0.0}, Point{0.0, 1.0}, two_stops(sky, leaf)));
    material.finish = 0.75;
    draw_material(definition, material, "finish");
}

void mask_solid(const FeatureSampleInputs&, ThemeDefinition& definition) {
    SurfaceMaterial material = lit_face();
    material.shadows.push_back(MaterialShadow{Point{0.0, 2.0}, 3.0, 0.0, Color::rgba(20, 30, 42, 96), false});
    material.mask = MaterialFillLayer::solid(Color::rgba(0, 0, 0, 144));
    draw_material(definition, material, "mask_solid");
}

void mask_linear(const FeatureSampleInputs&, ThemeDefinition& definition) {
    SurfaceMaterial material = lit_face();
    material.mask = MaterialFillLayer::linear(Point{0.0, 0.0}, Point{1.0, 0.0},
                                              two_stops(Color::rgba(0, 0, 0, 255), Color::rgba(0, 0, 0, 0)));
    draw_material(definition, material, "mask_linear");
}

void mask_image(const FeatureSampleInputs& inputs, ThemeDefinition& definition) {
    SurfaceMaterial material = lit_face();
    material.mask = MaterialFillLayer::stretched_image(inputs.image, image_size);
    draw_material(definition, material, "mask_image");
}

// A reflection (stage T5, C7) below the face: 1 unit of gap, 6 of reach,
// fading from 0.6 to 0.
void reflection_below(const FeatureSampleInputs&, ThemeDefinition& definition) {
    SurfaceMaterial material = lit_face();
    material.reflection = MaterialReflection{MaterialEdge::bottom, 1.0, 6.0, 0.6, 0.0};
    draw_material(definition, material, "reflection_below");
}

// -- light: glass's colour filter and the sheen ------------------------------------

// Glass over a backplane of a gradient, the button recipe's colour filter
// applied to the blurred backplane before the tint (E9.6 G2).
void glass_colour_filter(const FeatureSampleInputs&, ThemeDefinition& definition) {
    SurfaceMaterial backplane{};
    backplane.fills.clear();
    backplane.fills.push_back(MaterialFillLayer::linear(Point{0.0, 0.0}, Point{1.0, 1.0}, three_stops()));
    ControlRoleRecipes& window = definition.roles[window_role];
    for (ControlVisualRecipe& recipe : window.ordinary) recipe.material = backplane;
    ControlRoleRecipes& button = definition.roles[button_role];
    for (ControlVisualRecipe& recipe : button.ordinary) {
        recipe.material.colour_filter = MaterialColourMatrix::grayscale(1.0);
    }
    ThemeRule rule = subject_rule("glass_colour_filter", PartRole::region);
    rule.declarations.push_back(RuleDeclaration::primitive("glass"));
    rule.declarations.push_back(RuleDeclaration::number_value("blur", 3.0));
    rule.declarations.push_back(RuleDeclaration::choice_value("recipe", "button"));
    definition.rules.rules.push_back(rule);
}

void sheen_at_pointer(const FeatureSampleInputs&, ThemeDefinition& definition) {
    ThemeRule rule = subject_rule("sheen_at_pointer", PartRole::sheen);
    rule.declarations.push_back(RuleDeclaration::primitive("sheen"));
    definition.rules.rules.push_back(rule);
}

// -- motion ------------------------------------------------------------------------

// A machine moving `property` to `hot_target` while the part is hot and to
// `rest_target` otherwise, on `curve`.
PropertyTransition hot_transition(std::string_view property, double hot_target, double rest_target,
                                  MotionCurve curve) {
    PropertyTransition transition{};
    transition.property = std::string(property);
    PresentationState hot{};
    hot.when.push_back(AnatomyState::hot);
    hot.target = hot_target;
    PresentationState rest{};
    rest.target = rest_target;
    transition.states.push_back(hot);
    transition.states.push_back(rest);
    transition.curve = curve;
    return transition;
}

// The pale face at rest, the lit face while hot (so that the face changes
// and can cross-fade).
void use_moving_faces(ThemeDefinition& definition) {
    use_material(definition, pale_face());
    ControlRoleRecipes& button = definition.roles[button_role];
    button.ordinary[hot_state].material = lit_face();
}

// Four timed easings: opacity smoothstep, translate_x ease, translate_y
// overshoot, and the face's cross-fade linear.
void motion_timed_easings(const FeatureSampleInputs&, ThemeDefinition& definition) {
    use_moving_faces(definition);
    ThemeRule rule = material_rule("motion_timed_easings", "rounded_rectangle");
    rule.transitions.push_back(hot_transition(opacity_property, 0.6, 1.0,
                                              MotionCurve::timed(milliseconds(160), MotionEasing::smoothstep)));
    rule.transitions.push_back(
        hot_transition(translate_x_property, 5.0, 0.0, MotionCurve::timed(milliseconds(200), MotionEasing::ease)));
    rule.transitions.push_back(hot_transition(translate_y_property, -3.0, 0.0,
                                              MotionCurve::timed(milliseconds(200), MotionEasing::overshoot)));
    PropertyTransition fade{};
    fade.property = std::string(face_property);
    fade.curve = MotionCurve::timed(milliseconds(160), MotionEasing::linear);
    rule.transitions.push_back(fade);
    definition.rules.rules.push_back(rule);
}

// The presentational scale on the exponential approach, with the face's
// cross-fade: a pop, both faces stretched while the scale moves.
void motion_scale_approach(const FeatureSampleInputs&, ThemeDefinition& definition) {
    use_moving_faces(definition);
    ThemeRule rule = material_rule("motion_scale_approach", "rounded_rectangle");
    rule.transitions.push_back(hot_transition(
        scale_property, 1.06, 1.0, MotionCurve::timed(milliseconds(200), MotionEasing::exponential_approach)));
    PropertyTransition fade{};
    fade.property = std::string(face_property);
    fade.curve = MotionCurve::timed(milliseconds(160), MotionEasing::linear);
    rule.transitions.push_back(fade);
    definition.rules.rules.push_back(rule);
}

// A cubic Bézier, steps at the end and at the start (the latter delayed),
// and the steps easing on a timed curve.
void motion_bezier_steps_delay(const FeatureSampleInputs&, ThemeDefinition& definition) {
    use_moving_faces(definition);
    ThemeRule rule = material_rule("motion_bezier_steps_delay", "rounded_rectangle");
    rule.transitions.push_back(hot_transition(translate_x_property, 6.0, 0.0,
                                              MotionCurve::cubic_bezier(milliseconds(200), 0.25, 0.1, 0.25, 1.0)));
    rule.transitions.push_back(hot_transition(translate_y_property, 4.0, 0.0,
                                              MotionCurve::stepped(milliseconds(200), 4U, MotionStepPosition::end)));
    const MotionCurve delayed_steps =
        MotionCurve::stepped(milliseconds(160), 3U, MotionStepPosition::start).delayed(milliseconds(48));
    rule.transitions.push_back(hot_transition(scale_property, 1.05, 1.0, delayed_steps));
    rule.transitions.push_back(
        hot_transition(opacity_property, 0.7, 1.0, MotionCurve::timed(milliseconds(160), MotionEasing::steps)));
    definition.rules.rules.push_back(rule);
}

// Multi-stop keyframes on opacity and translate_x.
void motion_keyframe_stops(const FeatureSampleInputs&, ThemeDefinition& definition) {
    use_moving_faces(definition);
    ThemeRule rule = material_rule("motion_keyframe_stops", "rounded_rectangle");
    PropertyTransition fade =
        hot_transition(opacity_property, 0.9, 1.0, MotionCurve::timed(milliseconds(240), MotionEasing::linear));
    fade.states[0].stops.push_back(MotionStop{0.3, 0.4});
    fade.states[0].stops.push_back(MotionStop{0.7, 0.7});
    rule.transitions.push_back(fade);
    PropertyTransition shake = hot_transition(translate_x_property, 6.0, 0.0,
                                              MotionCurve::timed(milliseconds(240), MotionEasing::smoothstep));
    shake.states[0].stops.push_back(MotionStop{0.5, -4.0});
    rule.transitions.push_back(shake);
    definition.rules.rules.push_back(rule);
}

// The follow and spring curves.
void motion_follow_spring(const FeatureSampleInputs&, ThemeDefinition& definition) {
    use_moving_faces(definition);
    ThemeRule rule = material_rule("motion_follow_spring", "rounded_rectangle");
    rule.transitions.push_back(hot_transition(translate_x_property, 6.0, 0.0, MotionCurve::follow()));
    rule.transitions.push_back(
        hot_transition(translate_y_property, 3.0, 0.0, MotionCurve::spring(milliseconds(150), 0.05)));
    definition.rules.rules.push_back(rule);
}

// An animated primitive parameter: the bulge's amplitude, 0.3 at rest and
// 1 while hot, shown at its keyframe levels while it moves (E7r).
void motion_animated_parameter(const FeatureSampleInputs&, ThemeDefinition& definition) {
    ThemeRule rule = subject_rule("motion_animated_parameter", PartRole::face);
    rule.declarations.push_back(RuleDeclaration::primitive("bulge"));
    rule.declarations.push_back(RuleDeclaration::number_value("amplitude", 0.3));
    PropertyTransition rise{};
    rise.property = "amplitude";
    PresentationState hot{};
    hot.when.push_back(AnatomyState::hot);
    hot.target = 1.0;
    rise.states.push_back(hot);
    rise.curve = MotionCurve::timed(milliseconds(200), MotionEasing::linear);
    rule.transitions.push_back(rise);
    definition.rules.rules.push_back(rule);
}

using Drive = FeatureSampleDrive;

constexpr EngineFeatureSample samples[] = {
    {"fill_conic", "material", PartRole::face, Drive::rest, fill_conic},
    {"fill_repeating_conic", "material", PartRole::face, Drive::rest, fill_repeating_conic},
    {"fill_repeating_radial", "material", PartRole::face, Drive::rest, fill_repeating_radial},
    {"fill_reflected_linear", "material", PartRole::face, Drive::rest, fill_reflected_linear},
    {"fill_layers", "material", PartRole::face, Drive::rest, fill_layers},
    {"image_stretched", "material", PartRole::face, Drive::rest, image_stretched},
    {"image_tiled_positioned", "material", PartRole::face, Drive::rest, image_tiled_positioned},
    {"image_nine_patch", "material", PartRole::face, Drive::rest, image_nine_patch},
    {"image_single", "material", PartRole::face, Drive::rest, image_single},
    {"procedural_felt", "material", PartRole::face, Drive::rest, procedural_felt},
    {"procedural_wood_grain", "material", PartRole::face, Drive::rest, procedural_wood_grain},
    {"procedural_paper_fibre", "material", PartRole::face, Drive::rest, procedural_paper_fibre},
    {"procedural_pinstripe", "material", PartRole::face, Drive::rest, procedural_pinstripe},
    {"procedural_brushed_metal", "material", PartRole::face, Drive::rest, procedural_brushed_metal},
    {"procedural_fresco", "material", PartRole::face, Drive::rest, procedural_fresco},
    {"procedural_checker", "material", PartRole::face, Drive::rest, procedural_checker},
    {"finish_dusty_matte", "material", PartRole::face, Drive::rest, finish_dusty_matte},
    {"finish_orange_peel", "material", PartRole::face, Drive::rest, finish_orange_peel},
    {"finish_hammertone", "material", PartRole::face, Drive::rest, finish_hammertone},
    {"finish_wrinkle", "material", PartRole::face, Drive::rest, finish_wrinkle},
    {"finish_bead_blast", "material", PartRole::face, Drive::rest, finish_bead_blast},
    {"finish_sand_cast", "material", PartRole::face, Drive::rest, finish_sand_cast},
    {"finish_flat", "material", PartRole::face, Drive::rest, finish_flat},
    {"finish_cloth_weave", "material", PartRole::face, Drive::rest, finish_cloth_weave},
    {"fill_blend_multiply", "material", PartRole::face, Drive::rest, fill_blend_multiply},
    {"fill_blend_screen", "material", PartRole::face, Drive::rest, fill_blend_screen},
    {"shape_corner_radii", "material", PartRole::face, Drive::rest, shape_corner_radii},
    {"shape_pill", "material", PartRole::face, Drive::rest, shape_pill},
    {"shape_circle", "material", PartRole::face, Drive::rest, shape_circle},
    {"shape_cap_start", "material", PartRole::face, Drive::rest, shape_cap_start},
    {"shape_cap_end", "material", PartRole::face, Drive::rest, shape_cap_end},
    {"border_dashed", "material", PartRole::face, Drive::rest, border_dashed},
    {"border_dotted", "material", PartRole::face, Drive::rest, border_dotted},
    {"border_edges", "material", PartRole::face, Drive::rest, border_edges},
    {"keylines", "material", PartRole::face, Drive::rest, keylines},
    {"shadows_outer_inset", "material", PartRole::face, Drive::rest, shadows_outer_inset},
    {"glow_under", "material", PartRole::face, Drive::rest, glow_under},
    {"glow_over", "material", PartRole::face, Drive::rest, glow_over},
    {"gloss_preset", "material", PartRole::face, Drive::rest, gloss_preset},
    {"filter_colour_matrix", "material", PartRole::face, Drive::rest, filter_colour_matrix},
    {"filter_drop_shadow", "material", PartRole::face, Drive::rest, filter_drop_shadow},
    {"filter_blur", "material", PartRole::face, Drive::rest, filter_blur},
    {"part_blend_multiply", "material", PartRole::face, Drive::rest, part_blend_multiply},
    {"part_blend_screen", "material", PartRole::face, Drive::rest, part_blend_screen},
    {"finish", "material", PartRole::face, Drive::rest, finish},
    {"mask_solid", "material", PartRole::face, Drive::rest, mask_solid},
    {"mask_linear", "material", PartRole::face, Drive::rest, mask_linear},
    {"mask_image", "material", PartRole::face, Drive::rest, mask_image},
    {"reflection_below", "material", PartRole::face, Drive::rest, reflection_below},
    {"glass_colour_filter", "glass", PartRole::region, Drive::rest, glass_colour_filter},
    {"sheen_at_pointer", "sheen", PartRole::sheen, Drive::pointer, sheen_at_pointer},
    {"motion_timed_easings", "material", PartRole::face, Drive::hot_motion, motion_timed_easings},
    {"motion_scale_approach", "material", PartRole::face, Drive::hot_motion, motion_scale_approach},
    {"motion_bezier_steps_delay", "material", PartRole::face, Drive::hot_motion, motion_bezier_steps_delay},
    {"motion_keyframe_stops", "material", PartRole::face, Drive::hot_motion, motion_keyframe_stops},
    {"motion_follow_spring", "material", PartRole::face, Drive::hot_motion, motion_follow_spring},
    {"motion_animated_parameter", "bulge", PartRole::face, Drive::hot_motion, motion_animated_parameter},
};

} // namespace

std::vector<std::byte> feature_sample_image_pixels() {
    constexpr std::uint32_t side = feature_sample_image_side;
    constexpr std::uint32_t half = side / 2U;
    std::vector<std::byte> pixels(static_cast<std::size_t>(side) * side * 4U);
    for (std::uint32_t row = 0U; row < side; ++row) {
        for (std::uint32_t column = 0U; column < side; ++column) {
            // BGRA, premultiplied: red, green, blue and white quadrants.
            std::array<std::uint8_t, 4> bgra{255U, 255U, 255U, 255U};
            if (row < half && column < half) bgra = {0U, 0U, 255U, 255U};
            if (row < half && column >= half) bgra = {0U, 255U, 0U, 255U};
            if (row >= half && column < half) bgra = {255U, 0U, 0U, 255U};
            // The ring one pixel in: grey at half alpha.
            const bool on_ring_row = (row == 1U || row == side - 2U) && column >= 1U && column <= side - 2U;
            const bool on_ring_column = (column == 1U || column == side - 2U) && row >= 1U && row <= side - 2U;
            if (on_ring_row || on_ring_column) bgra = {64U, 64U, 64U, 128U};
            const std::size_t at = (static_cast<std::size_t>(row) * side + column) * 4U;
            for (std::size_t channel = 0U; channel < 4U; ++channel) {
                pixels[at + channel] = static_cast<std::byte>(bgra[channel]);
            }
        }
    }
    return pixels;
}

std::span<const EngineFeatureSample> builtin_feature_samples() noexcept {
    const std::span<const EngineFeatureSample> listed(samples);
    return listed;
}

} // namespace gui_forms::engine_samples
