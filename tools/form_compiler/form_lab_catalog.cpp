// The Gallery's form compiler's extension types (audit F9): the lab
// library's controls (lab/include/gui_forms_lab, the optional gui_forms_lab
// library a product uses) and the demonstration controls
// (demo/lab_controls/include/gui_forms_demo), which left the installed
// library and its registry. The Gallery's pages still name them, so the
// compiler that generates those pages, and the tests that read documents
// naming them, link this catalog with gui_forms_lab and
// gui_forms_demo_controls.

#include "form_catalog.hpp"

#include "gui_forms_demo/easing_preview.hpp"
#include "gui_forms_demo/typography_specimen.hpp"
#include "gui_forms_demo/visual_inspector_view.hpp"
#include "gui_forms_lab/correspondence_view.hpp"
#include "gui_forms_lab/instrument_rack.hpp"
#include "gui_forms_lab/review_card.hpp"

#include <array>

namespace gui_forms::form_compiler {

namespace {

// In the order the registry listed them before the move.
constexpr std::array<FormCatalogEntry, 7> lab_catalog{{
    {"review_card", "ReviewCard", "gui_forms_lab/review_card.hpp", &add_catalog_type<ReviewCard>},
    {"correspondence_view", "CorrespondenceView", "gui_forms_lab/correspondence_view.hpp",
     &add_catalog_type<CorrespondenceView>},
    {"instrument_rack", "InstrumentRack", "gui_forms_lab/instrument_rack.hpp", &add_catalog_type<InstrumentRack>},
    {"easing_preview", "EasingPreview", "gui_forms_demo/easing_preview.hpp", &add_catalog_type<EasingPreview>},
    {"typography_specimen", "TypographySpecimen", "gui_forms_demo/typography_specimen.hpp",
     &add_catalog_type<TypographySpecimen>},
    {"visual_inspector_view", "VisualInspectorView", "gui_forms_demo/visual_inspector_view.hpp",
     &add_catalog_type<VisualInspectorView>},
    {"visual_inspector_overlay", "VisualInspectorOverlay", "gui_forms_demo/visual_inspector_view.hpp",
     &add_catalog_type<VisualInspectorOverlay>},
}};

} // namespace

void add_extension_types(form_language::FormSchema& schema) {
    schema.types.reserve(schema.types.size() + lab_catalog.size() + 1U);
    for (const FormCatalogEntry& entry : lab_catalog) {
        (*entry.add)(entry, schema);
    }
}

} // namespace gui_forms::form_compiler
