#include "gui_forms/collection_controls.hpp"
#include "gui_forms/window.hpp"
#include "../src/core/control/control/control_input_access.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <malloc.h>
#endif

namespace details_allocation_probe {
thread_local bool enabled{};
thread_local bool fired{};
thread_local std::size_t fail_at{};
thread_local std::size_t attempts{};

void visit() {
    if (!enabled) return;
    const std::size_t current = attempts;
    ++attempts;
    if (current == fail_at) {
        fired = true;
        throw std::bad_alloc();
    }
}

void* allocate(const std::size_t bytes) {
    visit();
    const std::size_t extent = bytes == 0U ? 1U : bytes;
    void* memory = std::malloc(extent);
    if (!memory) throw std::bad_alloc();
    return memory;
}

void* allocate_aligned(const std::size_t bytes, const std::size_t alignment) {
    visit();
    const std::size_t extent = bytes == 0U ? 1U : bytes;
    void* memory = nullptr;
#ifdef _WIN32
    memory = _aligned_malloc(extent, alignment);
#else
    const int status = posix_memalign(&memory, alignment, extent);
    if (status != 0) throw std::bad_alloc();
#endif
    if (!memory) throw std::bad_alloc();
    return memory;
}

void release_aligned(void* const memory) noexcept {
#ifdef _WIN32
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}

class FaultScope final {
public:
    explicit FaultScope(const std::size_t index) {
        attempts = 0U;
        fired = false;
        fail_at = index;
        enabled = true;
    }
    ~FaultScope() { enabled = false; }
    FaultScope(const FaultScope&) = delete;
    FaultScope& operator=(const FaultScope&) = delete;
};
} // namespace details_allocation_probe

// Replacement allocation is confined to this executable. Raw addresses are
// used only at the allocation boundary; injection never spans fixture setup,
// snapshot construction, assertions, logging, or fixture destruction.
void* operator new(const std::size_t bytes) {
    void* const memory = details_allocation_probe::allocate(bytes);
    return memory;
}
void* operator new[](const std::size_t bytes) {
    void* const memory = details_allocation_probe::allocate(bytes);
    return memory;
}
void operator delete(void* const memory) noexcept { std::free(memory); }
void operator delete[](void* const memory) noexcept { std::free(memory); }
void operator delete(void* const memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* const memory, std::size_t) noexcept { std::free(memory); }
void* operator new(const std::size_t bytes, const std::align_val_t alignment) {
    void* memory = details_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
    return memory;
}
void* operator new[](const std::size_t bytes, const std::align_val_t alignment) {
    void* memory = details_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
    return memory;
}
void operator delete(void* const memory, const std::align_val_t) noexcept { details_allocation_probe::release_aligned(memory); }
void operator delete[](void* const memory, const std::align_val_t) noexcept { details_allocation_probe::release_aligned(memory); }
void operator delete(void* const memory, std::size_t, const std::align_val_t) noexcept { details_allocation_probe::release_aligned(memory); }
void operator delete[](void* const memory, std::size_t, const std::align_val_t) noexcept { details_allocation_probe::release_aligned(memory); }
// The nothrow forms too, so that every allocation (std::stable_sort's
// temporary buffer, for one, uses nothrow new) meets the replaced delete
// that frees it. A failure they would throw becomes a null result.
void* operator new(const std::size_t bytes, const std::nothrow_t&) noexcept {
    try {
        void* const memory = details_allocation_probe::allocate(bytes);
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void* operator new[](const std::size_t bytes, const std::nothrow_t&) noexcept {
    try {
        void* const memory = details_allocation_probe::allocate(bytes);
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void* operator new(const std::size_t bytes, const std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        void* const memory = details_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void* operator new[](const std::size_t bytes, const std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        void* const memory = details_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void operator delete(void* const memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete[](void* const memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete(void* const memory, const std::align_val_t, const std::nothrow_t&) noexcept {
    details_allocation_probe::release_aligned(memory);
}
void operator delete[](void* const memory, const std::align_val_t, const std::nothrow_t&) noexcept {
    details_allocation_probe::release_aligned(memory);
}

namespace {
using namespace gui_forms;

void require(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string row_id(const std::size_t index) {
    std::string result = "failure.fixture.long.stable.object.identity.";
    result.append(std::to_string(index));
    return result;
}

std::vector<ObjectDetailsColumn> make_columns(const bool replacement) {
    std::vector<ObjectDetailsColumn> result{};
    result.push_back({.id = {"failure.fixture.column.name"}, .label = "Long factual object name", .width = 150.0});
    result.push_back({.id = {replacement ? "failure.fixture.column.new" : "failure.fixture.column.old"},
        .label = "Long factual metadata column", .width = replacement ? 190.0 : 120.0,
        .alignment = ObjectColumnAlignment::right});
    return result;
}

std::vector<ObjectViewItem> make_items(const bool replacement) {
    std::vector<ObjectViewItem> result{};
    for (std::size_t index = 0U; index < 12U; ++index) {
        if (replacement && index == 3U) continue;
        ObjectViewItem item{};
        item.stable_id = row_id(index);
        item.name = "Long factual display name for " + item.stable_id;
        item.secondary_text = "Long secondary metadata for allocation coverage";
        item.description = "Long description for allocation coverage";
        item.image_key = "long.fixture.image.key";
        item.cells.push_back({{replacement ? "failure.fixture.column.new" : "failure.fixture.column.old"},
            replacement ? "Replacement factual metadata" : "Original factual metadata"});
        item.cells.push_back({{"failure.fixture.column.name"}, item.name});
        result.push_back(std::move(item));
    }
    if (replacement) std::reverse(result.begin(), result.end());
    return result;
}

void key(ObjectView& view, const std::uint32_t physical_key) {
    KeyEvent event{KeyAction::down, physical_key};
    gui_forms::detail::ControlInputAccess::on_key(view, event);
}

struct Fixture final {
    std::shared_ptr<ObjectView> owner{make_control<ObjectView>(StableId("details.failure.fixture"))};
    Window window{owner, {230.0, 190.0}};

    explicit Fixture(const bool capture) {
        ObjectView& view = *owner;
        view.set_requested_bounds({0.0, 0.0, 230.0, 190.0});
        view.view_mode = ObjectViewMode::details;
        view.set_details_model(make_columns(false), make_items(false));
        view.set_selected_ids({row_id(2U), row_id(3U), row_id(8U)}, row_id(3U));
        require(window.request_focus(owner), "fixture focus");
        view.set_top_row(4U);
        view.details_sort = {{"failure.fixture.column.old"}, ObjectSortDirection::descending};
        window.flush();
        key(view, PhysicalKey::f6);
        key(view, PhysicalKey::end);
        if (capture) {
            view.set_horizontal_offset(0.0);
            PointerEvent down{PointerAction::down, PointerButton::primary, {154.0, 15.0}};
            gui_forms::detail::ControlInputAccess::on_pointer(view, down);
            require(view.has_pointer_capture(), "fixture resize capture");
        }
    }
};

void append_text(std::string& destination, const std::string_view value) {
    destination.append(std::to_string(value.size()));
    destination.push_back(':');
    destination.append(value);
    destination.push_back(';');
}

struct HeaderProbe final {
    std::string& identity;
    void operator()(const ObjectDetailsSort& request) const { identity = request.column.value; }
};

// Complete public model and identity state, plus observable header focus,
// capture and visible semantic selection. No private access or new public seam.
std::string snapshot(ObjectView& view) {
    std::string result{};
    for (const ObjectDetailsColumn& column : view.details_columns()) {
        append_text(result, column.id.value);
        append_text(result, column.label);
        append_text(result, std::to_string(column.width));
        append_text(result, std::to_string(column.minimum_width));
        append_text(result, std::to_string(column.maximum_width));
        append_text(result, std::to_string(static_cast<unsigned int>(column.alignment)));
        append_text(result, column.sortable ? "sortable" : "unsortable");
    }
    append_text(result, "end columns");
    for (const ObjectViewItem& item : view.items()) {
        append_text(result, item.stable_id);
        append_text(result, item.name);
        append_text(result, item.secondary_text);
        append_text(result, item.description);
        append_text(result, item.image_key);
        append_text(result, std::to_string(static_cast<unsigned int>(item.glyph)));
        append_text(result, item.enabled ? "enabled" : "disabled");
        for (const ObjectDetailsCell& cell : item.cells) {
            append_text(result, cell.column.value);
            append_text(result, cell.text);
            append_text(result, std::to_string(static_cast<unsigned int>(cell.availability)));
        }
        append_text(result, "end cells");
    }
    append_text(result, "end items");
    for (const std::string& id : view.selected_ids()) append_text(result, id);
    append_text(result, "end selection");
    append_text(result, view.selected_id.read());
    append_text(result, view.focused_id());
    append_text(result, view.selection_anchor_id());
    append_text(result, std::to_string(view.top_row()));
    append_text(result, std::to_string(view.horizontal_offset()));
    append_text(result, view.details_sort.read().column.value);
    append_text(result, std::to_string(static_cast<unsigned int>(view.details_sort.read().direction)));
    append_text(result, view.has_pointer_capture() ? "captured" : "released");
    std::string header{};
    SubscriptionToken probe = view.sort_requested().subscribe(HeaderProbe{header});
    // Enter now retires a captured resize by contract. Do not mutate that
    // gesture while taking its snapshot; finish_resize probes it separately.
    if (view.has_pointer_capture()) header = "active resize verified by follow-up gesture";
    else key(view, PhysicalKey::enter);
    append_text(result, header);
    const std::vector<SemanticNode> nodes = view.semantic_virtual_children();
    for (const SemanticNode& node : nodes) {
        append_text(result, std::to_string(static_cast<unsigned int>(node.states)));
    }
    return result;
}

enum class Operation : std::uint8_t { replace, selection, select_all, clear, replace_with_sort };

struct Input final {
    std::vector<ObjectDetailsColumn> columns{make_columns(true)};
    std::vector<ObjectViewItem> items{make_items(true)};
    std::vector<std::string> selection{row_id(1U), row_id(7U)};
    std::string primary{row_id(7U)};
    ObjectDetailsSort accepted_sort{{"failure.fixture.column.new"}, ObjectSortDirection::ascending};

    void apply(ObjectView& view, const Operation operation) {
        if (operation == Operation::replace) view.set_details_model(std::move(columns), std::move(items));
        else if (operation == Operation::replace_with_sort) {
            view.set_details_model(std::move(columns), std::move(items), std::move(accepted_sort));
        }
        else if (operation == Operation::selection) view.set_selected_ids(std::move(selection), primary);
        else if (operation == Operation::select_all) view.select_all();
        else view.clear_selection();
    }
};

struct SelectionCounter final {
    std::size_t& calls;
    void operator()(const ObjectSelectionChange&) const { ++calls; }
};

void finish_resize(ObjectView& view) {
    PointerEvent move{PointerAction::move, PointerButton::none, {174.0, 15.0}};
    gui_forms::detail::ControlInputAccess::on_pointer(view, move);
    PointerEvent up{PointerAction::up, PointerButton::primary, {174.0, 15.0}};
    gui_forms::detail::ControlInputAccess::on_pointer(view, up);
}

void campaign(const Operation operation, const bool capture) {
    Fixture expected_fixture(capture);
    Input expected_input{};
    expected_input.apply(*expected_fixture.owner, operation);
    const std::string expected = snapshot(*expected_fixture.owner);
    Fixture prior_fixture(capture);
    if (capture) {
        finish_resize(*prior_fixture.owner);
        finish_resize(*expected_fixture.owner);
    }
    const std::string prior_after_gesture = snapshot(*prior_fixture.owner);
    const std::string expected_after_gesture = snapshot(*expected_fixture.owner);
    std::size_t old_failures = 0U;
    std::size_t new_failures = 0U;
    constexpr std::size_t limit = 10000U;
    for (std::size_t failure = 0U; failure < limit; ++failure) {
        Fixture fixture(capture);
        ObjectView& view = *fixture.owner;
        Input input{};
        const std::string prior = snapshot(view);
        std::size_t observer_calls = 0U;
        SubscriptionToken observer = view.selection_changed().subscribe(SelectionCounter{observer_calls});
        bool threw = false;
        try {
            const details_allocation_probe::FaultScope scope(failure);
            input.apply(view, operation);
        } catch (const std::bad_alloc&) {
            threw = true;
        }
        const std::size_t attempts = details_allocation_probe::attempts;
        const bool fired = details_allocation_probe::fired;
        const std::string actual = snapshot(view);
        const bool old_state = actual == prior;
        const bool new_state = actual == expected;
        if (!old_state && !new_state) {
            std::cerr << "Mixed state operation=" << static_cast<unsigned int>(operation)
                      << " capture=" << capture << " failure=" << failure << '\n';
        }
        require(old_state || new_state, "allocation failure must preserve complete old or complete published state");
        require(!old_state || observer_calls == 0U, "precommit failure must not publish selection");
        if (capture) {
            finish_resize(view);
            const std::string after_gesture = snapshot(view);
            const std::string& expected_gesture = old_state ? prior_after_gesture : expected_after_gesture;
            require(after_gesture == expected_gesture,
                "allocation failure must preserve or retire pending header resize consistently");
        }
        require(fired == threw, "injected failure must not be silently swallowed");
        if (threw) {
            require(fired && attempts == failure + 1U, "injected failure must escape from its selected allocation");
            if (old_state) ++old_failures;
            else ++new_failures;
        } else if (!fired) {
            require(new_state && observer_calls == 1U && attempts == failure,
                "unfailed completion must publish complete new state exactly once");
            std::cout << "Details allocation operation=" << static_cast<unsigned int>(operation)
                      << " capture=" << capture << " sites=" << attempts
                      << " old_failures=" << old_failures << " new_failures=" << new_failures << '\n';
            return;
        }
    }
    require(false, "allocation campaign did not reach unfailed completion");
}

struct ObserverFailure final {};

struct ThrowingObserver final {
    ObjectView& view;
    const std::string& expected;
    Input& reentrant_input;
    bool reenter;
    bool& entered;

    void operator()(const ObjectSelectionChange&) const {
        if (entered) return;
        entered = true;
        require(snapshot(view) == expected, "observer must see fully published state");
        if (reenter) reentrant_input.apply(view, Operation::selection);
        throw ObserverFailure{};
    }
};

void throwing_observer(const Operation operation, const bool reenter) {
    Fixture expected_fixture(false);
    Input expected_input{};
    expected_input.apply(*expected_fixture.owner, operation);
    const std::string published = snapshot(*expected_fixture.owner);
    Input nested_expected{};
    nested_expected.selection = {row_id(5U), row_id(9U)};
    nested_expected.primary = row_id(9U);
    if (reenter) nested_expected.apply(*expected_fixture.owner, Operation::selection);
    const std::string final_expected = snapshot(*expected_fixture.owner);
    Fixture fixture(false);
    ObjectView& view = *fixture.owner;
    Input input{};
    Input nested{};
    nested.selection = {row_id(5U), row_id(9U)};
    nested.primary = row_id(9U);
    bool entered = false;
    SubscriptionToken observer = view.selection_changed().subscribe(
        ThrowingObserver{view, published, nested, reenter, entered});
    bool threw = false;
    try { input.apply(view, operation); }
    catch (const ObserverFailure&) { threw = true; }
    require(threw && entered && snapshot(view) == final_expected,
        "throwing observer must retain coherent published or reentrant state");
    observer.disconnect();
    std::size_t later_calls = 0U;
    SubscriptionToken later = view.selection_changed().subscribe(SelectionCounter{later_calls});
    view.selected_id = row_id(0U);
    require(later_calls == 1U && view.selected_id.read() == row_id(0U),
        "selection publication must remain usable after observer exception unwinds");
}
} // namespace

int main() {
    try {
        campaign(Operation::replace, false);
        campaign(Operation::replace, true);
        campaign(Operation::replace_with_sort, false);
        campaign(Operation::replace_with_sort, true);
        campaign(Operation::selection, false);
        campaign(Operation::selection, true);
        campaign(Operation::select_all, false);
        campaign(Operation::clear, false);
        for (const Operation operation : {Operation::replace, Operation::selection, Operation::select_all, Operation::clear}) {
            throwing_observer(operation, false);
            throwing_observer(operation, true);
        }
        std::cout << "gui_forms_object_details_failure_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_object_details_failure_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
