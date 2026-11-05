// The forced high-contrast pass (T2 plan, phase E4): the derived
// high-contrast records equal, for every existing family and surface state,
// selected or not, the records the professional theme authored by hand
// before E4 (reproduced below as the reference); the pass keeps a family's
// geometry and cues and drops gradients, images, keylines, per-edge borders
// and shadows; and Theme::create replaces authored high-contrast records.

#include "gui_forms/theme.hpp"
#include "support/theme_definition_heap.hpp"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_theme_high_contrast_tests: " << message << '\n';
    std::exit(1);
}

// -- the hand-authored records, as theme.cpp wrote them before E4 ---------------
// Since T4 phase 5d a derived record's muted text is its text and its focus
// ring is the palette's focus cue (cyan; the selection ink on an inverse
// plate): the hand-authored muted text and ring failed the 7:1 and 3:1
// gates. reference_high_contrast applies that over the old records.

SurfaceMaterial reference_surface(Color top, Color bottom, Color border, double radius) {
    SurfaceMaterial result;
    if (top == bottom) {
        result.fills = {MaterialFillLayer::solid(top)};
    } else {
        result.fills = {MaterialFillLayer::linear({0.0, 0.0}, {0.0, 1.0}, {{0.0, top}, {1.0, bottom}})};
    }
    result.border = MaterialBorder{border, 1.0};
    result.corner_radius = radius;
    return result;
}

ControlVisualRecipe reference_recipe(Color top, Color bottom, Color border, Color text, double radius) {
    ControlVisualRecipe result;
    result.material = reference_surface(top, bottom, border, radius);
    result.text = text;
    result.muted_text = Color::rgba(118, 128, 138);
    result.glyph = text;
    result.focus_ring = Color::rgba(31, 104, 185);
    result.default_ring = Color::rgba(20, 75, 145);
    return result;
}

using StateRecipes = std::array<ControlVisualRecipe, control_surface_state_count>;

std::size_t at(ControlSurfaceState state) {
    const std::size_t index = static_cast<std::size_t>(state);
    return index;
}

StateRecipes reference_high_contrast(double radius) {
    const Color black = Color::rgba(0, 0, 0);
    const Color white = Color::rgba(255, 255, 255);
    const ControlVisualRecipe normal = reference_recipe(black, black, white, white, radius);
    const ControlVisualRecipe hot = reference_recipe(black, black, Color::rgba(0, 255, 255), white, radius);
    const ControlVisualRecipe pressed = reference_recipe(white, white, Color::rgba(255, 255, 0), black, radius);
    StateRecipes result{};
    result.fill(normal);
    result[at(ControlSurfaceState::hot)] = hot;
    result[at(ControlSurfaceState::pressed)] = pressed;
    result[at(ControlSurfaceState::pending)] = hot;
    result[at(ControlSurfaceState::invalid)] =
        reference_recipe(black, black, Color::rgba(255, 96, 96), white, radius);
    result[at(ControlSurfaceState::disabled)] =
        reference_recipe(black, black, Color::rgba(128, 128, 128), Color::rgba(160, 160, 160), radius);
    for (std::size_t state = 0; state < control_surface_state_count; ++state) {
        ControlVisualRecipe& record = result[state];
        record.muted_text = record.text;
        const bool inverse = state == at(ControlSurfaceState::pressed);
        record.focus_ring = inverse ? black : Color::rgba(0, 255, 255);
    }
    return result;
}

StateRecipes reference_high_contrast_selected(double radius) {
    StateRecipes result = reference_high_contrast(radius);
    for (ControlVisualRecipe& selected : result) {
        const Color state_border =
            selected.material.border ? (*selected.material.border).color : Color::rgba(255, 255, 0);
        selected.material =
            reference_surface(Color::rgba(255, 255, 255), Color::rgba(255, 255, 255), state_border, radius);
        selected.text = Color::rgba(0, 0, 0);
        selected.glyph = Color::rgba(0, 0, 0);
        selected.muted_text = Color::rgba(0, 0, 0);
        selected.focus_ring = Color::rgba(0, 0, 0);
    }
    return result;
}

// -- tests ---------------------------------------------------------------------

void derived_records_match_the_authored_ones() {
    const std::shared_ptr<const Theme> theme = Theme::create(*test_support::professional_definition());
    const ThemeDefinition& definition = (*theme).definition();
    std::size_t compared = 0U;
    for (std::size_t family = 0; family < control_visual_role_count; ++family) {
        const ControlRoleRecipes& role = definition.roles[family];
        const double radius = role.ordinary[at(ControlSurfaceState::normal)].material.corner_radius;
        StateRecipes expected = reference_high_contrast(radius);
        StateRecipes expected_selected = reference_high_contrast_selected(radius);
        const ControlVisualRole visual = static_cast<ControlVisualRole>(family);
        if (visual == ControlVisualRole::window || visual == ControlVisualRole::panel ||
            visual == ControlVisualRole::card || visual == ControlVisualRole::choice ||
            visual == ControlVisualRole::editor) {
            // A container, a choice or an editor never inverts (phase 5e): pressed is the plain
            // record inside the pressed edge, selected the state's record
            // inside the selection line.
            ControlVisualRecipe& pressed = expected[at(ControlSurfaceState::pressed)];
            pressed = reference_recipe(Color::rgba(0, 0, 0), Color::rgba(0, 0, 0), Color::rgba(255, 255, 0),
                                       Color::rgba(255, 255, 255), radius);
            pressed.muted_text = pressed.text;
            pressed.focus_ring = Color::rgba(0, 255, 255);
            for (std::size_t state = 0; state < control_surface_state_count; ++state) {
                expected_selected[state] = expected[state];
                (*expected_selected[state].material.border).color = Color::rgba(0, 255, 255);
            }
        }
        for (std::size_t state = 0; state < control_surface_state_count; ++state) {
            require(role.high_contrast[state] == expected[state],
                    "every family's derived high-contrast record equals the hand-authored one");
            require(role.high_contrast_selected[state] == expected_selected[state],
                    "and so does every selected one");
            compared += 2U;
        }
    }
    require(compared == control_visual_role_count * control_surface_state_count * 2U,
            "all twelve families, seven states, ordinary and selected, are compared");
    ControlVisualContext context{};
    context.high_contrast = true;
    context.surface = ControlSurfaceState::pressed;
    const ControlVisualRecipe& resolved = (*theme).resolve(ControlVisualRole::button, context);
    require(resolved == reference_high_contrast(3.0)[at(ControlSurfaceState::pressed)],
            "Theme::resolve serves the derived record");
}

void the_pass_keeps_geometry_and_drops_effects() {
    const std::unique_ptr<ThemeDefinition> held_definition = test_support::professional_definition();
    ThemeDefinition& definition = *held_definition;
    definition.id = "effects";
    ControlRoleRecipes& card = definition.roles[static_cast<std::size_t>(ControlVisualRole::card)];
    ControlVisualRecipe& ordinary = card.ordinary[at(ControlSurfaceState::hot)];
    ordinary.material.border = MaterialBorder{Color::rgba(1, 2, 3), 2.0};
    ordinary.material.corner_radius = 9.0;
    ordinary.material.shadows.push_back(MaterialShadow{});
    ordinary.material.keylines.push_back(MaterialKeyline{});
    ordinary.focus_width = 2.0;
    ordinary.visual_offset = Point{0.0, -1.0};
    // Authored high-contrast records are replaced by the pass.
    card.high_contrast[at(ControlSurfaceState::hot)].text = Color::rgba(9, 9, 9);
    const std::shared_ptr<const Theme> theme = Theme::create(definition);
    const ControlVisualRecipe& derived =
        (*theme).definition().roles[static_cast<std::size_t>(ControlVisualRole::card)]
            .high_contrast[at(ControlSurfaceState::hot)];
    require(derived.material.corner_radius == 9.0 && derived.material.border &&
                (*derived.material.border).width == 2.0 && derived.focus_width == 2.0 &&
                derived.visual_offset == Point{0.0, -1.0},
            "the pass keeps the shape, the edge width and the cues");
    require(derived.material.shadows.empty() && derived.material.keylines.empty() &&
                derived.material.fills.size() == 1U &&
                derived.material.fills[0] == MaterialFillLayer::solid(Color::rgba(0, 0, 0)) &&
                (*derived.material.border).color == Color::rgba(0, 255, 255),
            "and draws one solid plate in one solid edge, without shadows or keylines");
    require(derived.text == Color::rgba(255, 255, 255), "an authored high-contrast record is replaced");
    const ControlRoleRecipes direct = derive_high_contrast_recipes(card, ControlVisualRole::card);
    require(direct.high_contrast[at(ControlSurfaceState::hot)] == derived,
            "derive_high_contrast_recipes is the pass Theme::create applies");
}

} // namespace

int main() {
    derived_records_match_the_authored_ones();
    the_pass_keeps_geometry_and_drops_effects();
    std::cout << "theme high contrast: the derived pass equals the hand-authored records for every family, keeps "
                 "geometry and drops effects passed\n";
    return EXIT_SUCCESS;
}
