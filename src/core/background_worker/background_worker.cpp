#include "gui_forms/background_worker/background_worker.hpp"

#include "../dispatcher/state/dispatcher_state.hpp"

#include "gui_forms/window.hpp"

#include <array>
#include <chrono>
#include <functional>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace gui_forms::detail {

// The UI-side target of a worker's posted callbacks. The worker clears
// `owner` at disposal; a callback that arrives later finds none and drops.
struct BackgroundWorkerTarget final {
    BackgroundWorker* owner{};
};

// One run. The UI thread fills the fields above `result` before the worker
// starts and never changes them. The work writes `result`, `error` and
// `cancelled` on the worker thread before it posts the completion; the UI
// thread reads them only in that completion, after the dispatcher's mutex has
// ordered the two. `abandoned` and `stopping` are the fields both threads
// touch while the run is live.
struct BackgroundWorkerRun final {
    std::weak_ptr<BackgroundWorkerRun> self{};
    std::weak_ptr<BackgroundWorkerTarget> target{};
    std::shared_ptr<DispatcherState> dispatcher{};
    BackgroundWorker::Work work{};
    BindingValue argument{};
    bool reports_progress{};
    BindingValue result{};
    std::exception_ptr error{};
    bool cancelled{};
    // Set by the worker thread when the dispatcher refused the completion
    // (its window shut down): the run then ends without an event.
    std::atomic<bool> abandoned{false};
    // Set by the UI thread before it joins the worker thread (disposal, the
    // destructor, the next run). The worker then posts nothing more: a
    // completion waiting for room in a full queue would otherwise wait for
    // ever on a UI thread that is blocked in the join and cannot drain it,
    // and anything posted would be dropped, its owner gone.
    std::atomic<bool> stopping{false};
};

// A posted callback: a progress report, or the completion when `progress`
// is empty. Runs on the UI thread from the window's dispatcher.
struct BackgroundWorkerDelivery final {
    std::shared_ptr<BackgroundWorkerRun> run{};
    std::optional<BackgroundWorkerProgress> progress{};

    void operator()() const {
        const std::shared_ptr<BackgroundWorkerTarget> target = (*run).target.lock();
        BackgroundWorker* const owner = target ? (*target).owner : nullptr;
        if (owner == nullptr || !(*owner).is_alive()) return;
        if (progress) {
            (*owner).deliver_progress(run, *progress);
        } else {
            (*owner).deliver_completion(run);
        }
    }
};

namespace {

// Posts a callback from the worker thread. Returns false when the
// dispatcher no longer accepts work or the UI thread is stopping the run. A
// full queue is waited out, one millisecond at a time, only when
// `wait_for_room` is set: a completion must arrive, a progress report may be
// dropped. The wait ends when the UI thread starts stopping the run.
bool post_from_worker(const BackgroundWorkerRun& run, BackgroundWorkerDelivery delivery, bool wait_for_room) {
    for (;;) {
        if (run.stopping.load(std::memory_order_acquire)) return false;
        try {
            DispatchOperation operation = post_dispatch(run.dispatcher, {}, false,
                                                        std::function<void()>(delivery));
            static_cast<void>(operation);
            return true;
        } catch (const std::length_error&) {
            if (!wait_for_room) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } catch (const std::logic_error&) {
            return false;
        }
    }
}

std::string exception_message(const std::exception_ptr& error) {
    if (!error) return {};
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& thrown) {
        return thrown.what();
    } catch (...) {
        return "unknown exception";
    }
}

} // namespace
} // namespace gui_forms::detail

namespace gui_forms {

// -- task --------------------------------------------------------------------

const BindingValue& BackgroundWorkerTask::argument() const noexcept {
    return (*run_).argument;
}

void BackgroundWorkerTask::set_result(BindingValue result) {
    (*run_).result = std::move(result);
}

bool BackgroundWorkerTask::cancellation_pending() const noexcept {
    return (*cancellation_).requested();
}

void BackgroundWorkerTask::set_cancelled(bool cancelled) noexcept {
    (*run_).cancelled = cancelled;
}

void BackgroundWorkerTask::report_progress(int percent, BindingValue user_state) {
    if (!(*run_).reports_progress) {
        throw std::logic_error("GUI.Forms BackgroundWorker does not report progress");
    }
    if (percent < 0 || percent > 100) {
        throw std::out_of_range("GUI.Forms BackgroundWorker progress must be between 0 and 100");
    }
    detail::BackgroundWorkerDelivery delivery{};
    delivery.run = (*run_).self.lock();
    delivery.progress = BackgroundWorkerProgress{percent, std::move(user_state)};
    static_cast<void>(detail::post_from_worker(*run_, std::move(delivery), false));
}

// -- worker ------------------------------------------------------------------

BackgroundWorker::BackgroundWorker(Window& window)
    : window_lifetime_(window.lifetime_), dispatcher_(window.dispatcher_state_),
      target_(std::make_shared<detail::BackgroundWorkerTarget>()) {
    window.verify_access("BackgroundWorker construction");
    (*target_).owner = this;
}

BackgroundWorker::~BackgroundWorker() {
    if (target_) (*target_).owner = nullptr;
    stop_run();
}

Window* BackgroundWorker::bound_window() const noexcept {
    const std::shared_ptr<detail::WindowLifetime> lifetime = window_lifetime_.lock();
    return lifetime ? (*lifetime).window : nullptr;
}

void BackgroundWorker::require_ui(std::string_view operation) const {
    if (!is_alive()) throw std::logic_error("GUI.Forms cannot use a disposed BackgroundWorker");
    Window* owner = bound_window();
    if (owner == nullptr) throw std::logic_error("GUI.Forms cannot use a BackgroundWorker after Window shutdown");
    (*owner).verify_access(operation);
}

void BackgroundWorker::set_do_work(Work work) {
    require_ui("BackgroundWorker work mutation");
    do_work_ = work;
}

void BackgroundWorker::set_worker_reports_progress(bool reports) {
    require_ui("BackgroundWorker option mutation");
    reports_progress_ = reports;
}

void BackgroundWorker::set_worker_supports_cancellation(bool supports) {
    require_ui("BackgroundWorker option mutation");
    supports_cancellation_ = supports;
}

bool BackgroundWorker::is_busy() const noexcept {
    const bool result = run_ != nullptr && !(*run_).abandoned.load(std::memory_order_acquire);
    return result;
}

bool BackgroundWorker::cancellation_pending() const noexcept {
    const bool result = run_ != nullptr && cancellation_requested_;
    return result;
}

// The worker thread's entry: runs the work, then posts the completion.
void BackgroundWorker::run_entry(const CancellationFlag& cancellation, void* context) {
    Run& run = *static_cast<Run*>(context);
    BackgroundWorkerTask task(run, cancellation);
    try {
        run.work(task);
    } catch (...) {
        run.error = std::current_exception();
        run.result = BindingValue{};
    }
    detail::BackgroundWorkerDelivery completion{};
    completion.run = run.self.lock();
    if (!detail::post_from_worker(run, std::move(completion), true)) {
        run.abandoned.store(true, std::memory_order_release);
    }
}

void BackgroundWorker::run_worker_async(BindingValue argument) {
    require_ui("BackgroundWorker start");
    if (is_busy()) throw std::logic_error("GUI.Forms BackgroundWorker is already running");
    {
        std::scoped_lock lock((*dispatcher_).mutex);
        if (!(*dispatcher_).accepting) {
            throw std::logic_error("GUI.Forms BackgroundWorker cannot start after its dispatcher shut down");
        }
    }
    // An abandoned run's work has returned or is about to; collect it first.
    stop_run();
    std::shared_ptr<Run> run = std::make_shared<Run>();
    (*run).self = run;
    (*run).target = target_;
    (*run).dispatcher = dispatcher_;
    (*run).work = do_work_;
    (*run).argument = std::move(argument);
    (*run).reports_progress = reports_progress_;
    cancellation_requested_ = false;
    run_ = std::move(run);
    worker_ = std::make_unique<Worker>(&BackgroundWorker::run_entry, static_cast<void*>(run_.get()));
}

void BackgroundWorker::cancel_async() {
    require_ui("BackgroundWorker cancellation");
    if (!supports_cancellation_) {
        throw std::logic_error("GUI.Forms BackgroundWorker does not support cancellation");
    }
    if (!is_busy() || !worker_) return;
    cancellation_requested_ = true;
    (*worker_).request_cancel();
}

void BackgroundWorker::deliver_progress(const std::shared_ptr<Run>& run, const BackgroundWorkerProgress& progress) {
    // Reports from a run that already completed, or from an earlier run, are
    // dropped.
    if (run != run_) return;
    progress_changed_.emit(progress);
}

void BackgroundWorker::deliver_completion(const std::shared_ptr<Run>& run) {
    if (run != run_) return;
    // The work posted this completion as its last act: joining waits only
    // for its thread to return.
    if (worker_) (*worker_).join();
    worker_.reset();
    BackgroundWorkerCompleted completed{};
    completed.result = (*run).result;
    completed.error = (*run).error;
    completed.error_message = detail::exception_message((*run).error);
    completed.cancelled = (*run).cancelled;
    run_.reset();
    cancellation_requested_ = false;
    // Not busy any more, so a handler may start the next run.
    run_worker_completed_.emit(completed);
}

void BackgroundWorker::stop_run() noexcept {
    // Mark the run before the join, so a worker waiting for room to post its
    // completion gives up instead of waiting on this blocked thread.
    if (run_) (*run_).stopping.store(true, std::memory_order_release);
    if (worker_) {
        (*worker_).request_cancel();
        try {
            (*worker_).join();
        } catch (...) {
        }
        worker_.reset();
    }
    run_.reset();
    cancellation_requested_ = false;
}

void BackgroundWorker::verify_dispose_thread() {
    if (Window* owner = bound_window()) {
        (*owner).verify_access("BackgroundWorker disposal");
    }
}

void BackgroundWorker::on_dispose() noexcept {
    // No event follows disposal: callbacks already posted find no owner.
    if (target_) (*target_).owner = nullptr;
    stop_run();
    window_lifetime_.reset();
}

namespace {

// The events BackgroundWorker adds to those of Component, in declaration order.
constexpr std::array<InteractionDeclaration, 2> background_worker_interactions{{
    {.name = "progress_changed", .payload = "BackgroundWorkerProgress", .mutable_payload = false},
    {.name = "run_worker_completed", .payload = "BackgroundWorkerCompleted", .mutable_payload = false},
}};

} // namespace

void BackgroundWorker::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Component::append_interactions(interactions);
    interactions.insert(interactions.end(), background_worker_interactions.begin(),
                        background_worker_interactions.end());
}

} // namespace gui_forms
