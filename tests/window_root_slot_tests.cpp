// The root slot (stage B, B.2a of the window-is-form study): the client
// rectangle less the chrome insets. The layout pass measures the root at
// the slot's size and arranges it there; a resize keeps the insets; the
// inset band hits no control; the semantic tree places the root in the
// slot; refused insets change nothing. Zero insets are today's layout.

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/container_controls.hpp"
#include "gui_forms/window.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// A panel that records the size the layout pass hands it.
class SlotProbe final : public Panel {
public:
    explicit SlotProbe(StableId stable_id) : Panel(std::move(stable_id)) {}

    Size measure(Size available) override {
        handed = available;
        const Size measured = Panel::measure(available);
        return measured;
    }

    Size handed{};
};

// The node with `stable_id` among `nodes` and their descendants, or null.
const SemanticNode* find_node(const std::vector<SemanticNode>& nodes, std::string_view stable_id) {
    for (const SemanticNode& node : nodes) {
        if (node.stable_id == stable_id) return &node;
        const SemanticNode* found = find_node(node.children, stable_id);
        if (found != nullptr) return found;
    }
    return nullptr;
}
const SemanticNode* find_node(std::vector<SemanticNode>&& nodes, std::string_view stable_id) = delete;

// Whether setting `insets` throws std::invalid_argument and keeps the
// window's insets.
bool refused(Window& window, Insets insets) {
    const Insets before = window.chrome_insets();
    bool thrown = false;
    try {
        window.set_chrome_insets(insets);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    return thrown && window.chrome_insets() == before;
}

void test_root_in_its_slot() {
    std::shared_ptr<SlotProbe> root = make_control<SlotProbe>(StableId("slot.root"));
    std::shared_ptr<Button> child = make_control<Button>(StableId("slot.child"));
    (*child).set_requested_bounds({0.0, 0.0, 20.0, 20.0});
    (*root).add_child(child);
    Window window(root, Size{200.0, 100.0});
    window.perform_layout();
    require(window.root_slot() == Rect{0.0, 0.0, 200.0, 100.0}, "no insets: the slot is the client area");
    require((*root).committed_arranged_bounds() == Rect{0.0, 0.0, 200.0, 100.0},
            "no insets: the root is arranged at the origin, as before");

    // left, top, right, bottom
    window.set_chrome_insets(Insets{10.0, 20.0, 30.0, 40.0});
    window.perform_layout();
    require(window.root_slot() == Rect{10.0, 20.0, 160.0, 40.0}, "insets: the slot is the client less the band");
    require((*root).committed_arranged_bounds() == Rect{10.0, 20.0, 160.0, 40.0},
            "insets: the root is arranged in the slot");
    require((*root).handed == Size{160.0, 40.0}, "insets: the root is measured at the slot's size");
    require((*child).absolute_bounds() == Rect{10.0, 20.0, 20.0, 20.0}, "insets: a child is placed in the root");

    require(window.hit_test(Point{5.0, 50.0}) == nullptr, "the left band hits no control");
    require(window.hit_test(Point{100.0, 90.0}) == nullptr, "the bottom band hits no control");
    require(window.hit_test(Point{100.0, 50.0}) == root, "inside the slot, the root");
    require(window.hit_test(Point{12.0, 22.0}) == child, "inside the slot, the child");

    const SemanticSnapshot snapshot = window.semantic_snapshot();
    const SemanticNode* child_node = find_node(snapshot.roots, "slot.child");
    require(child_node != nullptr && (*child_node).bounds == Rect{10.0, 20.0, 20.0, 20.0},
            "the child's semantic node is placed in the slot");

    window.resize(Size{300.0, 200.0});
    window.perform_layout();
    require((*root).committed_arranged_bounds() == Rect{10.0, 20.0, 260.0, 140.0},
            "a resize keeps the insets");
    require((*root).handed == Size{260.0, 140.0}, "a resize measures the root at the new slot");

    // Insets that cover an axis leave an empty slot on it.
    window.set_chrome_insets(Insets{150.0, 0.0, 200.0, 0.0});
    window.perform_layout();
    require(window.root_slot() == Rect{150.0, 0.0, 0.0, 200.0}, "covering insets: an empty width");

    window.set_chrome_insets(Insets{});
    window.perform_layout();
    require((*root).committed_arranged_bounds() == Rect{0.0, 0.0, 300.0, 200.0},
            "insets cleared: the root fills the client again");
}

void test_refused_insets() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("slot.refused"));
    Window window(root, Size{100.0, 100.0});
    window.set_chrome_insets(Insets{1.0, 2.0, 3.0, 4.0});
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    require(refused(window, Insets{-1.0, 0.0, 0.0, 0.0}), "a negative inset is refused");
    require(refused(window, Insets{0.0, nan, 0.0, 0.0}), "a NaN inset is refused");
    require(refused(window, Insets{0.0, 0.0, infinity, 0.0}), "an infinite inset is refused");
    require(window.chrome_insets() == Insets{1.0, 2.0, 3.0, 4.0}, "the refusals keep the insets");
}

} // namespace

int main() {
    try {
        test_root_in_its_slot();
        test_refused_insets();
        std::cout << "gui_forms_window_root_slot_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_window_root_slot_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
