// Stage B6 property capabilities: the editors and composites (PropertyGrid,
// PropertyList, Card, ReviewCard, MaterialPanel, BreadcrumbTrail,
// AnchoredPopupLayer, CorrespondenceView, InstrumentRack). Each test writes
// the properties through their cells or views, checks the refusals leave the
// value unchanged, and reads table entries where a property has one.

#include "gui_forms/gui_forms.hpp"
#include "gui_forms/inspection_controls.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_editor_composite_capabilities_tests: " << message << '\n';
    std::exit(1);
}

void test_property_grid() {
    std::shared_ptr<PropertyGrid> grid = make_control<PropertyGrid>(StableId("caps.grid"));
    require((*grid).property_sort.read() == PropertySort::categorized, "the grid starts categorized");
    (*grid).property_sort = PropertySort::alphabetical;
    require((*grid).property_sort.read() == PropertySort::alphabetical, "a sort write is read back");
    (*grid).set_property_value("property_sort", BindingValue{PropertyEnumValue{"property_sort", "categorized", 0}});
    require((*grid).property_sort.read() == PropertySort::categorized, "a table write reaches the view");
    const std::shared_ptr<PropertyValueConverterRegistry> converters = (*grid).converter_registry.read();
    bool refused = false;
    try {
        (*grid).converter_registry = std::shared_ptr<PropertyValueConverterRegistry>{};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*grid).converter_registry.read() == converters, "a null converter registry is refused");
    refused = false;
    try {
        (*grid).editor_registry = std::shared_ptr<PropertyEditorRegistry>{};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*grid).editor_registry.read(), "a null editor registry is refused");
}

void test_property_list() {
    std::shared_ptr<PropertyList> list = make_control<PropertyList>(StableId("caps.list"));
    (*list).label_width = 120.0;
    (*list).row_height = 30.0;
    require((*list).label_width.read() == 120.0 && (*list).row_height.read() == 30.0,
            "layout writes are read back");
    bool refused = false;
    try {
        (*list).label_width = 20.0;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*list).label_width.read() == 120.0, "a label column under 40 is refused");
    refused = false;
    try {
        (*list).header_height = 40.0;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*list).header_height.read() == 0.0, "a header height without header content is refused");
    (*list).scroll_offset = 500.0;
    require((*list).scroll_offset.read() == 0.0, "a scroll write is clamped to the overflow (none here)");
    const FontSpec larger{FontRole::content, 15.0, 400, false};
    (*list).font = larger;
    require((*list).font.read() == larger, "a font write overrides the theme's");
}

// Counts selected_changed payloads.
struct CountSelected final {
    int* count{};
    void operator()(bool) const { ++*count; }
};

void test_cards() {
    std::shared_ptr<Card> card = make_control<Card>(StableId("caps.card"));
    int changes = 0;
    const SubscriptionToken token = (*card).selected_changed().subscribe(CountSelected{&changes});
    (*card).selected = true;
    (*card).selected = true;
    require((*card).selected.read() && changes == 1, "selected raises selected_changed once per change");
    (*card).interactive = true;
    require((*card).interactive.read() && (*card).focusable(), "an interactive card takes focus");
    bool refused = false;
    try {
        (*card).selection_behavior = static_cast<CardSelectionBehavior>(9);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*card).selection_behavior.read() == CardSelectionBehavior::manual,
            "an undeclared selection behavior is refused");
    CardLayout layout;
    layout.section_gap = 4.0;
    (*card).card_layout = layout;
    require(!(*card).uses_theme_layout() && (*card).card_layout.read() == layout,
            "a layout write leaves the theme's layout");
    static_cast<void>(token);

    std::shared_ptr<ReviewCard> review = make_control<ReviewCard>(StableId("caps.review"));
    (*review).record = ReviewRecord{"k", "Title", "Summary", "Verdict", ReviewDisposition::pending};
    require((*review).record.read().title == "Title" && (*review).accessible_name.read() == "Title",
            "a record write projects onto the accessibility text");

    std::shared_ptr<MaterialPanel> panel = make_control<MaterialPanel>(StableId("caps.material"));
    const SurfaceMaterial before = (*panel).material.read();
    SurfaceMaterial broken = before;
    broken.corner_radius = -1.0;
    refused = false;
    try {
        (*panel).material = broken;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*panel).material.read() == before, "an invalid material is refused");
}

void test_breadcrumb_trail() {
    std::shared_ptr<BreadcrumbTrail> trail = make_control<BreadcrumbTrail>(StableId("caps.trail"));
    (*trail).set_segments({{"root", "Home", ""}, {"docs", "Documents", ""}});
    bool refused = false;
    try {
        (*trail).appearance = static_cast<BreadcrumbAppearance>(9);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*trail).appearance.read() == BreadcrumbAppearance::plain,
            "an undeclared appearance is refused");
    (*trail).appearance = BreadcrumbAppearance::raised;
    require((*trail).appearance.read() == BreadcrumbAppearance::raised, "appearance stores a declared value");

    refused = false;
    try {
        (*trail).active_id = std::string("missing");
    } catch (const std::out_of_range&) {
        refused = true;
    }
    require(refused, "an active ID outside the model is refused");

    (*trail).tab_completion_available = true;
    require(!(*trail).tab_completion_available.read(), "completion stays off outside an edit");
    (*trail).editing = true;
    require((*(*trail).editor()).visible.read(), "editing shows the path editor");
    (*trail).tab_completion_available = true;
    require((*trail).tab_completion_available.read(), "completion holds while editing");
    (*trail).editing = false;
    require(!(*trail).tab_completion_available.read() && !(*(*trail).editor()).visible.read(),
            "ending the edit withdraws completion and hides the editor");

    const FontSpec larger{FontRole::control, 13.0, 400, false};
    (*trail).font = larger;
    require((*(*trail).editor()).font.read() == larger, "the editor follows the trail's font");
    refused = false;
    try {
        (*trail).font = FontSpec{FontRole::control, -1.0, 400, false};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*trail).font.read() == larger, "an invalid font is refused");
}

void test_anchored_overlay_layer() {
    std::shared_ptr<Panel> anchor = make_control<Panel>(StableId("caps.anchor"));
    AnchoredOverlayPlacement initial{};
    initial.gap = 3.0;
    std::shared_ptr<AnchoredOverlayLayer> layer =
        make_control<AnchoredOverlayLayer>(StableId("caps.layer"), anchor, initial);
    require((*layer).placement.read() == initial, "the constructor's placement is stored");
    AnchoredOverlayPlacement broken = initial;
    broken.gap = -1.0;
    bool refused = false;
    try {
        (*layer).placement = broken;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*layer).placement.read() == initial, "a negative gap is refused");
    require((*layer).dismiss_on_click_away.read() && (*layer).dismiss_on_escape.read(),
            "both dismissals are reported by default");
    (*layer).dismiss_on_escape = false;
    require(!(*layer).dismiss_on_escape.read() && (*layer).dismiss_on_click_away.read(),
            "the dismissals are independent");
}

void test_correspondence_view() {
    std::shared_ptr<CorrespondenceView> view = make_control<CorrespondenceView>(StableId("caps.correspondence"));
    bool refused = false;
    try {
        (*view).selected_id = std::string("missing");
    } catch (const std::out_of_range&) {
        refused = true;
    }
    require(refused && (*view).selected_id.read().empty(), "a selection outside the model is refused");
    refused = false;
    try {
        (*view).compact_height = (*view).expanded_height.read();
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*view).compact_height.read() == 45.0, "a compact height not below the expanded height is refused");
    (*view).expanded_height = 140.0;
    (*view).compact_height = 50.0;
    require((*view).expanded_height.read() == 140.0 && (*view).compact_height.read() == 50.0,
            "row heights store within their bounds");
    refused = false;
    try {
        (*view).status_rail_width = 13.0;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*view).status_rail_width.read() == 4.0, "a rail wider than 12 is refused");
    refused = false;
    try {
        (*view).hover_intent_delay = std::chrono::milliseconds(0);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*view).hover_intent_delay.read() == std::chrono::milliseconds(180),
            "a zero hover delay is refused");
    refused = false;
    try {
        (*view).scroll_offset = -1.0;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    (*view).scroll_offset = 30.0;
    require(refused && (*view).scroll_offset.read() == 0.0, "an empty view clamps its scroll offset to zero");
    const FontSpec larger{FontRole::content, 13.0, 400, false};
    (*view).font = larger;
    require((*view).font.read() == larger, "the font stores");
}

} // namespace

int main() {
    test_property_grid();
    test_property_list();
    test_cards();
    test_breadcrumb_trail();
    test_anchored_overlay_layer();
    test_correspondence_view();
    std::cout << "gui_forms_editor_composite_capabilities_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
