// W-4 display controls: Badge (and, as they land, InfoBanner and
// EmptyState). Each test covers the control's behaviour, its refusals, its
// semantics, its published parts and states, and the capability cases: a
// write stores, an equal write does nothing, and a write from another
// thread is refused before anything is compared or stored.

#include "gui_forms/gui_forms.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_display_controls_tests: " << message << '\n';
    std::exit(1);
}

// Writes a badge's count from another thread and records whether it was
// refused.
struct OtherThreadCount final {
    Badge* badge{};
    bool* refused{};
    void operator()() const {
        try {
            (*badge).count = std::uint32_t{5U};
        } catch (const std::logic_error&) {
            *refused = true;
        }
    }
};

void test_badge_text_and_visibility() {
    std::shared_ptr<Badge> badge = make_control<Badge>(StableId("display.badge"));
    require(!(*badge).showing() && (*badge).display_text().empty(), "a zero count hides the badge");
    require((*badge).measure(Size{200.0, 50.0}) == (Size{0.0, 0.0}), "a hidden badge measures empty");
    (*badge).show_zero = true;
    require((*badge).showing() && (*badge).display_text() == "0", "show_zero shows a zero count");
    (*badge).count = std::uint32_t{42U};
    require((*badge).display_text() == "42", "a count up to the maximum is shown as it is");
    (*badge).count = std::uint32_t{100U};
    require((*badge).display_text() == "99+", "a count past the maximum shows the maximum and +");
    (*badge).maximum = std::uint32_t{9U};
    require((*badge).display_text() == "9+", "a smaller maximum caps sooner");
    bool refused = false;
    try {
        (*badge).maximum = std::uint32_t{0U};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*badge).maximum.read() == 9U, "a zero maximum is refused and the old one kept");
    (*badge).mode = BadgeMode::dot;
    require((*badge).showing() && (*badge).display_text().empty(), "dot mode shows a dot without text");
    const Size dot = (*badge).measure(Size{200.0, 50.0});
    require(dot.width > 0.0 && dot.width == dot.height, "a dot is square");
    const std::optional<BindingValue> by_name = (*badge).property_value("count");
    require(by_name && *by_name == BindingValue{std::uint64_t{100U}}, "the table reads the count cell");
    (*badge).set_property_value("mode", BindingValue{PropertyEnumValue{"badge_mode", "count", 0}});
    require((*badge).mode.read() == BadgeMode::count, "the table writes the mode cell");
}

void test_badge_semantics_parts_and_threads() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("display.root"));
    std::shared_ptr<Badge> badge = make_control<Badge>(StableId("display.badge.attached"));
    (*badge).set_requested_bounds(Rect{10.0, 10.0, 60.0, 24.0});
    (*root).add_child(badge);
    Window window(root, Size{200.0, 100.0});
    window.perform_layout();

    SemanticDescriptor hidden = (*badge).semantic_descriptor();
    require(hidden.role == SemanticRole::static_text && !hidden.exposed, "a hidden badge is not exposed");
    require(!(*badge).parts().chip.present && !(*badge).parts().label.present, "a hidden badge publishes no chip");

    (*badge).count = std::uint32_t{3U};
    window.perform_layout();
    const SemanticDescriptor shown = (*badge).semantic_descriptor();
    require(shown.exposed && shown.name == "3", "a shown badge is named by its text");
    (*badge).accessible_name = "3 new alerts";
    require((*badge).semantic_descriptor().name == "3 new alerts", "an accessible name wins over the text");
    const Badge::Parts counted = (*badge).parts();
    require(counted.chip.present && counted.label.present, "a count badge publishes its chip and label");

    const std::optional<CustomStateIndex> dot = find_custom_state((*badge).anatomy(), "dot");
    require(dot && !(*badge).anatomy_states().contains(*dot), "count mode is not the dot state");
    (*badge).mode = BadgeMode::dot;
    window.perform_layout();
    require((*badge).anatomy_states().contains(*dot), "dot mode publishes the dot state");
    const Badge::Parts dotted = (*badge).parts();
    require(dotted.chip.present && !dotted.label.present, "a dot publishes a chip without a label");
    require(!(*badge).hit_test_local(Point{4.0, 4.0}), "a badge takes no pointer input");

    bool refused = false;
    std::thread worker(OtherThreadCount{badge.get(), &refused});
    worker.join();
    require(refused && (*badge).count.read() == 3U, "a count write from another thread is refused");
}

} // namespace

int main() {
    test_badge_text_and_visibility();
    test_badge_semantics_parts_and_threads();
    std::cout << "display control tests passed\n";
    return EXIT_SUCCESS;
}
