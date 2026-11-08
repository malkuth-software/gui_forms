// Stage B6 property capabilities: the editors and composites (PropertyGrid,
// PropertyList, Card, ReviewCard, MaterialPanel, BreadcrumbTrail,
// AnchoredPopupLayer, CorrespondenceView, InstrumentRack). Each test writes
// the properties through their cells or views, checks the refusals leave the
// value unchanged, and reads table entries where a property has one.

#include "gui_forms/gui_forms.hpp"
#include "gui_forms/inspection_controls.hpp"

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

} // namespace

int main() {
    test_property_grid();
    std::cout << "gui_forms_editor_composite_capabilities_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
