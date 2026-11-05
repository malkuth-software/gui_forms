// What a window resize measures again (stage L, L7 follow-up). A resize
// arranges, paints and hit-tests every control again, but measures again
// only the controls whose measure input the client size reaches: the root,
// and below it each control whose ancestors request no extent on an axis
// that changed. The window's measure pass hands every other control the
// same available size as before. The counts are taken at 10,000 labels.

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/container_controls.hpp"
#include "gui_forms/window.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gui_forms;

constexpr std::size_t label_count = 10'000U;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// A label that counts its measure callbacks, from any caller.
class CountingLabel final : public Label {
public:
    CountingLabel(StableId stable_id, std::uint64_t& measures)
        : Label(std::move(stable_id), "Item"), measures_(measures) {}

    Size measure(Size available) override {
        ++measures_;
        const Size measured = Label::measure(available);
        return measured;
    }

private:
    std::uint64_t& measures_;
};

// Adds label_count labels to `parent`, each at its own requested bounds in
// a grid of 100 columns, so no label's size comes from its text.
std::shared_ptr<CountingLabel> add_labels(Control& parent, const std::string& prefix,
                                          std::uint64_t& measures) {
    std::shared_ptr<CountingLabel> last;
    for (std::size_t index = 0U; index < label_count; ++index) {
        const double column = static_cast<double>(index % 100U);
        const double row = static_cast<double>(index / 100U);
        std::shared_ptr<CountingLabel> label = make_control<CountingLabel>(
            StableId(prefix + std::to_string(index)), measures);
        (*label).set_requested_bounds({column * 8.0, row * 6.0, 8.0, 6.0});
        parent.add_child(label);
        last = label;
    }
    return last;
}

struct ResizeCounts final {
    std::uint64_t label_measures{};
    std::uint64_t window_measures{};
};

ResizeCounts resize_and_count(Window& window, Size client, std::uint64_t& measures) {
    const std::uint64_t measures_before = measures;
    const std::uint64_t window_before = window.metrics_snapshot().controls_measured;
    window.resize(client);
    window.perform_layout();
    ResizeCounts counts;
    counts.label_measures = measures - measures_before;
    counts.window_measures = window.metrics_snapshot().controls_measured - window_before;
    return counts;
}

// Labels in a panel with a requested size: the panel hands them its own
// extent, which a resize does not change. They are not measured again,
// but they are still arranged: the panel is anchored to all four edges, so
// it stretches, and a label anchored to its right edge moves with it.
void test_sized_panel_keeps_its_labels_measures() {
    std::uint64_t measures = 0U;
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    std::shared_ptr<Panel> sized = make_control<Panel>(StableId("sized"));
    (*sized).set_requested_bounds({0.0, 0.0, 800.0, 600.0});
    (*sized).anchor = AnchorStyles::top | AnchorStyles::bottom | AnchorStyles::left |
                        AnchorStyles::right;
    (*root).add_child(sized);
    const std::shared_ptr<CountingLabel> last = add_labels(*sized, "sized.", measures);
    (*last).anchor = AnchorStyles::top | AnchorStyles::right;
    const std::unique_ptr<Window> held = std::make_unique<Window>(root, Size{1000.0, 700.0});
    Window& window = *held;
    window.perform_layout();
    const double right_before = (*last).absolute_bounds().x;

    const ResizeCounts counts = resize_and_count(window, Size{1200.0, 900.0}, measures);
    std::cout << "window-resize-measure-tests: " << label_count
              << " labels in a sized panel: " << counts.label_measures
              << " label measures, " << counts.window_measures
              << " window measure callbacks on a resize\n";
    require(counts.label_measures == 0U, "a resize measured labels whose input did not change");
    // The root and the sized panel: the root is handed the client size, and
    // the panel is handed the root's (the root requests no extent).
    require(counts.window_measures == 2U, "a resize measured more than the root and the panel");
    require((*last).absolute_bounds().x == right_before + 200.0,
            "an anchored label was not arranged again after a resize");
}

// Labels directly under the root are handed the client size, so a resize
// changes their input and every one of them is measured again.
void test_labels_reached_by_the_client_size_are_measured() {
    std::uint64_t measures = 0U;
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    static_cast<void>(add_labels(*root, "root.", measures));
    const std::unique_ptr<Window> held = std::make_unique<Window>(root, Size{1000.0, 700.0});
    Window& window = *held;
    window.perform_layout();

    const ResizeCounts counts = resize_and_count(window, Size{1200.0, 900.0}, measures);
    std::cout << "window-resize-measure-tests: " << label_count
              << " labels under the root: " << counts.label_measures
              << " label measures on a resize\n";
    require(counts.label_measures == label_count,
            "a resize skipped labels whose available size changed");
}

// The rule is per axis: a panel that requests only a width hands its
// labels the client height, so a height change reaches them and a width
// change does not.
void test_reach_is_per_axis() {
    std::uint64_t measures = 0U;
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    std::shared_ptr<Panel> column = make_control<Panel>(StableId("column"));
    (*column).set_requested_bounds({0.0, 0.0, 800.0, 0.0});
    (*root).add_child(column);
    static_cast<void>(add_labels(*column, "column.", measures));
    const std::unique_ptr<Window> held = std::make_unique<Window>(root, Size{1000.0, 700.0});
    Window& window = *held;
    window.perform_layout();

    const ResizeCounts wider = resize_and_count(window, Size{1200.0, 700.0}, measures);
    require(wider.label_measures == 0U, "a width change reached labels under a fixed width");
    const ResizeCounts taller = resize_and_count(window, Size{1200.0, 900.0}, measures);
    require(taller.label_measures == label_count,
            "a height change did not reach labels under an unrequested height");
    std::cout << "window-resize-measure-tests: per axis: width change "
              << wider.label_measures << ", height change " << taller.label_measures
              << " label measures\n";
}

} // namespace

int main() {
    try {
        test_sized_panel_keeps_its_labels_measures();
        test_labels_reached_by_the_client_size_are_measured();
        test_reach_is_per_axis();
    } catch (const std::exception& error) {
        std::cerr << "window-resize-measure-tests: FAILED: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "window-resize-measure-tests: passed\n";
    return EXIT_SUCCESS;
}
