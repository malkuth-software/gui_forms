// The control tree's depth limit (maximum_control_tree_depth, house rule
// 2026-11-11): a tree exactly at the limit lays out, paints, hit-tests,
// publishes semantics and disposes; one level more is refused before any
// change, by add_child, insert_child, a re-parent and the static-tree
// builder; a stale height bound never refuses a tree that fits; and the
// destruction of a tree at the limit does not recurse per level.

#include "gui_forms/gui_forms.hpp"
#include "gui_forms/static_tree.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <pthread.h>
#endif

namespace {

using namespace gui_forms;

// The controls in `root`'s tree, root included: an iterative walk over the
// public children() (Control::subtree_size is private).
std::uint64_t control_count(const Control& root) {
    std::uint64_t count = 0U;
    std::vector<const Control*> pending{&root};
    while (!pending.empty()) {
        const Control* const current = pending.back();
        pending.pop_back();
        ++count;
        for (const Control::Ptr& child : (*current).children()) {
            if (child) pending.push_back(child.get());
        }
    }
    return count;
}

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// The depth refusal is a limit, and a logic error as the cycle refusal is.
static_assert(std::is_base_of_v<std::length_error, ControlTreeDepthError>);
static_assert(std::is_base_of_v<std::logic_error, ControlTreeDepthError>);

class RecordingPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
};

std::string level_id(std::string_view prefix, std::size_t level) {
    return std::string(prefix) + "." + std::to_string(level);
}

// A chain of Panels `levels` deep, the root counted as one, each inset one
// unit in its parent. The chain's controls, root first, are kept in
// `chain`; only the root is owned by the caller's handle, the rest by
// their parents.
std::shared_ptr<Panel> make_chain(std::string_view prefix, std::size_t levels, double size,
                                  std::vector<Control::Ptr>& chain) {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId(level_id(prefix, 1U)));
    (*root).set_requested_bounds({0.0, 0.0, size, size});
    chain.clear();
    chain.push_back(root);
    for (std::size_t level = 2U; level <= levels; ++level) {
        const double inset = static_cast<double>(level - 1U);
        std::shared_ptr<Panel> panel = make_control<Panel>(StableId(level_id(prefix, level)));
        (*panel).set_requested_bounds({1.0, 1.0, size - 2.0 * inset, size - 2.0 * inset});
        (*chain.back()).add_child(panel);
        chain.push_back(panel);
    }
    return root;
}

void test_tree_at_the_limit_works() {
    std::vector<Control::Ptr> chain;
    // One level short of the limit, so the leaf Button is the limit's level.
    std::shared_ptr<Panel> root = make_chain("depth.limit", maximum_control_tree_depth - 1U, 200.0, chain);
    std::shared_ptr<Button> leaf = make_control<Button>(StableId("depth.limit.leaf"), "Leaf");
    (*leaf).set_requested_bounds({1.0, 1.0, 40.0, 24.0});
    (*chain.back()).add_child(leaf);
    require(control_count(*root) == maximum_control_tree_depth,
            "a chain at the limit holds one control per level");

    std::unique_ptr<Window> window = std::make_unique<Window>(root, Size{200.0, 200.0});
    (*window).perform_layout();
    const MetricsSnapshot laid_out = (*window).metrics_snapshot();
    require(laid_out.measure_nodes_visited >= maximum_control_tree_depth,
            "layout reaches every level of a tree at the limit");

    RecordingPainter painter;
    (*window).paint(painter, {0.0, 0.0, 200.0, 200.0});
    const MetricsSnapshot painted = (*window).metrics_snapshot();
    require(painted.paint_nodes_visited >= maximum_control_tree_depth,
            "paint reaches every level of a tree at the limit");

    const Rect leaf_bounds = (*leaf).absolute_bounds();
    const Point inside{leaf_bounds.x + leaf_bounds.width * 0.5, leaf_bounds.y + leaf_bounds.height * 0.5};
    require((*window).hit_test(inside) == leaf, "hit testing finds the control at the limit's level");

    const std::string json = (*window).semantic_snapshot().to_json();
    require(json.find("depth.limit.leaf") != std::string::npos,
            "the semantic tree publishes the control at the limit's level");

    // Disposal of everything below the root, while attached, then of the
    // root itself.
    (*chain[1]).dispose();
    require(!(*leaf).is_alive() && (*root).children().empty(),
            "disposing a subtree at the limit reaches its deepest control");
    window.reset();
    (*root).dispose();
    require(!(*root).is_alive(), "the root disposes after its window");
}

void test_one_level_more_is_refused_without_change() {
    std::vector<Control::Ptr> chain;
    std::shared_ptr<Panel> root = make_chain("depth.over", maximum_control_tree_depth, 300.0, chain);
    const Control::Ptr deepest = chain.back();
    std::shared_ptr<Button> extra = make_control<Button>(StableId("depth.over.extra"), "Extra");

    bool refused = false;
    try {
        (*deepest).add_child(extra);
    } catch (const ControlTreeDepthError&) {
        refused = true;
    }
    require(refused, "add_child refuses a child one level past the limit");
    require((*deepest).children().empty() && !(*extra).parent(),
            "a refused add_child changes neither the parent nor the child");

    // The refusal is a length_error and, as the cycle refusal, a logic_error.
    bool as_logic_error = false;
    try {
        (*deepest).insert_child(extra, 0U);
    } catch (const std::length_error&) {
        as_logic_error = true;
    }
    require(as_logic_error, "insert_child refuses with a std::length_error, a std::logic_error");
    require((*deepest).children().empty() && !(*extra).parent(),
            "a refused insert_child changes neither the parent nor the child");

    // A subtree two levels high is refused one level higher up too.
    std::shared_ptr<Panel> pair = make_control<Panel>(StableId("depth.over.pair"));
    std::shared_ptr<Panel> pair_child = make_control<Panel>(StableId("depth.over.pair.child"));
    (*pair).add_child(pair_child);
    const Control::Ptr second_deepest = chain[chain.size() - 2U];
    const std::size_t children_before = (*second_deepest).children().size();
    bool pair_refused = false;
    try {
        (*second_deepest).add_child(pair);
    } catch (const ControlTreeDepthError&) {
        pair_refused = true;
    }
    require(pair_refused && (*second_deepest).children().size() == children_before && !(*pair).parent(),
            "a subtree whose height passes the limit is refused, changing nothing");
    // At the limit exactly, the same subtree fits one level higher still.
    const Control::Ptr third_deepest = chain[chain.size() - 3U];
    (*third_deepest).add_child(pair);
    require((*pair).parent() == third_deepest, "a subtree that ends at the limit is accepted");
}

void test_reparenting_past_the_limit_is_refused() {
    std::vector<Control::Ptr> deep;
    std::shared_ptr<Panel> deep_root = make_chain("depth.move.deep", 40U, 300.0, deep);
    std::vector<Control::Ptr> tall;
    std::shared_ptr<Panel> tall_root = make_chain("depth.move.tall", 31U, 300.0, tall);
    // The tall chain's subtree below its root is 30 levels high, held at
    // the tall root, beside a sibling, so its index can be checked.
    std::shared_ptr<Panel> sibling = make_control<Panel>(StableId("depth.move.sibling"));
    (*tall_root).add_child(sibling);
    const Control::Ptr moving = tall[1];
    const std::optional<std::size_t> index_before = (*tall_root).child_index_from_top((*moving).runtime_id());

    // 40 levels plus 30 is past 64.
    bool refused = false;
    try {
        (*deep.back()).add_child(moving);
    } catch (const ControlTreeDepthError&) {
        refused = true;
    }
    require(refused, "re-parenting a subtree past the limit is refused");
    require((*moving).parent() == tall_root &&
                (*tall_root).child_index_from_top((*moving).runtime_id()) == index_before &&
                (*deep.back()).children().empty(),
            "a refused re-parent leaves the subtree where it was, at its index");

    // 34 levels plus 30 is the limit: the same move one level higher fits.
    (*deep[33]).add_child(moving);
    require((*moving).parent() == deep[33] && (*tall_root).children().size() == 1U,
            "re-parenting a subtree to end exactly at the limit is accepted");
}

void test_stale_height_bound_does_not_refuse() {
    // A control whose tall subtree has gone keeps a stale bound; the check
    // then measures, and a tree that fits is accepted.
    std::vector<Control::Ptr> holder_chain;
    std::shared_ptr<Panel> holder = make_chain("depth.stale.holder", 50U, 300.0, holder_chain);
    const Control::Ptr tall = holder_chain[1];
    static_cast<void>((*holder).remove_child((*tall).runtime_id()));
    std::vector<Control::Ptr> target_chain;
    std::shared_ptr<Panel> target_root = make_chain("depth.stale.target", 60U, 300.0, target_chain);
    (*target_chain.back()).add_child(holder);
    require((*holder).parent() == target_chain.back(),
            "a control whose tall subtree was removed is accepted where it now fits");
    // The holder is at level 61 now: a child at 62 fits.
    std::shared_ptr<Panel> more = make_control<Panel>(StableId("depth.stale.more"));
    (*holder).add_child(more);
    require((*more).parent() == holder, "a refreshed bound accepts a child that fits");
}

// Each probe's destructor records the frame it runs in. A recursive
// teardown runs each level's destructor inside its parent's, deeper in the
// stack; an iterative one runs every descendant's from the same loop, at
// the same depth.
struct FrameSpread final {
    std::uintptr_t lowest{std::numeric_limits<std::uintptr_t>::max()};
    std::uintptr_t highest{0U};
    std::size_t recorded{0U};
};

std::uintptr_t current_frame_address() noexcept {
#if defined(__GNUC__) || defined(__clang__)
    // The real frame, not a sanitizer's relocated local.
    return reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0));
#else
    volatile char marker = 0;
    return reinterpret_cast<std::uintptr_t>(&marker);
#endif
}

class FrameProbe final : public Control {
public:
    FrameProbe(StableId stable_id, FrameSpread& spread, bool records)
        : Control(std::move(stable_id)), spread_(spread), records_(records) {}
    ~FrameProbe() override {
        if (!records_) return;
        const std::uintptr_t address = current_frame_address();
        if (address < spread_.lowest) spread_.lowest = address;
        if (address > spread_.highest) spread_.highest = address;
        ++spread_.recorded;
    }

private:
    FrameSpread& spread_;
    bool records_;
};

void test_destruction_at_the_limit_is_iterative() {
    // std::thread takes no stack size, so the iteration is checked by the
    // frames the destructors run in rather than on a small-stack thread.
    FrameSpread spread{};
    // The root's destructor runs from the test; it does not record.
    std::shared_ptr<FrameProbe> root = make_control<FrameProbe>(StableId("depth.teardown.1"), spread, false);
    Control* parent = root.get();
    for (std::size_t level = 2U; level <= maximum_control_tree_depth; ++level) {
        std::shared_ptr<FrameProbe> probe =
            make_control<FrameProbe>(StableId(level_id("depth.teardown", level)), spread, true);
        Control* const made = probe.get();
        (*parent).add_child(std::move(probe));
        parent = made;
    }
    root.reset();
    require(spread.recorded == maximum_control_tree_depth - 1U, "every level below the root is destroyed");
    // A recursive teardown would spread 63 levels of several frames each,
    // kilobytes; the iterative one runs them all from one frame.
    const std::uintptr_t spread_bytes = spread.highest - spread.lowest;
    require(spread_bytes < 1024U, "destroying a tree at the limit does not nest a destructor per level");
}

#if !defined(_WIN32)
// The small-stack run: a tree at the limit made and destroyed on a thread
// whose stack holds a few dozen frames, not one destructor chain per level.
// The sanitizers enlarge every frame, so their builds give it twice the room.
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
constexpr std::size_t small_stack_bytes = 128U * 1024U;
#else
constexpr std::size_t small_stack_bytes = 64U * 1024U;
#endif
#else
constexpr std::size_t small_stack_bytes = 64U * 1024U;
#endif

struct SmallStackRun final {
    std::size_t built_levels{0U};
    bool destroyed{false};
    bool failed{false};
};

// The thread's body: builds a chain at the limit, releases every handle but
// the root's, then the root's, which destroys the whole tree on this stack.
void* build_and_destroy_at_the_limit(void* raw) noexcept {
    SmallStackRun& run = *static_cast<SmallStackRun*>(raw);
    try {
        std::vector<Control::Ptr> chain;
        std::shared_ptr<Panel> root = make_chain("depth.small-stack", maximum_control_tree_depth, 300.0, chain);
        run.built_levels = chain.size();
        chain.clear();
        root.reset();
        run.destroyed = true;
    } catch (...) {
        run.failed = true;
    }
    return nullptr;
}

void test_destruction_at_the_limit_on_a_small_stack() {
    SmallStackRun run{};
    pthread_attr_t attributes{};
    require(pthread_attr_init(&attributes) == 0, "thread attributes initialise");
    const int sized = pthread_attr_setstacksize(&attributes, small_stack_bytes);
    pthread_t thread{};
    const int started = sized == 0 ? pthread_create(&thread, &attributes, &build_and_destroy_at_the_limit, &run) : sized;
    static_cast<void>(pthread_attr_destroy(&attributes));
    require(started == 0, "a small-stack thread starts");
    require(pthread_join(thread, nullptr) == 0, "the small-stack thread ends");
    require(!run.failed && run.built_levels == maximum_control_tree_depth && run.destroyed,
            "a tree at the limit is made and destroyed on a small-stack thread");
}
#endif

void test_detached_tree_at_the_limit_disposes() {
    std::vector<Control::Ptr> chain;
    std::shared_ptr<Panel> root = make_chain("depth.detached", maximum_control_tree_depth, 300.0, chain);
    const Control::Ptr deepest = chain.back();
    chain.clear();
    (*root).dispose();
    require(!(*deepest).is_alive(), "disposing a detached tree at the limit reaches its deepest control");
}

// Makes Panels and counts them, so a refused build can be shown to make
// none.
class CountingPanelCreator final {
public:
    explicit CountingPanelCreator(std::size_t& made) : made_(&made) {}
    Control::Ptr operator()(StableId stable_id) const {
        ++(*made_);
        return make_control<Panel>(std::move(stable_id));
    }

private:
    std::size_t* made_;
};

// A description `levels` deep, the root counted as one, built from the
// deepest node up.
StaticNode chain_description(std::size_t levels) {
    StaticNode below{};
    bool has_below = false;
    for (std::size_t level = levels; level >= 2U; --level) {
        StaticNode node{};
        node.type = "Panel";
        node.stable_id = level_id("depth.static", level);
        if (has_below) node.children.push_back(std::move(below));
        below = std::move(node);
        has_below = true;
    }
    StaticNode root{};
    root.type = "Panel";
    root.stable_id = level_id("depth.static", 1U);
    if (has_below) root.children.push_back(std::move(below));
    return root;
}

void test_static_tree_depth() {
    std::size_t made = 0U;
    ControlFactory factory;
    factory.register_type("Panel", ControlFactory::Creator(CountingPanelCreator(made)));
    const StaticNode fitting = chain_description(maximum_control_tree_depth);
    const Control::Ptr built = build_static_tree(fitting, factory);
    require(control_count(*built) == maximum_control_tree_depth && made == maximum_control_tree_depth,
            "a description at the limit builds every level, top-down");
    require((*built).children().size() == 1U &&
                (*(*built).children()[0]).stable_id().value() == level_id("depth.static", 2U),
            "the top-down build keeps the description's order");

    made = 0U;
    const StaticNode deeper = chain_description(maximum_control_tree_depth + 1U);
    bool refused = false;
    try {
        static_cast<void>(build_static_tree(deeper, factory));
    } catch (const ControlTreeDepthError&) {
        refused = true;
    }
    require(refused && made == 0U, "a description one level past the limit is refused before anything is made");
}

} // namespace

int main() {
    try {
        test_tree_at_the_limit_works();
        test_one_level_more_is_refused_without_change();
        test_reparenting_past_the_limit_is_refused();
        test_stale_height_bound_does_not_refuse();
        test_destruction_at_the_limit_is_iterative();
#if !defined(_WIN32)
        test_destruction_at_the_limit_on_a_small_stack();
#endif
        test_detached_tree_at_the_limit_disposes();
        test_static_tree_depth();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "control depth: the limit's tree works, one level more is refused, teardown iterates passed\n";
    return EXIT_SUCCESS;
}
