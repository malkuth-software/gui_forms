#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/dispatcher/operation/dispatch_operation.hpp"
#include "gui_forms/dispatcher/types/dispatcher_types.hpp"
#include "gui_forms/dispatcher/window_dispatcher/window_dispatcher.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/scheduler/frame_request_token/frame_request_token.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/timer/timer/timer.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"
#include "gui_forms/detail/bound_member_function.hpp"
#include "headless_host.hpp"
#include "support/named_callbacks.hpp"
#include "support/routed_input_probe.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace gui_forms;
namespace callbacks = gui_forms::test_support;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class NullPainter final : public Painter {
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

class DispatchPhaseProbe final : public gui_forms::tests::RoutedInputProbe<Control> {
public:
    DispatchPhaseProbe(StableId stable_id, std::vector<std::string>& trace)
        : gui_forms::tests::RoutedInputProbe<Control>(std::move(stable_id)), trace_(trace) {}

    Size measure(Size available) override {
        if (recording) trace_.push_back("measure");
        return Control::measure(available);
    }

    void arrange(Rect bounds) override {
        if (recording) trace_.push_back("arrange");
        Control::arrange(bounds);
    }

    void on_paint(Painter&, Rect) override {
        if (recording) trace_.push_back("paint");
    }

    void routed_pointer(PointerEvent& event) override {
        if (!recording || event.action != PointerAction::down) return;
        trace_.push_back("input");
        static_cast<void>(begin_invoke(detail::BoundMemberFunction<
            void (DispatchPhaseProbe::*)()>(
                *this, &DispatchPhaseProbe::apply_posted_pointer_mutation)));
        event.handled = true;
    }

    // An animation tick: records itself and damages the probe, as a
    // control's own UI timer does (ADR-016).
    void advance_frame() {
        if (recording) trace_.push_back("frame");
        invalidate(Dirty::paint);
    }

    bool recording{};

private:
    void apply_posted_pointer_mutation() {
        trace_.push_back("dispatch");
        const std::weak_ptr<Control> weak = weak_from_this();
        const std::shared_ptr<Control> control = weak.lock();
        if (control) {
            (*control).set_requested_bounds({0.0, 0.0, 319.0, 179.0});
        }
    }

    std::vector<std::string>& trace_;
};

template <typename Predicate>
void require_eventually(Predicate predicate, const char* message) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            throw std::runtime_error(message);
        }
        std::this_thread::yield();
    }
}

class PostNestedTrace final {
public:
    PostNestedTrace(Window& window, std::string& trace,
                    DispatchOperation& nested) noexcept
        : window_(window), trace_(trace), nested_(nested) {}

    void operator()() const {
        trace_ += 'A';
        nested_ = window_.begin_invoke(
            callbacks::AppendLiteral<>(trace_, "D"));
    }

private:
    Window& window_;
    std::string& trace_;
    DispatchOperation& nested_;
};

class RecordUiThread final {
public:
    RecordUiThread(std::atomic<bool>& result,
                   std::thread::id ui_thread) noexcept
        : result_(result), ui_thread_(ui_thread) {}

    void operator()() const {
        result_.store(std::this_thread::get_id() == ui_thread_,
                      std::memory_order_release);
    }

private:
    std::atomic<bool>& result_;
    std::thread::id ui_thread_;
};

class CrossThreadPost final {
public:
    CrossThreadPost(Window& window, const Control::Ptr& root,
                    std::atomic<bool>& saw_affinity,
                    std::atomic<bool>& callback_on_ui,
                    DispatchOperation& operation,
                    std::thread::id ui_thread) noexcept
        : window_(window), root_(root), saw_affinity_(saw_affinity),
          callback_on_ui_(callback_on_ui), operation_(operation),
          ui_thread_(ui_thread) {}

    void operator()() const {
        saw_affinity_.store(window_.invoke_required() &&
                                (*root_).invoke_required(),
                            std::memory_order_release);
        operation_ = (*root_).begin_invoke(
            RecordUiThread(callback_on_ui_, ui_thread_));
    }

private:
    Window& window_;
    const Control::Ptr& root_;
    std::atomic<bool>& saw_affinity_;
    std::atomic<bool>& callback_on_ui_;
    DispatchOperation& operation_;
    std::thread::id ui_thread_;
};

class RecordMarshalledCall final {
public:
    RecordMarshalledCall(std::string& trace, std::atomic<bool>& on_ui,
                         std::thread::id ui_thread) noexcept
        : trace_(trace), on_ui_(on_ui), ui_thread_(ui_thread) {}

    void operator()() const {
        trace_ += 'C';
        on_ui_.store(std::this_thread::get_id() == ui_thread_,
                     std::memory_order_release);
    }

private:
    std::string& trace_;
    std::atomic<bool>& on_ui_;
    std::thread::id ui_thread_;
};

class MarshalledInvokeWorker final {
public:
    MarshalledInvokeWorker(const Control::Ptr& child, std::string& trace,
                           std::atomic<bool>& on_ui,
                           std::atomic<bool>& returned,
                           std::thread::id ui_thread) noexcept
        : child_(child), trace_(trace), on_ui_(on_ui), returned_(returned),
          ui_thread_(ui_thread) {}

    void operator()() const {
        (*child_).invoke(RecordMarshalledCall(trace_, on_ui_, ui_thread_));
        returned_.store(true, std::memory_order_release);
    }

private:
    const Control::Ptr& child_;
    std::string& trace_;
    std::atomic<bool>& on_ui_;
    std::atomic<bool>& returned_;
    std::thread::id ui_thread_;
};

class HeadlessWakeAndPending final {
public:
    HeadlessWakeAndPending(host::HeadlessHost& host, Window& window) noexcept
        : host_(host), window_(window) {}
    bool operator()() const {
        return host_.dispatcher_wake_pending() &&
               window_.dispatcher_snapshot().pending == 1U;
    }

private:
    host::HeadlessHost& host_;
    Window& window_;
};

class OnePendingDispatch final {
public:
    explicit OnePendingDispatch(Window& window) noexcept : window_(window) {}
    bool operator()() const {
        return window_.dispatcher_snapshot().pending == 1U;
    }

private:
    Window& window_;
};

void do_nothing() {}

void throw_dispatcher_fixture() {
    throw std::runtime_error("dispatcher fixture fault");
}

void throw_inline_fault() {
    throw std::runtime_error("inline fault");
}

void throw_worker_fault() {
    throw std::runtime_error("worker fault");
}

class FaultedInvokeWorker final {
public:
    FaultedInvokeWorker(Window& window, std::string& message) noexcept
        : window_(window), message_(message) {}

    void operator()() const {
        try {
            window_.invoke(throw_worker_fault);
        } catch (const std::runtime_error& error) {
            message_ = error.what();
        }
    }

private:
    Window& window_;
    std::string& message_;
};

class OwnerInvokeWaiter final {
public:
    OwnerInvokeWaiter(const Control::Ptr& child,
                      std::atomic<bool>& cancelled) noexcept
        : child_(child), cancelled_(cancelled) {}

    void operator()() const {
        try {
            (*child_).invoke(do_nothing);
        } catch (const DispatchCancelledError&) {
            cancelled_.store(true, std::memory_order_release);
        }
    }

private:
    const Control::Ptr& child_;
    std::atomic<bool>& cancelled_;
};

class UnhostedInvokeWorker final {
public:
    UnhostedInvokeWorker(Window& window,
                         std::atomic<bool>& rejected) noexcept
        : window_(window), rejected_(rejected) {}

    void operator()() const {
        try {
            window_.invoke(do_nothing);
        } catch (const std::logic_error&) {
            rejected_.store(true, std::memory_order_release);
        }
    }

private:
    Window& window_;
    std::atomic<bool>& rejected_;
};

class ShutdownInvokeWaiter final {
public:
    ShutdownInvokeWaiter(Window& window,
                         std::atomic<bool>& cancelled) noexcept
        : window_(window), cancelled_(cancelled) {}

    void operator()() const {
        try {
            window_.invoke(do_nothing);
        } catch (const DispatchCancelledError&) {
            cancelled_.store(true, std::memory_order_release);
        }
    }

private:
    Window& window_;
    std::atomic<bool>& cancelled_;
};

struct DispatchRecord final {
    std::uint64_t sequence{};
    std::size_t producer{};
    std::size_t index{};
};

class RecordDispatchedItem final {
public:
    RecordDispatchedItem(std::vector<DispatchRecord>& observed,
                         std::shared_ptr<DispatchRecord> record) noexcept
        : observed_(observed), record_(std::move(record)) {}

    void operator()() const { observed_.push_back(*record_); }

private:
    std::vector<DispatchRecord>& observed_;
    std::shared_ptr<DispatchRecord> record_;
};

class DispatchProducer final {
public:
    DispatchProducer(Window& window, std::mutex& records_mutex,
                     std::vector<DispatchRecord>& expected,
                     std::vector<DispatchRecord>& observed,
                     std::size_t producer,
                     std::size_t callback_count) noexcept
        : window_(window), records_mutex_(records_mutex), expected_(expected),
          observed_(observed), producer_(producer),
          callback_count_(callback_count) {}

    void operator()() const {
        for (std::size_t index = 0U; index < callback_count_; ++index) {
            std::shared_ptr<DispatchRecord> record =
                std::make_shared<DispatchRecord>(
                    DispatchRecord{0U, producer_, index});
            DispatchOperation operation = window_.begin_invoke(
                RecordDispatchedItem(observed_, record));
            (*record).sequence = operation.sequence();
            std::scoped_lock<std::mutex> lock(records_mutex_);
            expected_.push_back(*record);
        }
    }

private:
    Window& window_;
    std::mutex& records_mutex_;
    std::vector<DispatchRecord>& expected_;
    std::vector<DispatchRecord>& observed_;
    std::size_t producer_;
    std::size_t callback_count_;
};

class DispatchRecordSequenceLess final {
public:
    bool operator()(const DispatchRecord& left,
                    const DispatchRecord& right) const noexcept {
        return left.sequence < right.sequence;
    }
};

// The probe's animation timer target. Observes the probe.
class ProbeFrameTick final {
public:
    explicit ProbeFrameTick(const std::shared_ptr<DispatchPhaseProbe>& probe) : probe_(probe) {}

    void operator()(FrameTime) const {
        const std::shared_ptr<DispatchPhaseProbe> live = probe_.lock();
        if (live) (*live).advance_frame();
    }

private:
    std::weak_ptr<DispatchPhaseProbe> probe_;
};

class TimerPostsDispatch final {
public:
    TimerPostsDispatch(std::vector<std::string>& trace,
                       const std::shared_ptr<DispatchPhaseProbe>& root) noexcept
        : trace_(trace), root_(root) {}

    void operator()() const {
        trace_.push_back("timer");
        static_cast<void>((*root_).begin_invoke(
            callbacks::PushConstant<std::vector<std::string>, std::string>(
                trace_, "timer-dispatch")));
    }

private:
    std::vector<std::string>& trace_;
    const std::shared_ptr<DispatchPhaseProbe>& root_;
};

void test_fifo_snapshot_and_nested_deferral() {
    std::shared_ptr<gui_forms::Control> root = make_control<Control>(StableId("dispatcher.root"));
    Window window(root, {320.0, 180.0});
    std::uint64_t wake_count = 0U;
    window.set_dispatch_wake_handler(
        callbacks::IncrementCounter<std::uint64_t>(wake_count));

    std::string trace;
    DispatchOperation nested;
    DispatchOperation first =
        window.begin_invoke(PostNestedTrace(window, trace, nested));
    DispatchOperation second = window.begin_invoke(
        callbacks::AppendLiteral<>(trace, "B"));
    DispatchOperation third = window.begin_invoke(
        callbacks::AppendLiteral<>(trace, "C"));
    require(trace.empty() && wake_count == 1U,
            "BeginInvoke must remain posted and coalesce one host wake");
    const DispatcherSnapshot queued = window.dispatcher_snapshot();
    require(queued.posted == 3U && queued.pending == 3U &&
                queued.maximum_pending == 3U && queued.wake_requests == 1U &&
                queued.coalesced_wakes == 2U,
            "dispatcher telemetry must describe the pending FIFO batch");

    const DispatchDrainResult first_turn = window.drain_posted_work();
    require(trace == "ABC" && first_turn.invoked == 3U &&
                first_turn.remaining == 1U && first_turn.wake_requested &&
                wake_count == 2U && first.state() == DispatchOperationState::completed &&
                second.state() == DispatchOperationState::completed &&
                third.state() == DispatchOperationState::completed && nested.pending(),
            "a dispatch turn must use a FIFO snapshot and defer nested work");
    const DispatchDrainResult second_turn = window.drain_posted_work();
    require(trace == "ABCD" && second_turn.invoked == 1U &&
                second_turn.remaining == 0U &&
                nested.state() == DispatchOperationState::completed,
            "nested BeginInvoke work must run on the next explicit turn");
}

void test_cancellation_owner_lifetime_and_fault_isolation() {
    std::shared_ptr<gui_forms::Control> root = make_control<Control>(StableId("dispatcher.owner.root"));
    std::shared_ptr<gui_forms::Control> child = make_control<Control>(StableId("dispatcher.owner.child"));
    (*root).add_child(child);
    Window window(root, {320.0, 180.0});

    std::uint64_t callbacks = 0U;
    DispatchOperation explicit_cancel = window.begin_invoke(
        callbacks::IncrementCounter<std::uint64_t>(callbacks));
    require(explicit_cancel.cancel() && !explicit_cancel.cancel(),
            "posted cancellation must be explicit and idempotent");
    DispatchOperation owner_cancel = (*child).begin_invoke(
        callbacks::IncrementCounter<std::uint64_t>(callbacks));
    const Control::Ptr detached = (*root).remove_child((*child).runtime_id());
    require(detached == child && !(*child).attached() && !(*child).invoke_required(),
            "detached controls must lose dispatcher affinity atomically");

    DispatchOperation fault = window.begin_invoke(throw_dispatcher_fixture);
    DispatchOperation survivor = window.begin_invoke(
        callbacks::IncrementCounter<std::uint64_t>(callbacks));
    const DispatchDrainResult result = window.drain_posted_work();
    require(callbacks == 1U && result.invoked == 1U && result.cancelled == 2U &&
                result.faulted == 1U &&
                explicit_cancel.state() == DispatchOperationState::cancelled &&
                owner_cancel.state() == DispatchOperationState::cancelled &&
                fault.state() == DispatchOperationState::faulted &&
                fault.exception() != nullptr &&
                survivor.state() == DispatchOperationState::completed,
            "cancelled/ownerless/faulted work must not suppress later callbacks");

    bool detached_rejected = false;
    try {
        static_cast<void>((*child).begin_invoke(do_nothing));
    } catch (const std::logic_error&) {
        detached_rejected = true;
    }
    require(detached_rejected,
            "BeginInvoke on a detached control must reject instead of orphaning work");
}

// A callable small enough for std::function's inline buffer, owning a
// sentinel so the test can see when the callable has been destroyed.
class HoldSentinel final {
public:
    explicit HoldSentinel(std::shared_ptr<int> sentinel)
        : sentinel_(std::move(sentinel)) {}

    void operator()() const {}

private:
    std::shared_ptr<int> sentinel_;
};

void test_completed_work_releases_its_callable() {
    std::shared_ptr<gui_forms::Control> root = make_control<Control>(StableId("dispatcher.release.root"));
    Window window(root, {320.0, 180.0});
    std::shared_ptr<int> sentinel = std::make_shared<int>(7);
    const std::weak_ptr<int> observer = sentinel;
    DispatchOperation operation = window.begin_invoke(HoldSentinel(std::move(sentinel)));
    require(!observer.expired(), "posted work must own its callable until it runs");
    const DispatchDrainResult result = window.drain_posted_work();
    require(result.invoked == 1U &&
                operation.state() == DispatchOperationState::completed &&
                observer.expired(),
            "completed work must release its callable while the operation is still held");
}

void test_cross_thread_post_and_headless_pump() {
    std::shared_ptr<gui_forms::Control> root = make_control<Control>(StableId("dispatcher.thread.root"));
    Window window(root, {320.0, 180.0});
    host::HeadlessHost host(window);
    std::atomic<bool> worker_saw_affinity{false};
    std::atomic<bool> callback_on_ui{false};
    DispatchOperation operation;
    const std::thread::id ui_thread = std::this_thread::get_id();
    std::thread worker(CrossThreadPost(
        window, root, worker_saw_affinity, callback_on_ui, operation,
        ui_thread));
    worker.join();
    require(worker_saw_affinity.load(std::memory_order_acquire) &&
                operation.pending() &&
                !callback_on_ui.load(std::memory_order_acquire),
            "worker BeginInvoke must publish affinity without executing inline");
    const DispatchDrainResult result = host.pump_dispatcher();
    require(result.invoked == 1U &&
                callback_on_ui.load(std::memory_order_acquire) &&
                operation.state() == DispatchOperationState::completed,
            "headless pump must marshal worker work to the UI thread");
}

void test_synchronous_invoke_inline_marshal_fault_and_owner_cancel() {
    std::shared_ptr<gui_forms::Control> root = make_control<Control>(StableId("dispatcher.invoke.root"));
    std::shared_ptr<gui_forms::Control> child = make_control<Control>(StableId("dispatcher.invoke.child"));
    (*root).add_child(child);
    Window window(root, {320.0, 180.0});
    host::HeadlessHost host(window);

    std::string trace;
    window.invoke(callbacks::AppendLiteral<>(trace, "A"));
    (*child).invoke(callbacks::AppendLiteral<>(trace, "B"));
    bool inline_fault = false;
    try {
        window.invoke(throw_inline_fault);
    } catch (const std::runtime_error& error) {
        inline_fault = std::string(error.what()) == "inline fault";
    }
    require(trace == "AB" && inline_fault &&
                window.dispatcher_snapshot().inline_invocations == 3U,
            "UI-thread Invoke must execute inline and preserve exception identity");

    std::atomic<bool> marshalled_returned{false};
    std::atomic<bool> marshalled_on_ui{false};
    const std::thread::id ui_thread = std::this_thread::get_id();
    std::thread marshalled(MarshalledInvokeWorker(
        child, trace, marshalled_on_ui, marshalled_returned, ui_thread));
    require_eventually(
        HeadlessWakeAndPending(host, window),
        "worker Invoke did not publish a bounded headless wake");
    require(!marshalled_returned.load(std::memory_order_acquire),
            "worker Invoke must block without pumping a nested loop");
    const DispatchDrainResult marshalled_turn = host.pump_dispatcher();
    marshalled.join();
    require(marshalled_turn.invoked == 1U && trace == "ABC" &&
                marshalled_on_ui.load(std::memory_order_acquire) &&
                marshalled_returned.load(std::memory_order_acquire),
            "headless host must release worker Invoke only after UI execution");

    std::string worker_fault;
    std::thread faulted(FaultedInvokeWorker(window, worker_fault));
    require_eventually(
        OnePendingDispatch(window),
        "faulting worker Invoke was not queued");
    const DispatchDrainResult fault_turn = host.pump_dispatcher();
    faulted.join();
    require(fault_turn.faulted == 1U && worker_fault == "worker fault",
            "worker Invoke must rethrow the original callback exception on its caller");

    std::atomic<bool> owner_cancelled{false};
    std::thread owner_waiter(OwnerInvokeWaiter(child, owner_cancelled));
    require_eventually(
        OnePendingDispatch(window),
        "owned worker Invoke was not queued");
    const Control::Ptr detached = (*root).remove_child((*child).runtime_id());
    const DispatchDrainResult cancelled_turn = host.pump_dispatcher();
    owner_waiter.join();
    const DispatcherSnapshot snapshot = window.dispatcher_snapshot();
    require(detached == child && cancelled_turn.cancelled == 1U &&
                owner_cancelled.load(std::memory_order_acquire) &&
                snapshot.synchronous_invocations == 6U &&
                snapshot.inline_invocations == 3U &&
                snapshot.marshalled_invocations == 3U,
            "owner detachment must release synchronous waiters with cancellation");
}

void test_synchronous_invoke_host_and_shutdown_guards() {
    std::shared_ptr<gui_forms::Control> unhosted_root = make_control<Control>(
        StableId("dispatcher.invoke.unhosted"));
    Window unhosted(unhosted_root, {200.0, 100.0});
    std::atomic<bool> unhosted_rejected{false};
    std::thread no_host(
        UnhostedInvokeWorker(unhosted, unhosted_rejected));
    no_host.join();
    require(unhosted_rejected.load(std::memory_order_acquire) &&
                unhosted.dispatcher_snapshot().pending == 0U,
            "worker Invoke without a running host must reject instead of deadlocking");

    std::shared_ptr<gui_forms::Control> root = make_control<Control>(StableId("dispatcher.invoke.shutdown"));
    Window window(root, {200.0, 100.0});
    host::HeadlessHost host(window);
    std::atomic<bool> shutdown_cancelled{false};
    std::thread waiter(ShutdownInvokeWaiter(window, shutdown_cancelled));
    require_eventually(
        OnePendingDispatch(window),
        "shutdown fixture worker Invoke was not queued");
    bool removal_rejected = false;
    try {
        window.set_dispatch_wake_handler({});
    } catch (const std::logic_error&) {
        removal_rejected = true;
    }
    window.shutdown_dispatcher();
    waiter.join();
    require(removal_rejected &&
                shutdown_cancelled.load(std::memory_order_acquire) &&
                !window.dispatcher_snapshot().accepting,
            "wake removal/shutdown must not strand a synchronous waiter");
}

class RepeatedInvokeWorker final {
public:
    RepeatedInvokeWorker(Window& window, std::atomic<unsigned>& completed) noexcept
        : window_(window), completed_(completed) {}
    void operator()() const {
        for (unsigned index = 0U; index < 2000U; ++index) {
            window_.invoke(do_nothing);
            completed_.fetch_add(1U, std::memory_order_release);
        }
    }
private:
    Window& window_;
    std::atomic<unsigned>& completed_;
};

void test_completion_races_with_wait_entry() {
    const Control::Ptr root = make_control<Control>(StableId("dispatcher.wait.race"));
    Window window(root, {200.0, 100.0});
    host::HeadlessHost host(window);
    std::atomic<unsigned> completed{0U};
    std::thread worker(RepeatedInvokeWorker(window, completed));
    while (completed.load(std::memory_order_acquire) != 2000U) {
        static_cast<void>(host.pump_dispatcher());
        std::this_thread::yield();
    }
    worker.join();
    require(window.dispatcher_snapshot().invoked == 2000U,
            "every synchronous completion must release its waiter");
}

void test_concurrent_producer_fifo_and_turn_bound() {
    std::shared_ptr<gui_forms::Control> root = make_control<Control>(StableId("dispatcher.stress.root"));
    Window window(root, {320.0, 180.0});
    constexpr std::size_t producer_count = 8U;
    constexpr std::size_t callbacks_per_producer = 129U;
    constexpr std::size_t callback_count =
        producer_count * callbacks_per_producer;
    std::mutex records_mutex;
    std::vector<DispatchRecord> expected;
    expected.reserve(callback_count);
    std::vector<DispatchRecord> observed;
    observed.reserve(callback_count);
    std::vector<std::thread> producers;
    producers.reserve(producer_count);
    for (std::size_t producer = 0U; producer < producer_count; ++producer) {
        producers.emplace_back(DispatchProducer(
            window, records_mutex, expected, observed, producer,
            callbacks_per_producer));
    }
    for (std::thread& producer : producers) producer.join();
    std::sort(expected.begin(), expected.end(), DispatchRecordSequenceLess());
    require(expected.size() == callback_count &&
                window.dispatcher_snapshot().pending == callback_count,
            "all concurrent producers must publish one bounded global FIFO");

    const DispatchDrainResult first = window.drain_posted_work();
    require(first.invoked == maximum_callbacks_per_dispatch_turn &&
                first.remaining == callback_count -
                    maximum_callbacks_per_dispatch_turn,
            "a saturated turn must stop at the public callback bound");
    const DispatchDrainResult second = window.drain_posted_work();
    require(second.invoked == callback_count -
                    maximum_callbacks_per_dispatch_turn &&
                second.remaining == 0U && observed.size() == callback_count,
            "the next turn must drain the exact saturated remainder");
    for (std::size_t index = 0U; index < callback_count; ++index) {
        require(observed[index].sequence == expected[index].sequence &&
                    observed[index].producer == expected[index].producer &&
                    observed[index].index == expected[index].index,
                "concurrent posting must execute in assigned global sequence order");
    }
    for (std::size_t producer = 0U; producer < producer_count; ++producer) {
        std::size_t prior = 0U;
        bool seen = false;
        for (const DispatchRecord& record : observed) {
            if (record.producer != producer) continue;
            require(!seen || record.index == prior + 1U,
                    "each producer's program order must survive interleaving");
            prior = record.index;
            seen = true;
        }
    }
}

void test_dispatch_order_against_input_timer_layout_and_paint() {
    std::vector<std::string> trace;
    std::shared_ptr<DispatchPhaseProbe> root =
        make_control<DispatchPhaseProbe>(
        StableId("dispatcher.phase.root"), trace);
    (*root).focusable = true;
    Window window(root, {320.0, 180.0});
    host::HeadlessHost host(window);
    NullPainter painter;
    window.flush();
    window.paint(painter);
    trace.clear();
    (*root).recording = true;

    require(window.dispatch_pointer(
                {PointerAction::down, PointerButton::primary, {20.0, 20.0}}) &&
                trace == std::vector<std::string>{"input"} &&
                window.dispatcher_snapshot().pending == 1U,
            "routed input must finish before work it posts can execute");
    static_cast<void>(host.pump_dispatcher());
    require(trace == std::vector<std::string>({"input", "dispatch"}),
            "posted input work must execute on the following dispatcher turn");
    window.flush();
    require(trace.size() >= 4U && trace[2] == "measure" &&
                trace[3] == "arrange",
            "dispatch mutations must enter layout only after the callback completes");
    window.paint(painter);
    require(!trace.empty() && trace.back() == "paint",
            "paint must consume the layout committed by posted work");

    trace.clear();
    Timer timer(window, std::chrono::milliseconds(1));
    SubscriptionToken tick =
        timer.tick().subscribe(timer, TimerPostsDispatch(trace, root));
    const FrameTime due = FrameClock::now() + std::chrono::milliseconds(1);
    timer.start_at(due);
    const FramePollResult timer_turn = window.poll_frame_schedule(due);
    require(timer_turn.ui_timer_ticks == 1U &&
                trace == std::vector<std::string>{"timer"} &&
                window.dispatcher_snapshot().pending == 1U,
            "timer callbacks must finish before their posted work runs");
    static_cast<void>(host.pump_dispatcher());
    require(trace == std::vector<std::string>({"timer", "timer-dispatch"}),
            "timer-posted work must run in a later dispatcher turn");
    timer.stop();
    tick.disconnect();

    trace.clear();
    const FrameTime frame_due = due + std::chrono::milliseconds(16);
    FrameRequestToken animation = window.schedule_ui_timer(
        *root, std::chrono::milliseconds(16), frame_due, ProbeFrameTick{root});
    const FramePollResult frame_turn = window.poll_frame_schedule(frame_due);
    require(frame_turn.ui_timer_ticks == 1U &&
                trace == std::vector<std::string>{"frame"},
            "animation state must advance before its paint is presented");
    window.paint(painter);
    require(trace == std::vector<std::string>({"frame", "paint"}),
            "the retained frame callback must precede its visible paint");
    animation.disconnect();
}

void test_bounds_and_shutdown_revocation() {
    std::shared_ptr<gui_forms::Control> root = make_control<Control>(StableId("dispatcher.bound.root"));
    Window window(root, {320.0, 180.0});
    std::vector<DispatchOperation> operations;
    operations.reserve(maximum_posted_callbacks);
    for (std::size_t index = 0U; index < maximum_posted_callbacks; ++index) {
        operations.push_back(window.begin_invoke(do_nothing));
    }
    bool bound_rejected = false;
    try {
        static_cast<void>(window.begin_invoke(do_nothing));
    } catch (const std::length_error&) {
        bound_rejected = true;
    }
    require(bound_rejected &&
                window.dispatcher_snapshot().pending == maximum_posted_callbacks,
            "dispatcher must reject work beyond its declared queue bound");

    window.shutdown_dispatcher();
    const DispatcherSnapshot stopped = window.dispatcher_snapshot();
    require(!stopped.accepting && stopped.pending == 0U &&
                stopped.cancelled == maximum_posted_callbacks &&
                operations.front().state() == DispatchOperationState::cancelled &&
                operations.back().state() == DispatchOperationState::cancelled,
            "shutdown must synchronously revoke every pending operation");
    bool shutdown_rejected = false;
    try {
        static_cast<void>(window.begin_invoke(do_nothing));
    } catch (const std::logic_error&) {
        shutdown_rejected = true;
    }
    require(shutdown_rejected,
            "a shut-down dispatcher must reject new work deterministically");
}

// A host's wake that stays inside the call until released, recording where
// it is: 0 not called, 1 inside, 2 returned.
struct SlowWake final {
    std::atomic<int>* phase{};
    std::atomic<bool>* release{};
    void operator()() const {
        (*phase).store(1, std::memory_order_release);
        while (!(*release).load(std::memory_order_acquire)) std::this_thread::yield();
        (*phase).store(2, std::memory_order_release);
    }
};

struct WakeEntered final {
    const std::atomic<int>* phase{};
    bool operator()() const { return (*phase).load(std::memory_order_acquire) == 1; }
};

class PostingWorker final {
public:
    explicit PostingWorker(Window& window) noexcept : window_(window) {}
    void operator()() const { static_cast<void>(window_.begin_invoke(do_nothing)); }

private:
    Window& window_;
};

// Lets the wake return a while after it entered, so that the UI thread is
// already removing the handler when it does. The wait only widens the window
// in which a removal that did not wait would be caught (N104): a correct
// removal returns after the wake however late the release comes, so a slow
// runner cannot fail the test.
struct LateRelease final {
    const std::atomic<int>* phase{};
    std::atomic<bool>* release{};
    void operator()() const {
        while ((*phase).load(std::memory_order_acquire) != 1) std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        (*release).store(true, std::memory_order_release);
    }
};

// A worker's post calls the host's wake. Removing the handler on the UI
// thread must wait for a wake already under way, so that the host may be
// destroyed as soon as the removal returns (the gallery's dispatcher page
// once woke a destroyed headless host from its worker).
void test_wake_removal_waits_for_a_wake_under_way() {
    std::shared_ptr<gui_forms::Control> root = make_control<Control>(StableId("dispatcher.wake.root"));
    Window window(root, {320.0, 180.0});
    std::atomic<int> phase{0};
    std::atomic<bool> release{false};
    window.set_dispatch_wake_handler(SlowWake{&phase, &release});
    std::thread worker{PostingWorker{window}};
    require_eventually(WakeEntered{&phase}, "the worker's post calls the host's wake");
    std::thread releaser(LateRelease{&phase, &release});
    window.set_dispatch_wake_handler({});
    const int after_removal = phase.load(std::memory_order_acquire);
    worker.join();
    releaser.join();
    require(after_removal == 2, "removing the wake handler waits for the wake already under way");
    window.shutdown_dispatcher();
}

// A host wake that throws on its first call, then counts.
struct FailingOnceWake final {
    std::shared_ptr<int> calls;
    std::shared_ptr<int> wakes;
    void operator()() const {
        ++(*calls);
        if ((*calls) == 1) throw std::runtime_error("the wake could not allocate");
        ++(*wakes);
    }
};

// N45: a wake that throws must not leave wake_pending set. Before the fix the
// flag stayed true, every later post counted as coalesced, and the host was
// never woken again.
void test_throwing_wake_leaves_the_dispatcher_awake() {
    std::shared_ptr<gui_forms::Control> root = make_control<Control>(StableId("dispatcher.throwing.root"));
    Window window(root, {320.0, 180.0});
    std::shared_ptr<int> calls = std::make_shared<int>(0);
    std::shared_ptr<int> wakes = std::make_shared<int>(0);
    window.set_dispatch_wake_handler(FailingOnceWake{calls, wakes});
    std::string trace;
    bool thrown = false;
    try {
        static_cast<void>(window.begin_invoke(callbacks::AppendLiteral<>(trace, "lost")));
    } catch (const std::runtime_error&) {
        thrown = true;
    }
    require(thrown, "a throwing wake fails the post");
    const DispatcherSnapshot failed = window.dispatcher_snapshot();
    require(failed.pending == 0U && failed.posted == 0U && failed.wake_requests == 0U,
            "a failed post leaves nothing queued and no counts behind");
    DispatchOperation posted = window.begin_invoke(callbacks::AppendLiteral<>(trace, "A"));
    require((*calls) == 2 && (*wakes) == 1, "the next post wakes the host again");
    const DispatcherSnapshot queued = window.dispatcher_snapshot();
    require(queued.pending == 1U && queued.wake_requests == 1U && queued.coalesced_wakes == 0U,
            "the next post is not counted as coalesced");
    static_cast<void>(window.drain_posted_work());
    require(trace == "A" && posted.state() == DispatchOperationState::completed,
            "only the successful post runs");
    window.set_dispatch_wake_handler({});
    window.shutdown_dispatcher();
}

// -- WindowDispatcher: a handle that outlives its window -------------------------
//
// The UI Automation Gallery client (windows_accessibility_uia_gallery_tests)
// once called Window::invoke from its thread after the host had destroyed
// the window, a heap use after free. These cases reproduce that order
// deterministically, with no host or contention: the handle reports the
// closed window and touches nothing of it.

struct HandleInvokeRecord final {
    std::atomic<bool> ran{false};
    std::atomic<bool> returned{false};
    DispatchInvokeResult result{DispatchInvokeResult::completed};
};

class MarkRan final {
public:
    explicit MarkRan(std::atomic<bool>& ran) noexcept : ran_(ran) {}
    void operator()() const { ran_.store(true, std::memory_order_release); }

private:
    std::atomic<bool>& ran_;
};

// Invokes through the handle once from its own thread and records the result.
class HandleInvokeWorker final {
public:
    HandleInvokeWorker(WindowDispatcher dispatcher, HandleInvokeRecord& record) noexcept
        : dispatcher_(std::move(dispatcher)), record_(record) {}

    void operator()() const {
        record_.result = dispatcher_.invoke(MarkRan(record_.ran));
        record_.returned.store(true, std::memory_order_release);
    }

private:
    WindowDispatcher dispatcher_;
    HandleInvokeRecord& record_;
};

// From its own thread, after the window is gone: invoke, begin_invoke and
// window_alive all answer without touching the window.
struct GoneWindowAnswers final {
    DispatchInvokeResult invoked{DispatchInvokeResult::completed};
    DispatchOperationState posted{DispatchOperationState::invalid};
    bool alive{true};
    std::atomic<bool> ran{false};
};

class GoneWindowWorker final {
public:
    GoneWindowWorker(WindowDispatcher dispatcher, GoneWindowAnswers& answers) noexcept
        : dispatcher_(std::move(dispatcher)), answers_(answers) {}

    void operator()() const {
        answers_.invoked = dispatcher_.invoke(MarkRan(answers_.ran));
        const DispatchOperation operation = dispatcher_.begin_invoke(MarkRan(answers_.ran));
        answers_.posted = operation.state();
        answers_.alive = dispatcher_.window_alive();
    }

private:
    WindowDispatcher dispatcher_;
    GoneWindowAnswers& answers_;
};

class HandlePending final {
public:
    explicit HandlePending(Window& window) noexcept : window_(window) {}
    bool operator()() const { return window_.dispatcher_snapshot().pending == 1U; }

private:
    Window& window_;
};

void test_window_dispatcher_outlives_its_window() {
    // A live window: marshalled and inline calls complete.
    {
        std::shared_ptr<Control> root = make_control<Control>(StableId("dispatcher.handle.live"));
        Window window(root, {200.0, 100.0});
        host::HeadlessHost host(window);
        const WindowDispatcher dispatcher = window.dispatcher();
        HandleInvokeRecord marshalled;
        std::thread worker(HandleInvokeWorker(dispatcher, marshalled));
        require_eventually(HandlePending(window), "the handle's worker Invoke was not queued");
        const DispatchDrainResult turn = host.pump_dispatcher();
        worker.join();
        std::atomic<bool> inline_ran{false};
        const DispatchInvokeResult inline_result = dispatcher.invoke(MarkRan(inline_ran));
        require(turn.invoked == 1U && marshalled.result == DispatchInvokeResult::completed &&
                    marshalled.ran.load(std::memory_order_acquire) &&
                    inline_result == DispatchInvokeResult::completed &&
                    inline_ran.load(std::memory_order_acquire) && dispatcher.window_alive(),
                "a live window's handle runs marshalled and inline work");
        window.shutdown_dispatcher();
    }

    // Work waiting when the window closes and is destroyed: the waiter
    // returns window_gone and the callback never runs.
    WindowDispatcher kept;
    HandleInvokeRecord waiting;
    std::thread waiter;
    {
        std::shared_ptr<Control> root = make_control<Control>(StableId("dispatcher.handle.closing"));
        std::unique_ptr<Window> window = std::make_unique<Window>(root, Size{200.0, 100.0});
        std::unique_ptr<host::HeadlessHost> host = std::make_unique<host::HeadlessHost>(*window);
        kept = (*window).dispatcher();
        waiter = std::thread(HandleInvokeWorker(kept, waiting));
        require_eventually(HandlePending(*window), "the waiting Invoke was not queued");
        // The host's close order: shut the dispatcher down, then the host,
        // then the window.
        (*window).shutdown_dispatcher();
        host.reset();
        window.reset();
    }
    waiter.join();
    require(waiting.returned.load(std::memory_order_acquire) &&
                waiting.result == DispatchInvokeResult::window_gone &&
                !waiting.ran.load(std::memory_order_acquire),
            "work waiting when the window closes returns window_gone and does not run");

    // After the window is destroyed: from another thread (the client's
    // order that freed memory before) and from the UI thread.
    GoneWindowAnswers answers;
    std::thread late(GoneWindowWorker(kept, answers));
    late.join();
    std::atomic<bool> ui_ran{false};
    const DispatchInvokeResult ui_result = kept.invoke(MarkRan(ui_ran));
    require(answers.invoked == DispatchInvokeResult::window_gone &&
                answers.posted == DispatchOperationState::cancelled && !answers.alive &&
                !answers.ran.load(std::memory_order_acquire) &&
                ui_result == DispatchInvokeResult::window_gone && !ui_ran.load(std::memory_order_acquire),
            "a destroyed window's handle reports window_gone and runs nothing");

    // An empty handle is a programming error.
    const WindowDispatcher empty;
    bool refused = false;
    try {
        static_cast<void>(empty.invoke(do_nothing));
    } catch (const std::logic_error&) {
        refused = true;
    }
    require(refused && !empty.window_alive(), "an empty handle refuses to post");
}

} // namespace

int main() {
    try {
        test_fifo_snapshot_and_nested_deferral();
        test_cancellation_owner_lifetime_and_fault_isolation();
        test_completed_work_releases_its_callable();
        test_cross_thread_post_and_headless_pump();
        test_synchronous_invoke_inline_marshal_fault_and_owner_cancel();
        test_synchronous_invoke_host_and_shutdown_guards();
        test_completion_races_with_wait_entry();
        test_concurrent_producer_fifo_and_turn_bound();
        test_dispatch_order_against_input_timer_layout_and_paint();
        test_bounds_and_shutdown_revocation();
        test_wake_removal_waits_for_a_wake_under_way();
        test_throwing_wake_leaves_the_dispatcher_awake();
        test_window_dispatcher_outlives_its_window();
        std::cout << "dispatcher-tests=pass\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "dispatcher-tests=fail reason=" << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
