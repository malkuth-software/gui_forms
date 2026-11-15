// The ImGui-class demonstrator's I9: FormLayout's labelled rows. The
// caption column (fixed and widest-caption), row heights from add_row and
// from the control's measure, the spacing properties, hidden and empty
// rows, captions naming their controls, caption alignment, and refused
// arguments that change nothing.

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/container_controls.hpp"
#include "gui_forms/window.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

using namespace gui_forms;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "form layout test failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool near(double left, double right) {
    const bool result = std::fabs(left - right) < 1e-9;
    return result;
}

std::shared_ptr<Panel> box(const std::string& id, double height) {
    std::shared_ptr<Panel> made = make_control<Panel>(StableId(id));
    (*made).set_bounds({0.0, 0.0, 10.0, height});
    return made;
}

bool refuses_row(FormLayout& form, std::string caption, Control::Ptr control, double height) {
    try {
        static_cast<void>(form.add_row(std::move(caption), std::move(control), height));
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

void test_rows_and_columns() {
    std::shared_ptr<FormLayout> form = make_control<FormLayout>(StableId("form"));
    (*form).caption_width = 100.0;
    (*form).row_spacing = 10.0;
    (*form).column_spacing = 12.0;
    std::shared_ptr<Panel> first = box("form.first", 40.0);
    std::shared_ptr<Panel> second = box("form.second", 5.0);
    std::shared_ptr<Panel> third = box("form.third", 50.0);
    const std::shared_ptr<Label> first_caption = (*form).add_row("Name", first, 0.0);
    const std::shared_ptr<Label> second_caption = (*form).add_row("Size", second, 30.0);
    static_cast<void>((*form).add_row("Notes", third, 0.0));
    require((*form).row_count() == 3U, "three rows");
    require((*first_caption).stable_id().value() == "form.first.caption", "the caption's ID derives from its control's");
    require((*form).caption_of(*second) == second_caption, "caption_of finds the row's caption");
    require((*first).accessible_name.read() == "Name", "the caption names an unnamed control");

    const std::unique_ptr<Window> held = std::make_unique<Window>(form, Size{400.0, 300.0});
    Window& window = *held;
    window.perform_layout();
    require(near((*form).used_caption_width(), 100.0), "the fixed caption width is used");
    const Rect first_slot = (*first).committed_arranged_bounds();
    const Rect second_slot = (*second).committed_arranged_bounds();
    const Rect third_slot = (*third).committed_arranged_bounds();
    require(near(first_slot.x, 112.0) && near(first_slot.width, 288.0), "controls take the rest of the width");
    require(near(first_slot.y, 0.0) && near(first_slot.height, 40.0), "a measured row is the control's height");
    require(near(second_slot.y, 50.0), "rows are spaced by row_spacing");
    require(second_slot.height >= 30.0, "a given height is used (or the caption's, if taller)");
    require(near(third_slot.y, second_slot.y + second_slot.height + 10.0), "the third row follows");
    const Rect caption_slot = (*first_caption).committed_arranged_bounds();
    require(near(caption_slot.x, 0.0) && near(caption_slot.width, 100.0) && near(caption_slot.height, 40.0),
            "the caption fills its cell");
    const Size measured = (*form).measure({400.0, 300.0});
    require(near(measured.height, third_slot.y + third_slot.height), "the measure is the rows' total");

    // A hidden control's row takes no space; shown again, it is back.
    (*second).visible = false;
    window.perform_layout();
    require(near((*third).committed_arranged_bounds().y, 50.0), "a hidden row takes no space");
    (*second).visible = true;
    window.perform_layout();
    require(near((*third).committed_arranged_bounds().y, third_slot.y), "a shown row is back");

    // The widest caption sizes the column when caption_width is 0.
    (*form).caption_width = 0.0;
    window.perform_layout();
    const double widest = (*form).used_caption_width();
    require(widest > 0.0, "the widest caption sizes the column");
    require(near((*first).committed_arranged_bounds().x, widest + 12.0), "controls follow the measured column");

    (*form).caption_alignment = HorizontalAlignment::far;
    require((*first_caption).alignment.read() == HorizontalAlignment::far, "captions follow caption_alignment");

    // A control removed other than by remove_row leaves an empty row.
    static_cast<void>((*form).remove_child((*third).runtime_id()));
    window.perform_layout();
    require((*form).row_count() == 3U, "the empty row stays until forgotten");
    require((*form).forget_empty_rows() == 1U, "forget_empty_rows drops it");
    require((*form).row_count() == 2U, "two rows remain");

    require((*form).remove_row(*second), "remove_row removes a row");
    require(!(*form).remove_row(*second), "a removed row is gone");
    require((*second).parent() == nullptr, "its control left the layout");
    require((*second_caption).parent() == nullptr, "its caption left the layout");
    require((*form).row_count() == 1U, "one row remains");
}

void test_refusals_change_nothing() {
    std::shared_ptr<FormLayout> form = make_control<FormLayout>(StableId("refuse"));
    require(refuses_row(*form, "A", nullptr, 0.0), "a null control is refused");
    require(refuses_row(*form, "A", box("refuse.a", 10.0), -1.0), "a negative height is refused");
    require(refuses_row(*form, "A", box("refuse.b", 10.0), std::nan("")), "a NaN height is refused");
    std::shared_ptr<Panel> owned = box("refuse.c", 10.0);
    std::shared_ptr<Panel> parent = make_control<Panel>(StableId("refuse.parent"));
    (*parent).add_child(owned);
    require(refuses_row(*form, "A", owned, 0.0), "a control with a parent is refused");
    require((*form).row_count() == 0U && (*form).children().empty(), "refusals change nothing");
    bool refused = false;
    try {
        (*form).row_spacing = -1.0;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && near((*form).row_spacing.read(), 6.0), "a negative row spacing is refused");
}

} // namespace

int main() {
    test_rows_and_columns();
    test_refusals_change_nothing();
    std::cout << "form layout tests passed\n";
    return EXIT_SUCCESS;
}
