#pragma once

#include "gui_forms/theme.hpp"
#include "gui_forms/window.hpp"

#include <memory>

namespace gui_forms::layered_material_lab {

// Deterministic File Manager material specimens. These are demo recipes over
// public GUI.Forms vocabulary, not framework-owned application theme policy.
[[nodiscard]] SurfaceMaterial watercolor_fresco_material();
[[nodiscard]] SurfaceMaterial office_pearl_material();
[[nodiscard]] SurfaceMaterial workshop_graphite_material();
[[nodiscard]] SurfaceMaterial studio_caption_material(ImageId nine_patch_image);
[[nodiscard]] ControlStateRecipes office_pearl_state_recipes();
[[nodiscard]] bool over_budget_recipe_is_rejected();

[[nodiscard]] std::unique_ptr<Window> make_layered_material_lab();

} // namespace gui_forms::layered_material_lab
