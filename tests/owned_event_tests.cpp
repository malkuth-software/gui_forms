#include "gui_forms/event.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/timer.hpp"
#include "gui_forms/commands.hpp"
#include "gui_forms/range_controls.hpp"
#include "gui_forms/value.hpp"
#include "gui_forms/collection_controls.hpp"
#include "gui_forms/window.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <malloc.h>
#endif

namespace owned_event_allocation_probe {
thread_local std::ptrdiff_t live_allocations{};
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
    ++live_allocations;
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
    ++live_allocations;
    return memory;
}

void release(void* const memory) noexcept {
    if (memory != nullptr) --live_allocations;
    std::free(memory);
}

void release_aligned(void* const memory) noexcept {
    if (memory != nullptr) --live_allocations;
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

// Whether live_allocations balances over a region. A block that the C++
// runtime library allocates without this executable's replacement operator
// new, and this executable frees, decrements the count without having
// incremented it. Ordinary builds keep those calls balanced within the
// region. Under AddressSanitizer they do not, on Windows (a DLL does not see
// the replacement) or on macOS with the pinned libc++ dylib: each
// short-lived Button frees 24 blocks that the runtime allocated, the strings
// of its property registrations among them (-24000 over the 1000 buttons
// below on both). The balance assertion is skipped there and the
// subscription assertions remain. This hides no leak finding: LeakSanitizer
// checks the macOS run and found none here, it is not available on Windows,
// and normal builds still run the assertion.
#if (defined(_WIN32) || defined(__APPLE__)) && defined(__has_feature)
#if __has_feature(address_sanitizer)
constexpr bool balance_is_observable = false;
#else
constexpr bool balance_is_observable = true;
#endif
#else
constexpr bool balance_is_observable = true;
#endif
} // namespace owned_event_allocation_probe

// Replacement allocation is confined to this executable. Raw addresses are
// used only at the allocation boundary; injection never spans fixture setup,
// snapshot construction, assertions, logging, or fixture destruction.
void* operator new(const std::size_t bytes) {
    void* const memory = owned_event_allocation_probe::allocate(bytes);
    return memory;
}
void* operator new[](const std::size_t bytes) {
    void* const memory = owned_event_allocation_probe::allocate(bytes);
    return memory;
}
void operator delete(void* const memory) noexcept { owned_event_allocation_probe::release(memory); }
void operator delete[](void* const memory) noexcept { owned_event_allocation_probe::release(memory); }
void operator delete(void* const memory, std::size_t) noexcept { owned_event_allocation_probe::release(memory); }
void operator delete[](void* const memory, std::size_t) noexcept { owned_event_allocation_probe::release(memory); }
void* operator new(const std::size_t bytes, const std::align_val_t alignment) {
    void* memory = owned_event_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
    return memory;
}
void* operator new[](const std::size_t bytes, const std::align_val_t alignment) {
    void* memory = owned_event_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
    return memory;
}
void operator delete(void* const memory, const std::align_val_t) noexcept { owned_event_allocation_probe::release_aligned(memory); }
void operator delete[](void* const memory, const std::align_val_t) noexcept { owned_event_allocation_probe::release_aligned(memory); }
void operator delete(void* const memory, std::size_t, const std::align_val_t) noexcept { owned_event_allocation_probe::release_aligned(memory); }
void operator delete[](void* const memory, std::size_t, const std::align_val_t) noexcept { owned_event_allocation_probe::release_aligned(memory); }
// The nothrow forms too, so that every allocation (std::stable_sort's
// temporary buffer, for one, uses nothrow new) meets the replaced delete
// that frees it. A failure they would throw becomes a null result.
void* operator new(const std::size_t bytes, const std::nothrow_t&) noexcept {
    try {
        void* const memory = owned_event_allocation_probe::allocate(bytes);
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void* operator new[](const std::size_t bytes, const std::nothrow_t&) noexcept {
    try {
        void* const memory = owned_event_allocation_probe::allocate(bytes);
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void* operator new(const std::size_t bytes, const std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        void* const memory = owned_event_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void* operator new[](const std::size_t bytes, const std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        void* const memory = owned_event_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void operator delete(void* const memory, const std::nothrow_t&) noexcept { owned_event_allocation_probe::release(memory); }
void operator delete[](void* const memory, const std::nothrow_t&) noexcept { owned_event_allocation_probe::release(memory); }
void operator delete(void* const memory, const std::align_val_t, const std::nothrow_t&) noexcept {
    owned_event_allocation_probe::release_aligned(memory);
}
void operator delete[](void* const memory, const std::align_val_t, const std::nothrow_t&) noexcept {
    owned_event_allocation_probe::release_aligned(memory);
}

namespace {
namespace gf = gui_forms;

void require(const bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class Recorder : public gf::Component {
public:
    explicit Recorder(int& total) : total_(total) {}
    virtual void add(const int value) { total_ += value; }
    void add_const(const int value) const { total_ += value; }
    void add_noexcept(const int value) noexcept { total_ += value; }
    void add_const_noexcept(const int value) const noexcept { total_ += value; }
    void change(std::string& text, const int& value) { text += std::to_string(value); }
    void clicked(gf::ButtonBase&) { ++total_; }
    void text_changed(const std::string&) { ++total_; }
    void tick() { ++total_; }
    void overload() { total_ += 100; }
    void overload(const int value) { total_ += value; }

private:
    int& total_;
};

class DerivedRecorder final : public Recorder {
public:
    explicit DerivedRecorder(int& total) : Recorder(total) {}
    void add(const int value) override { Recorder::add(value * 2); }
};

class ShadowingOwner final : public gf::Component {
public:
    // These application members must not replace Component's lifetime protocol.
    [[nodiscard]] bool is_alive() const noexcept { return true; }
    void own_subscription(gf::SubscriptionToken) { ++intercepted; }
    void tick() { ++calls; }
    int calls{};
    int intercepted{};
};

class NestedOwner final : public gf::Component {
public:
    explicit NestedOwner(gf::Event<int>& event) : event_(event) {}
    void first(const int depth) {
        order[count] = 10 + depth;
        ++count;
        if (depth == 0) {
            gf::on(event_, *this, &NestedOwner::third);
            event_.emit(1);
        }
    }
    void second(const int depth) {
        order[count] = 20 + depth;
        ++count;
    }
    void third(const int depth) {
        order[count] = 30 + depth;
        ++count;
    }
    std::array<int, 5U> order{};
    std::size_t count{};

private:
    gf::Event<int>& event_;
};

class DisposalOwner final : public gf::Component {
public:
    explicit DisposalOwner(gf::Event<>& event) : event_(event) {}
    void first() {
        ++calls;
        dispose();
        // Neither registration nor a nested emission may revive this owner.
        gf::on(event_, *this, &DisposalOwner::later);
        event_.emit();
        ++completed;
    }
    void later() { calls += 100; }
    int calls{};
    int completed{};
    bool revoked_before_hook{};

private:
    void on_dispose() noexcept override {
        const gf::EventStatistics statistics = event_.statistics();
        revoked_before_hook = statistics.subscriptions_connected ==
                              statistics.subscriptions_disconnected;
    }
    gf::Event<>& event_;
};

class PublisherDestruction final : public gf::Component {
public:
    std::unique_ptr<gf::Event<>> event{std::make_unique<gf::Event<>>()};
    int calls{};
    void destroy() {
        ++calls;
        event.reset();
        ++calls;
    }
    void later() { calls += 100; }
};

class SelfDestruction final : public gf::Component {
public:
    SelfDestruction(std::unique_ptr<SelfDestruction>& owner, int& calls)
        : owner_(owner), calls_(calls) {}
    void destroy() {
        ++calls_;
        owner_.reset();
        // No access to this after destruction. The event retains its slot.
    }
    void later() { calls_ += 100; }

private:
    std::unique_ptr<SelfDestruction>& owner_;
    int& calls_;
};

class ThrowingOwner final : public gf::Component {
public:
    void fail() {
        ++calls;
        if (calls == 1) {
            throw std::runtime_error("owned handler failure");
        }
    }
    int calls{};
};

struct RegistrationOnRelease final {
    gf::Component& owner;
    gf::Event<>& event;
    bool& observed_disposing;

    ~RegistrationOnRelease() {
        observed_disposing = !owner.is_alive();
        gf::on(event, owner, &gf::Component::dispose);
    }
};

struct RetainUntilDisconnected final {
    std::shared_ptr<RegistrationOnRelease> retained{};
    void operator()() const noexcept {}
};

void test_signatures_and_natural_destruction() {
    gf::Event<int> event{};
    gf::Event<int> overloaded{};
    gf::Event<std::string&, const int&> references{};
    int total = 0;
    std::string text{};
    {
        DerivedRecorder owner(total);
        gf::on(event, owner, &Recorder::add);
        gf::on(event, owner, &Recorder::add_const);
        gf::on(event, owner, &Recorder::add_noexcept);
        gf::on(event, owner, &Recorder::add_const_noexcept);
        gf::on(references, owner, &Recorder::change);
        gf::on(overloaded, owner, static_cast<void (Recorder::*)(int)>(&Recorder::overload));
        overloaded.emit(0);
        event.emit(2);
        references.emit(text, total);
        require(total == 10 && text == "10",
                "deduction must preserve const, noexcept, virtual bases and references");
    }
    event.emit(50);
    require(total == 10, "natural owner destruction must revoke every handler");
    const gf::EventStatistics statistics = event.statistics();
    require(statistics.subscriptions_connected == 4U &&
                statistics.subscriptions_disconnected == 4U,
            "each owned subscription must disconnect exactly once");
}

void test_real_control_and_timer_signatures() {
    int total = 0;
    Recorder owner(total);
    const std::shared_ptr<gf::Button> button =
        gf::make_control<gf::Button>(gf::StableId("owned.button"), "Run");
    const std::shared_ptr<gf::Label> label =
        gf::make_control<gf::Label>(gf::StableId("owned.label"));
    const gf::Control::Ptr root =
        gf::make_control<gf::Control>(gf::StableId("owned.root"));
    gf::Window window(root, {100.0, 100.0});
    gf::Timer timer(window);
    gf::on((*button).clicked(), owner, &Recorder::clicked);
    gf::on((*label).text_changed(), owner, &Recorder::text_changed);
    gf::on(timer.tick(), owner, &Recorder::tick);
    (*button).perform_click();
    (*label).set_text("Changed");
    timer.tick().emit();
    require(total == 3 && !timer.enabled(),
            "control and timer handlers must wire without starting a timer");
    owner.dispose();
    (*button).perform_click();
    (*label).set_text("Again");
    timer.tick().emit();
    require(total == 3, "disposed owner must stop real control notifications");
}

void test_component_protocol_is_explicit() {
    gf::Event<> event{};
    ShadowingOwner owner{};
    gf::on(event, owner, &ShadowingOwner::tick);
    event.emit();
    owner.dispose();
    gf::on(event, owner, &ShadowingOwner::tick);
    event.emit();
    const gf::EventStatistics statistics = event.statistics();
    require(owner.calls == 1 && owner.intercepted == 0 &&
                statistics.subscriptions_connected == 1U,
            "derived method names must not select or replace lifecycle authority");
}

void test_disposal_during_emission() {
    gf::Event<> event{};
    DisposalOwner owner(event);
    gf::on(event, owner, &DisposalOwner::first);
    gf::on(event, owner, &DisposalOwner::later);
    event.emit();
    require(owner.calls == 1 && owner.completed == 1 && owner.revoked_before_hook,
            "disposal must cancel pending callbacks and retain the executing binding");
    const gf::EventStatistics statistics = event.statistics();
    require(statistics.subscriptions_connected == 2U &&
                statistics.subscriptions_disconnected == 2U,
            "registration on a dead owner must not create a slot");
}

void test_natural_teardown_cannot_resubscribe() {
    gf::Event<> event{};
    bool observed_disposing = false;
    {
        gf::Component owner{};
        const std::shared_ptr<RegistrationOnRelease> teardown =
            std::make_shared<RegistrationOnRelease>(owner, event, observed_disposing);
        owner.own_subscription(event.subscribe(RetainUntilDisconnected{teardown}));
    }
    const gf::EventStatistics statistics = event.statistics();
    require(observed_disposing && statistics.subscriptions_connected == 1U &&
                statistics.subscriptions_disconnected == 1U,
            "callback destruction must not register new work on a destructing Component");
    event.emit();
}

// A subscribed callable keeps its own state across emissions, nested
// emissions and its own disconnection (sanitizer review, 2026-10-11).
struct CountingHandler final {
    gf::Event<int>* event{};
    std::vector<int>* observed{};
    gf::SubscriptionToken* token{};
    int count{};
    bool nest{};
    bool disconnect_self{};

    void operator()(const int value) {
        ++count;
        (*observed).push_back(count * 100 + value);
        if (nest) {
            nest = false;
            (*event).emit(value + 1);
        }
        if (disconnect_self && token != nullptr) {
            (*token).disconnect();
            (*observed).push_back(count * 100 + value);
        }
    }
};

void test_callable_state_persists() {
    gf::Event<int> event;
    std::vector<int> observed;
    CountingHandler repeated{};
    repeated.event = &event;
    repeated.observed = &observed;
    const gf::SubscriptionToken counted = event.subscribe(repeated);
    event.emit(1);
    event.emit(2);
    require(observed == std::vector<int>{101, 202}, "repeated emissions share one callable state");

    gf::Event<int> nested_event;
    std::vector<int> nested_observed;
    CountingHandler nested{};
    nested.event = &nested_event;
    nested.observed = &nested_observed;
    nested.nest = true;
    const gf::SubscriptionToken nested_token = nested_event.subscribe(nested);
    nested_event.emit(1);
    nested_event.emit(5);
    require(nested_observed == std::vector<int>{101, 202, 305}, "nested emissions share one callable state");

    gf::Event<int> revoking_event;
    std::vector<int> revoking_observed;
    gf::SubscriptionToken revoking_token;
    CountingHandler revoking{};
    revoking.observed = &revoking_observed;
    revoking.token = &revoking_token;
    revoking.disconnect_self = true;
    revoking_token = revoking_event.subscribe(revoking);
    revoking_event.emit(7);
    revoking_event.emit(8);
    require(revoking_observed == std::vector<int>{107, 107}, "a callable survives its own disconnection");
}

void test_nested_emission() {
    gf::Event<int> event{};
    NestedOwner owner(event);
    gf::on(event, owner, &NestedOwner::first);
    gf::on(event, owner, &NestedOwner::second);
    event.emit(0);
    const std::array<int, 5U> expected{10, 11, 21, 31, 20};
    require(owner.order == expected && owner.count == expected.size(),
            "nested events must keep independent registration boundaries");
}

void test_publisher_and_target_destruction() {
    PublisherDestruction owner{};
    gf::on(*owner.event, owner, &PublisherDestruction::destroy);
    gf::on(*owner.event, owner, &PublisherDestruction::later);
    (*owner.event).emit();
    require(owner.calls == 2, "publisher destruction must skip later slots");
    owner.dispose();

    gf::Event<> event{};
    int calls = 0;
    std::unique_ptr<SelfDestruction> target{};
    target = std::make_unique<SelfDestruction>(target, calls);
    gf::on(event, *target, &SelfDestruction::destroy);
    gf::on(event, *target, &SelfDestruction::later);
    event.emit();
    require(!target && calls == 1,
            "destroying a target during dispatch must cancel remaining handlers");
}

void test_exception_recovery_and_null_member() {
    gf::Event<> event{};
    ThrowingOwner owner{};
    gf::on(event, owner, &ThrowingOwner::fail);
    bool propagated = false;
    try {
        event.emit();
    } catch (const std::runtime_error&) {
        propagated = true;
    }
    event.emit();
    require(propagated && owner.calls == 2,
            "exception must propagate without corrupting the next emission");
    void (ThrowingOwner::*null_method)() = nullptr;
    bool rejected = false;
    try {
        gf::on(event, owner, null_method);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "null member pointers must be rejected in release builds");
}

void test_explicit_token_transfer_and_legacy_lifetime() {
    gf::Event<int> event{};
    int total = 0;
    Recorder owner(total);
    const gf::Delegate<int> handler =
        gf::Delegate<int>::bind<Recorder, &Recorder::add>(owner);
    {
        const gf::SubscriptionToken token = event.subscribe(owner, handler);
        event.emit(1);
    }
    event.emit(2);
    require(total == 1, "legacy subscribe still requires a surviving token");
    gf::SubscriptionToken token = event.subscribe(handler);
    owner.own_subscription(std::move(token));
    require(!token.connected(), "token transfer must leave the former owner empty");
    event.emit(4);
    owner.dispose();
    event.emit(8);
    require(total == 5, "explicit transfer must retain then revoke the connection");
    owner.own_subscription(event.subscribe(handler));
    event.emit(16);
    require(total == 5, "a dead owner must immediately disconnect a transferred token");
}

void test_short_lived_publishers_release_owner_storage() {
    int total = 0;
    Recorder owner(total);
    // Prime unrelated control metadata before measuring retained allocations.
    {
        gf::Button button(gf::StableId("warmup"));
        gf::on(button.clicked(), owner, &Recorder::clicked);
    }
    const std::ptrdiff_t before = owned_event_allocation_probe::live_allocations;
    for (int index = 0; index < 1000; ++index) {
        {
            gf::Button button(gf::StableId("short-lived"));
            gf::on(button.clicked(), owner, &Recorder::clicked);
            require(owner.owned_subscription_count() == 1U,
                    "a live button must retain exactly one owner subscription");
            button.perform_click();
        }
        require(owner.owned_subscription_count() == 0U,
                "publisher destruction must immediately unlink its owner token");
    }
    require(total == 1000, "all short-lived buttons must invoke their handler");
    if (owned_event_allocation_probe::balance_is_observable) {
        require(owned_event_allocation_probe::live_allocations == before,
                "publisher destruction must release all registration allocations");
    }
}

class BoundOwner final : public gf::Component {
public:
    void fixed() { ++calls; }
    void indexed(const int index) { total += index; }
    void cell(const int row, const int choice, gf::ButtonBase& sender) {
        total += row * 10 + choice;
        last = &sender;
    }
    void prefix(const int index, const int first) { total += index + first; }
    int calls{};
    int total{};
    gf::ButtonBase* last{};
};

void test_bound_values_and_omitted_arguments() {
    gf::Button button(gf::StableId("bound"));
    gf::Event<int, bool> event{};
    BoundOwner owner{};
    gf::on(button.clicked(), owner, &BoundOwner::fixed);
    gf::on(button.clicked(), owner, &BoundOwner::indexed, 7);
    gf::on(button.clicked(), owner, &BoundOwner::cell, 2, 3);
    gf::on(event, owner, &BoundOwner::prefix, 5);
    std::size_t allocations = 0U;
    {
        owned_event_allocation_probe::FaultScope probe(
            std::numeric_limits<std::size_t>::max());
        for (int index = 0; index < 10000; ++index) {
            button.clicked().emit(button);
            event.emit(11, true);
        }
        allocations = owned_event_allocation_probe::attempts;
    }
    require(owner.calls == 10000 && owner.total == 460000 &&
                owner.last == &button && allocations == 0U,
            "bound values precede accepted event prefixes without dispatch allocation");
    owner.dispose();
    button.clicked().emit(button);
    require(owner.calls == 10000, "bound handlers must obey owner lifetime");
}

class StateObserver final : public gf::Component {
public:
    void value(const double) { ++changes; }
    void scroll(const gf::RangeScrollEvent&) { ++inputs; }
    void command() { ++invocations; }
    int changes{};
    int inputs{};
    int invocations{};
};

class CountedTrackBar final : public gf::TrackBar {
public:
    explicit CountedTrackBar(gf::StableId id) : gf::TrackBar(std::move(id)) {}
    void set_value(const double value) override {
        ++writes;
        gf::TrackBar::set_value(value);
    }
    int writes{};
};

class DestroyValue final : public gf::Component {
public:
    explicit DestroyValue(std::unique_ptr<gf::Value<int>>& value) : value_(value) {}
    void destroy() { value_.reset(); }
private:
    std::unique_ptr<gf::Value<int>>& value_;
};

void test_shared_scalar_state() {
    gf::Value<double> volume(15.0);
    CountedTrackBar first(gf::StableId("first"));
    CountedTrackBar second(gf::StableId("second"));
    StateObserver observer{};
    first.bind(volume);
    second.bind(volume);
    gf::on(first.value_changed(), observer, &StateObserver::value);
    gf::on(second.value_changed(), observer, &StateObserver::value);
    gf::on(first.scroll(), observer, &StateObserver::scroll);
    require(first.value() == 15.0 && second.value() == 15.0,
            "model must win at bind time");
    first.writes = 0;
    second.writes = 0;
    first.set_value(20.0);
    require(volume.get() == 20.0 && second.value() == 20.0 &&
                first.writes == 1 && second.writes == 1 &&
                observer.changes == 2 && observer.inputs == 0,
            "programmatic writes synchronize once without user input or origin reentry");
    std::size_t allocations = 0U;
    {
        owned_event_allocation_probe::FaultScope probe(
            std::numeric_limits<std::size_t>::max());
        for (int index = 0; index < 10000; ++index) {
            volume.set(static_cast<double>(index % 100));
        }
        allocations = owned_event_allocation_probe::attempts;
    }
    require(allocations == 0U && first.value() == 99.0 && second.value() == 99.0,
            "steady state scalar propagation must allocate nothing");
    const gf::SemanticDescriptor semantic = first.semantic_descriptor();
    require(semantic.numeric_value == 99.0,
            "accessibility must observe the bound value");
    gf::KeyEvent input{.action = gf::KeyAction::down,
                      .physical_key = gf::PhysicalKey::left};
    first.on_key(input);
    require(input.handled && observer.inputs == 1 && volume.get() == first.value(),
            "keyboard input must update the shared model and emit the input event");
    second.set_maximum(50.0);
    const double prior = first.value();
    bool rejected = false;
    try { first.set_value(75.0); } catch (const std::out_of_range&) { rejected = true; }
    require(rejected && first.value() == prior && volume.get() == prior,
            "another control constraint must reject before the originating control changes");
    volume.dispose();
    first.set_value(30.0);
    require(second.value() != 30.0, "disposed model must detach its bindings");
}

void test_model_and_control_lifetimes() {
    gf::TrackBar control(gf::StableId("survivor"));
    {
        gf::Value<double> value(17.0);
        control.bind(value);
    }
    control.set_value(18.0);
    gf::Value<double> retained(12.0);
    {
        gf::TrackBar temporary(gf::StableId("temporary"));
        temporary.bind(retained);
    }
    retained.set(19.0);
    std::unique_ptr<gf::Value<int>> value = std::make_unique<gf::Value<int>>();
    DestroyValue destroy(value);
    int total = 0;
    Recorder observer(total);
    gf::on((*value).changed(), destroy, &DestroyValue::destroy);
    gf::on((*value).changed(), observer, &Recorder::add);
    (*value).set(5);
    require(!value && total == 0 && observer.owned_subscription_count() == 0U,
            "model destruction during dispatch must cancel later observers and release tokens");
}

void test_shared_commands() {
    gf::Command music{};
    music.set_checked(true);
    gf::CheckBox first(gf::StableId("first-music"));
    gf::CheckBox second(gf::StableId("second-music"));
    StateObserver observer{};
    gf::on(music.invoked(), observer, &StateObserver::command);
    first.bind(music);
    second.bind(music);
    require(first.checked() && second.checked(), "command checked state wins on binding");
    first.perform_click();
    require(!music.state().checked && !second.checked() && observer.invocations == 1,
            "one click invokes once and synchronizes every presentation");
    std::size_t allocations = 0U;
    {
        owned_event_allocation_probe::FaultScope probe(
            std::numeric_limits<std::size_t>::max());
        for (int index = 0; index < 10000; ++index) {
            music.set_checked(index % 2 == 0);
            music.set_enabled(index % 2 == 0);
        }
        allocations = owned_event_allocation_probe::attempts;
    }
    require(allocations == 0U && !first.enabled.type() && !second.enabled.type(),
            "command state propagation must allocate nothing");
    require(!first.perform_click() && observer.invocations == 1,
            "disabled bound commands cannot be invoked");
    music.set_enabled(true);
    music.set_checked(true);
    require(gf::has_semantic_state(second.semantic_descriptor().states,
                                  gf::SemanticState::checked),
            "bound command check state must reach accessibility");
    music.dispose();
    first.perform_click();
    require(observer.invocations == 1, "disposed commands cannot be invoked by a surviving button");
}

class CollectionObserver final : public gf::Component {
public:
    void command(const gf::CommandItem& item) { last_id = item.id; ++commands; }
    void choice(const int index) { last_index = index; ++choices; }
    void toggle(const int index, const bool expanded) {
        last_index = index;
        last_expanded = expanded;
        ++toggles;
    }
    int last_id{};
    int last_index{-1};
    int commands{};
    int choices{};
    int toggles{};
    bool last_expanded{};
};

class ReplaceCommandItems final : public gf::Component {
public:
    explicit ReplaceCommandItems(gf::CommandBar& bar) : bar_(bar) {}
    void replace(const gf::CommandItem&) {
        bar_.set_items({{.id = 9, .label = "Replacement"}});
    }
private:
    gf::CommandBar& bar_;
};

void test_command_bar_rebuild_and_keyboard() {
    const std::shared_ptr<gf::CommandBar> bar =
        gf::make_control<gf::CommandBar>(gf::StableId("commands"));
    gf::Command run("run", "Run");
    gf::Window window(bar, {300.0, 40.0});
    CollectionObserver observer{};
    StateObserver command_observer{};
    gf::on(run.invoked(), command_observer, &StateObserver::command);
    gf::on((*bar).invoked(), observer, &CollectionObserver::command);
    (*bar).set_items({{.id = 1, .label = "Run", .shortcut = gf::KeyGesture{gf::PhysicalKey::r, gf::Modifier::control}, .command = &run},
                     {.id = 2, .label = "Help"}});
    window.flush();
    require((*bar).item(0).arranged_bounds().width == 150.0 &&
                (*bar).item(1).arranged_bounds().x == 150.0,
            "collection geometry must survive Window layout-slot traversal");
    run.set_enabled(false);
    require(!(*bar).item(0).tab_stop.type() && (*bar).item(1).tab_stop.type(),
            "disabling the tab-stop command must leave an available bar entry reachable");
    run.set_enabled(true);
    gf::Button* const reused = &(*bar).item(0);
    (*bar).item(0).perform_click();
    require(observer.commands == 1 && observer.last_id == 1 && command_observer.invocations == 1,
            "command bar invokes the bound command and reports its item");
    (*bar).set_items({{.id = 2, .label = "Help"}, {.id = 1, .label = "Run", .command = &run}});
    require(&(*bar).item(1) == reused, "reordering must reuse the same item control");
    (*bar).item(1).perform_click();
    require(observer.commands == 2 && command_observer.invocations == 2,
            "group subscription survives replacement without duplicate commands");
    require((*bar).semantic_descriptor().role == gf::SemanticRole::toolbar &&
                (*bar).part("item", 1) == reused,
            "command bar must publish toolbar semantics and named item parts");
    static_cast<void>(window.request_focus((*bar).item(0).shared_from_this()));
    gf::KeyEvent right{.physical_key = gf::PhysicalKey::right};
    (*bar).on_key(right);
    require(right.handled && window.focused_control().get() == reused &&
                !(*bar).item(0).tab_stop.type() && (*bar).item(1).tab_stop.type(),
            "arrow navigation must maintain a single tab stop");
    gf::KeyEvent tab{.physical_key = gf::PhysicalKey::tab};
    (*bar).on_key(tab);
    require(!tab.handled, "Tab must leave command-bar traversal to the Window");

    const std::shared_ptr<gf::CommandBar> rebuilding =
        gf::make_control<gf::CommandBar>(gf::StableId("rebuilding"));
    (*rebuilding).set_items({{.id = 7, .label = "Original"}});
    ReplaceCommandItems replace(*rebuilding);
    CollectionObserver later{};
    gf::on((*rebuilding).invoked(), replace, &ReplaceCommandItems::replace);
    gf::on((*rebuilding).invoked(), later, &CollectionObserver::command);
    (*rebuilding).item(0).perform_click();
    require(later.last_id == 7 && (*rebuilding).items()[0].id == 9,
            "dispatch must retain the original item across list replacement");
}

void test_choice_group_state_and_rebuild() {
    const std::shared_ptr<gf::ChoiceGroup> group =
        gf::make_control<gf::ChoiceGroup>(gf::StableId("choices"));
    (*group).set_items({{.id = 10, .label = "First"}, {.id = 20, .label = "Second"}});
    gf::Value<int> model(0);
    (*group).bind(model);
    CollectionObserver observer{};
    gf::on((*group).changed(), observer, &CollectionObserver::choice);
    gf::Button* const first = &(*group).item(0);
    (*group).item(1).perform_click();
    require(model.get() == 1 && (*group).selected_index() == 1 && observer.choices == 1,
            "choice activation must synchronize the selection model once");
    require((*group).semantic_descriptor().role == gf::SemanticRole::radio_group &&
                (*group).item(1).semantic_descriptor().role == gf::SemanticRole::radio_button &&
                gf::has_semantic_state((*group).item(1).semantic_descriptor().states, gf::SemanticState::checked),
            "choice semantics must expose a checked radio item");
    bool rejected = false;
    try { model.set(9); } catch (const std::out_of_range&) { rejected = true; }
    require(rejected && model.get() == 1, "invalid bound selection must fail before model mutation");
    std::size_t allocations = 0U;
    {
        owned_event_allocation_probe::FaultScope probe(std::numeric_limits<std::size_t>::max());
        for (int index = 0; index < 10000; ++index) model.set(index % 2);
        allocations = owned_event_allocation_probe::attempts;
    }
    require(allocations == 0U, "choice changes must allocate nothing after configuration");
    (*group).set_items({{.id = 10, .label = "Only"}});
    require(&(*group).item(0) == first && model.get() == -1,
            "shrinking a choice list reuses controls and clears a removed selection");
    const int before = observer.choices;
    gf::KeyEvent right{.physical_key = gf::PhysicalKey::right};
    (*group).on_key(right);
    require(right.handled && model.get() == 0 && observer.choices == before + 1,
            "arrow keys select choices through the shared model");
}

void test_expandable_sections_state() {
    const std::shared_ptr<gf::ExpandableSections> sections =
        gf::make_control<gf::ExpandableSections>(gf::StableId("sections"));
    const gf::Control::Ptr first = gf::make_control<gf::Control>(gf::StableId("first-body"));
    const gf::Control::Ptr second = gf::make_control<gf::Control>(gf::StableId("second-body"));
    gf::Value<bool> first_open(true);
    gf::Value<bool> second_open(false);
    (*sections).set_single_open(true);
    (*sections).set_items({{.id = 1, .label = "First", .content = first, .expanded = &first_open},
                          {.id = 2, .label = "Second", .content = second, .expanded = &second_open}});
    CollectionObserver observer{};
    gf::on((*sections).toggled(), observer, &CollectionObserver::toggle);
    require((*first).visible.type() && !(*second).visible.type(), "section model wins at attachment");
    second_open.set(true);
    require(!first_open.get() && second_open.get() && !(*first).visible.type() && (*second).visible.type(),
            "single-open sections must close the other model and retained body");
    const gf::SemanticDescriptor header = (*(*sections).part("header", 1)).semantic_descriptor();
    require(gf::has_semantic_state(header.states, gf::SemanticState::expanded),
            "disclosure accessibility state must follow its shared model");
    std::size_t allocations = 0U;
    {
        owned_event_allocation_probe::FaultScope probe(std::numeric_limits<std::size_t>::max());
        for (int index = 0; index < 10000; ++index) second_open.set(index % 2 == 0);
        allocations = owned_event_allocation_probe::attempts;
    }
    require(allocations == 0U, "section changes must allocate nothing after configuration");
    (*sections).set_items({{.id = 1, .label = "First", .content = second, .expanded = &first_open},
                          {.id = 2, .label = "Second", .content = first, .expanded = &second_open}});
    require((*first).parent() == sections && (*second).parent() == sections,
            "swapping section contents must preserve both tree attachments");
    gf::Control* const reused = (*sections).part("header", 0);
    (*sections).set_items({{.id = 1, .label = "Renamed", .content = first, .expanded = &first_open}});
    require((*sections).part("header", 0) == reused, "section replacement reuses headers");
    const int before = observer.toggles;
    (*sections).set_expanded(0, true);
    require(observer.toggles == before + 1 && first_open.get(),
            "group toggle subscription survives replacement");
}

struct ReleaseRecord final {
    std::array<int, 3U>& order;
    std::size_t& count;
    int id;
    ~ReleaseRecord() { order[count] = id; ++count; }
};
struct RetainRecord final {
    std::shared_ptr<ReleaseRecord> record;
    void operator()() const noexcept {}
};
void test_mixed_revocation_order() {
    gf::Component owner{};
    gf::Event<> first{};
    gf::Event<> middle{};
    gf::Event<> last{};
    std::array<int, 3U> order{};
    std::size_t count = 0U;
    const gf::SubscriptionToken first_token = first.subscribe(owner,
        RetainRecord{std::make_shared<ReleaseRecord>(order, count, 1)});
    owner.own_subscription(middle.subscribe(
        RetainRecord{std::make_shared<ReleaseRecord>(order, count, 2)}));
    const gf::SubscriptionToken last_token = last.subscribe(owner,
        RetainRecord{std::make_shared<ReleaseRecord>(order, count, 3)});
    owner.dispose();
    const std::array<int, 3U> expected{3, 2, 1};
    require(count == 3U && order == expected,
            "caller-held and owner-held tokens must revoke in reverse acquisition order");
}

class CheckDuringEnableChange final : public gf::Component {
public:
    explicit CheckDuringEnableChange(gf::Command& command) : command_(command) {}
    void change(bool) { command_.set_checked(true); }
private:
    gf::Command& command_;
};
void test_nested_command_change_keeps_latest_state() {
    gf::Command command{};
    command.set_checked(false);
    gf::CheckBox button(gf::StableId("nested-command"));
    button.bind(command);
    CheckDuringEnableChange observer(command);
    gf::on(button.enabled_changed(), observer, &CheckDuringEnableChange::change);
    command.set_enabled(false);
    require(command.state().checked && button.checked() && !button.enabled.type(),
            "a nested command update must not be overwritten by an older projection");
}

class DestroyCommand final : public gf::Component {
public:
    explicit DestroyCommand(std::unique_ptr<gf::Command>& command) : command_(command) {}
    void destroy() { command_.reset(); }
private:
    std::unique_ptr<gf::Command>& command_;
};
class DestroySlider final : public gf::Component {
public:
    explicit DestroySlider(std::shared_ptr<gf::TrackBar>& slider) : slider_(slider) {}
    void destroy() { slider_.reset(); }
private:
    std::shared_ptr<gf::TrackBar>& slider_;
};
void test_command_and_bound_control_destruction_during_dispatch() {
    gf::Button button(gf::StableId("surviving-button"));
    std::unique_ptr<gf::Command> command = std::make_unique<gf::Command>();
    DestroyCommand destroy(command);
    StateObserver later{};
    gf::on((*command).invoked(), destroy, &DestroyCommand::destroy);
    gf::on((*command).invoked(), later, &StateObserver::command);
    button.bind(*command);
    button.perform_click();
    require(!command && later.invocations == 0 && !button.command_connected(),
            "destroying a command in invocation cancels later observers and control access");
    button.perform_click();
    gf::Value<double> model(10.0);
    std::shared_ptr<gf::TrackBar> slider = gf::make_control<gf::TrackBar>(gf::StableId("destroying-slider"));
    DestroySlider destroy_slider(slider);
    (*slider).bind(model);
    gf::on((*slider).value_changed(), destroy_slider, &DestroySlider::destroy);
    model.set(20.0);
    require(!slider && model.get() == 20.0, "a bound control can be destroyed during propagation");
    model.set(30.0);
}

void test_command_shortcut_lifetime_and_enabled_state() {
    const gf::Control::Ptr root = gf::make_control<gf::Control>(gf::StableId("shortcut-root"));
    gf::Window window(root, {100.0, 100.0});
    std::unique_ptr<gf::Command> command = std::make_unique<gf::Command>();
    StateObserver observer{};
    gf::on((*command).invoked(), observer, &StateObserver::command);
    const gf::AcceleratorToken shortcut = (*command).bind_shortcut(window,
        {gf::PhysicalKey::r, gf::Modifier::control});
    const gf::KeyEvent key{.physical_key = gf::PhysicalKey::r, .modifiers = gf::Modifier::control};
    require(window.dispatch_key(key) && observer.invocations == 1,
            "keyboard invokes the same command authority");
    (*command).set_enabled(false);
    require(!window.dispatch_key(key) && observer.invocations == 1,
            "disabled commands are also disabled through shortcuts");
    command.reset();
    require(!shortcut.connected() && !window.dispatch_key(key),
            "command destruction revokes the window's shortcut");
}

void test_choice_images_and_section_keyboard() {
    const std::shared_ptr<gf::ChoiceGroup> choices = gf::make_control<gf::ChoiceGroup>(gf::StableId("image-choices"));
    gf::Window window(choices, {100.0, 60.0});
    const std::array<std::byte, 4U> pixel{std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255}};
    const gf::ImageLoadResult loaded = window.load_bgra32_premultiplied(1U, 1U, 4U, pixel);
    require(static_cast<bool>(loaded), "image fixture must load");
    (*choices).set_items({{.id = 1, .label = "Red", .image = loaded.image, .accessible_name = "Red tile"}});
    require((*choices).item(0).image() == loaded.image &&
                (*choices).item(0).semantic_descriptor().name == "Red tile",
            "choice images and accessible names reach the retained item control");
    const std::shared_ptr<gf::ExpandableSections> sections = gf::make_control<gf::ExpandableSections>(gf::StableId("keyboard-sections"));
    const gf::Control::Ptr body = gf::make_control<gf::Control>(gf::StableId("keyboard-body"));
    (*sections).set_items({{.id = 1, .label = "Section", .content = body}});
    gf::Window section_window(sections, {200.0, 120.0});
    gf::Control& header = *(*sections).part("header", 0);
    section_window.flush();
    static_cast<void>(section_window.request_focus(header.shared_from_this()));
    static_cast<void>(section_window.dispatch_key({.action = gf::KeyAction::down, .physical_key = gf::PhysicalKey::space}));
    static_cast<void>(section_window.dispatch_key({.action = gf::KeyAction::up, .physical_key = gf::PhysicalKey::space}));
    require((*sections).expanded(0) && (*body).visible.type(),
            "keyboard activation must expand the section through its retained state");
}

void test_unlink_middle_subscription() {
    gf::Event<int> first{};
    std::unique_ptr<gf::Event<int>> middle = std::make_unique<gf::Event<int>>();
    gf::Event<int> last{};
    int total = 0;
    Recorder owner(total);
    gf::on(first, owner, &Recorder::add);
    gf::on(*middle, owner, &Recorder::add);
    gf::on(last, owner, &Recorder::add);
    middle.reset();
    require(owner.owned_subscription_count() == 2U,
            "unlinking a middle token must preserve both neighbors");
    first.emit(1);
    last.emit(2);
    owner.dispose();
    first.emit(4);
    last.emit(8);
    require(total == 3 && owner.owned_subscription_count() == 0U,
            "remaining subscriptions must invoke and revoke correctly");
}

void test_registration_failure_rollback() {
    std::size_t failure_count = 0U;
    bool reached_success = false;
    // The bound stops a broken failure loop; no expected allocation count is
    // baked in. Every fresh registration allocation is failed in turn.
    for (std::size_t fail_at = 0U; fail_at < 32U; ++fail_at) {
        gf::Event<int> event{};
        int total = 0;
        Recorder owner(total);
        bool failed = false;
        try {
            owned_event_allocation_probe::FaultScope fault(fail_at);
            gf::on(event, owner, &Recorder::add);
        } catch (const std::bad_alloc&) {
            failed = true;
        }
        event.emit(1);
        const gf::EventStatistics statistics = event.statistics();
        if (!failed) {
            require(total == 1, "successful registration must publish one handler");
            reached_success = true;
            break;
        }
        ++failure_count;
        require(total == 0 && statistics.subscriptions_connected ==
                                  statistics.subscriptions_disconnected,
                "failed registration must leave no connected callback or stale statistic");
        gf::on(event, owner, &Recorder::add);
        event.emit(2);
        require(total == 2, "registration must remain usable after allocation failure");
    }
    require(reached_success && failure_count != 0U,
            "fault sweep must cover every registration allocation and terminate");
    std::cout << "registration allocation failures covered: " << failure_count << '\n';
}

void test_allocation_free_emission() {
    gf::Event<int> event{};
    int total = 0;
    Recorder owner(total);
    gf::on(event, owner, &Recorder::add);
    constexpr std::size_t iterations = 10000U;
    std::size_t allocations = 0U;
    {
        owned_event_allocation_probe::FaultScope probe(
            std::numeric_limits<std::size_t>::max());
        for (std::size_t index = 0U; index < iterations; ++index) {
            event.emit(1);
        }
        allocations = owned_event_allocation_probe::attempts;
    }
    require(total == static_cast<int>(iterations) && allocations == 0U,
            "steady-state owned member dispatch must not allocate");
    std::cout << "owned member emissions: " << iterations
              << ", allocations: " << allocations << '\n';
}

} // namespace

int main() {
    try {
        test_signatures_and_natural_destruction();
        test_real_control_and_timer_signatures();
        test_component_protocol_is_explicit();
        test_disposal_during_emission();
        test_natural_teardown_cannot_resubscribe();
        test_nested_emission();
        test_callable_state_persists();
        test_publisher_and_target_destruction();
        test_exception_recovery_and_null_member();
        test_explicit_token_transfer_and_legacy_lifetime();
        test_short_lived_publishers_release_owner_storage();
        test_unlink_middle_subscription();
        test_nested_command_change_keeps_latest_state();
        test_command_and_bound_control_destruction_during_dispatch();
        test_command_shortcut_lifetime_and_enabled_state();
        test_choice_images_and_section_keyboard();
        test_mixed_revocation_order();
        test_command_bar_rebuild_and_keyboard();
        test_choice_group_state_and_rebuild();
        test_expandable_sections_state();
        test_shared_scalar_state();
        test_model_and_control_lifetimes();
        test_shared_commands();
        test_bound_values_and_omitted_arguments();
        test_registration_failure_rollback();
        test_allocation_free_emission();
        std::cout << "owned event tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "owned event tests failed: " << error.what() << '\n';
        return 1;
    }
}
