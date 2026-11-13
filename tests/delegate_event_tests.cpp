#include "gui_forms/component.hpp"
#include "gui_forms/event.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using gui_forms::Component;
using gui_forms::Delegate;
using gui_forms::Event;
using gui_forms::EventStatistics;
using gui_forms::SubscriptionToken;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class IntegerRecorder final {
public:
    void add(int value) { total += value; }
    int total{};
};

class ConstIntegerRecorder final {
public:
    explicit ConstIntegerRecorder(int& target) : target_(target) {}
    void add(int value) const { target_ += value; }

private:
    int& target_;
};

int free_function_total{};

void add_with_free_function(int value) {
    free_function_total += value;
}

class DisposingOwner final : public Component {
public:
    explicit DisposingOwner(std::vector<int>& order) : order_(order) {}

    void dispose_first() {
        order_.push_back(1);
        dispose();
    }

    void should_be_skipped() {
        order_.push_back(2);
    }

private:
    std::vector<int>& order_;
};

class SnapshotScenario final {
public:
    void connect() {
        first_ = event_.subscribe(
            Delegate<>::bind<SnapshotScenario, &SnapshotScenario::first>(*this));
        second_.emplace(event_.subscribe(
            Delegate<>::bind<SnapshotScenario, &SnapshotScenario::second>(*this)));
    }

    void emit() { event_.emit(); }

    [[nodiscard]] const std::vector<int>& order() const& noexcept { return order_; }
    const std::vector<int>& order() const&& = delete;
    [[nodiscard]] EventStatistics statistics() const noexcept {
        return event_.statistics();
    }

private:
    void first() {
        order_.push_back(1);
        (*second_).disconnect();
        if (!third_) {
            third_.emplace(event_.subscribe(
                Delegate<>::bind<SnapshotScenario, &SnapshotScenario::third>(
                    *this)));
        }
    }

    void second() { order_.push_back(2); }
    void third() { order_.push_back(3); }

    Event<> event_;
    SubscriptionToken first_;
    std::optional<SubscriptionToken> second_;
    std::optional<SubscriptionToken> third_;
    std::vector<int> order_;
};

class ThrowingRecorder final {
public:
    void fail() { throw std::runtime_error("delegate callback failure"); }
};

class AddToTotal final {
public:
    explicit AddToTotal(int& total) : total_(total) {}

    void operator()(int value) const { total_ += value; }

private:
    int& total_;
};

class EventOwnerDestruction final {
public:
    std::unique_ptr<Event<int>> event{std::make_unique<Event<int>>()};
    int calls{};
    void destroy(int) {
        ++calls;
        event.reset();
    }
    void must_not_run(int) { calls += 100; }
};

class ScopedSubscriber final : public Component {
public:
    explicit ScopedSubscriber(int& total) : total_(total) {}
    void add(int value) { total_ += value; }
private:
    int& total_;
};

class NestedEmission final {
public:
    Event<int> event;
    std::vector<int> order;
    SubscriptionToken added;
    void first(int depth) {
        order.push_back(10 + depth);
        if (depth == 0) {
            added = event.subscribe(
                Delegate<int>::bind<NestedEmission, &NestedEmission::third>(*this));
            event.emit(1);
        }
    }
    void second(int depth) { order.push_back(20 + depth); }
    void third(int depth) { order.push_back(30 + depth); }
};

void test_event_destruction_during_emission() {
    EventOwnerDestruction owner;
    const SubscriptionToken first = (*owner.event).subscribe(
        Delegate<int>::bind<EventOwnerDestruction,
                            &EventOwnerDestruction::destroy>(owner));
    const SubscriptionToken second = (*owner.event).subscribe(
        Delegate<int>::bind<EventOwnerDestruction,
                            &EventOwnerDestruction::must_not_run>(owner));
    (*owner.event).emit(1);
    require(owner.event == nullptr && owner.calls == 1 &&
                !first.connected() && !second.connected(),
            "event destruction must cancel pending callbacks and retain dispatch state");
}

void test_natural_owner_destruction_revokes_subscription() {
    Event<int> event;
    int total = 0;
    SubscriptionToken token;
    {
        ScopedSubscriber subscriber(total);
        token = event.subscribe(subscriber,
            Delegate<int>::bind<ScopedSubscriber, &ScopedSubscriber::add>(subscriber));
        event.emit(3);
    }
    require(!token.connected(), "natural Component destruction must revoke its slots");
    event.emit(5);
    require(total == 3, "a destroyed subscriber must never receive another callback");
}

void test_nested_emissions_have_independent_boundaries() {
    NestedEmission scenario;
    const SubscriptionToken first = scenario.event.subscribe(
        Delegate<int>::bind<NestedEmission, &NestedEmission::first>(scenario));
    const SubscriptionToken second = scenario.event.subscribe(
        Delegate<int>::bind<NestedEmission, &NestedEmission::second>(scenario));
    scenario.event.emit(0);
    require(scenario.order == std::vector<int>({10, 11, 21, 31, 20}),
            "new slots belong to the next nested emission, not the enclosing one");
}

void test_delegate_representation_and_binding() {
    static_assert(std::is_trivially_copyable_v<Delegate<int>>);
    static_assert(sizeof(Delegate<int>) == 2U * sizeof(void*));

    IntegerRecorder recorder;
    const Delegate<int> member =
        Delegate<int>::bind<IntegerRecorder, &IntegerRecorder::add>(recorder);
    member(3);

    int const_total = 0;
    const ConstIntegerRecorder const_recorder(const_total);
    const Delegate<int> const_member = Delegate<int>::bind<
        ConstIntegerRecorder, &ConstIntegerRecorder::add>(const_recorder);
    const_member(5);

    free_function_total = 0;
    const Delegate<int> free_function =
        Delegate<int>::bind<&add_with_free_function>();
    free_function(7);

    require(recorder.total == 3 && const_total == 5 && free_function_total == 7,
            "member, const-member, and free-function delegates must invoke exactly");
}

void test_delegate_event_owner_revocation() {
    Event<> event;
    std::vector<int> order;
    DisposingOwner owner(order);
    const SubscriptionToken first = event.subscribe(
        owner, Delegate<>::bind<DisposingOwner, &DisposingOwner::dispose_first>(
                   owner));
    const SubscriptionToken second = event.subscribe(
        owner, Delegate<>::bind<DisposingOwner, &DisposingOwner::should_be_skipped>(
                   owner));
    event.emit();
    require(order == std::vector<int>{1} && !first.connected() &&
                !second.connected(),
            "owner disposal inside a delegate must revoke and skip later slots");
}

void test_delegate_event_snapshot_and_statistics() {
    SnapshotScenario scenario;
    scenario.connect();
    scenario.emit();
    require(scenario.order() == std::vector<int>{1},
            "delegate emission must skip disconnected pending slots and defer additions");
    scenario.emit();
    require(scenario.order() == std::vector<int>({1, 1, 3}),
            "the next delegate emission must use current registration order");
    const EventStatistics statistics = scenario.statistics();
    require(statistics.subscriptions_connected == 3U &&
                statistics.subscriptions_disconnected == 1U &&
                statistics.callbacks_emitted == 3U,
            "delegate slots must preserve deterministic event statistics");
}

void test_delegate_event_exception_boundary() {
    Event<> event;
    ThrowingRecorder recorder;
    const SubscriptionToken token = event.subscribe(
        Delegate<>::bind<ThrowingRecorder, &ThrowingRecorder::fail>(recorder));
    bool propagated = false;
    try {
        event.emit();
    } catch (const std::runtime_error&) {
        propagated = true;
    }
    require(propagated && token.connected(),
            "delegate callbacks must preserve exception propagation and slot state");
}

void test_legacy_callback_compatibility() {
    Event<int> event;
    int total = 0;
    const SubscriptionToken token = event.subscribe(
        AddToTotal(total));
    event.emit(11);
    require(total == 11 && token.connected(),
            "the legacy std::function subscription overload must remain compatible");
}

} // namespace

int main() {
    try {
        test_delegate_representation_and_binding();
        test_delegate_event_owner_revocation();
        test_delegate_event_snapshot_and_statistics();
        test_delegate_event_exception_boundary();
        test_legacy_callback_compatibility();
        test_event_destruction_during_emission();
        test_natural_owner_destruction_revokes_subscription();
        test_nested_emissions_have_independent_boundaries();
        std::cout << "delegate event tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "delegate event tests failed: " << error.what() << '\n';
        return 1;
    }
}
