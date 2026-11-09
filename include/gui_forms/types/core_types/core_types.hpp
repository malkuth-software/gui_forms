#pragma once

// Compatibility family umbrella. The underlying contracts remain independently
// includable so geometry-only users do not import Painter or damage storage.
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/paint_marks/paint_marks.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/types/paint_recipes/paint_recipes.hpp"
#include "gui_forms/types/damage_region/damage_region.hpp"
