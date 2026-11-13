#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/component.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/scheduler.hpp"
#include "gui_forms/threading.hpp"

#include <atomic>
#include <exception>
#include <memory>
#include <string>

namespace gui_forms {

class Window;
class BackgroundWorker;
namespace detail {
struct DispatcherState;
// One run's shared state and the posted callback that delivers its progress
// and completion; defined with BackgroundWorker.
struct BackgroundWorkerRun;
struct BackgroundWorkerTarget;
struct BackgroundWorkerDelivery;
}

// The one object the work function may touch on the worker thread: the
// argument, the result, the cancellation request and progress reports. It
// lives for one run; a run's work must not keep it after returning.
class GUI_FORMS_API BackgroundWorkerTask final {
public:
    BackgroundWorkerTask(const BackgroundWorkerTask&) = delete;
    BackgroundWorkerTask& operator=(const BackgroundWorkerTask&) = delete;

    // The value run_worker_async was given.
    [[nodiscard]] const BindingValue& argument() const noexcept;
    // The value run_worker_completed reports; empty until set.
    void set_result(BindingValue result);
    // Whether cancel_async was called during this run. The work polls it.
    [[nodiscard]] bool cancellation_pending() const noexcept;
    // The work acknowledges a cancellation: run_worker_completed reports the
    // run as cancelled, as Windows Forms' DoWorkEventArgs.Cancel does.
    void set_cancelled(bool cancelled) noexcept;
    // Posts a progress report to the UI thread, where progress_changed
    // raises it. Throws std::logic_error when the worker does not report
    // progress and std::out_of_range for a percentage outside 0 to 100. A
    // report the dispatcher no longer accepts (its window closed) is dropped.
    void report_progress(int percent, BindingValue user_state = {});

private:
    friend class BackgroundWorker;
    BackgroundWorkerTask(detail::BackgroundWorkerRun& run, const CancellationFlag& cancellation) noexcept
        : run_(&run), cancellation_(&cancellation) {}

    detail::BackgroundWorkerRun* run_{};
    const CancellationFlag* cancellation_{};
};

// One progress report, delivered on the UI thread.
struct GUI_FORMS_API BackgroundWorkerProgress final {
    int percent{};
    BindingValue user_state{};
};

// The end of a run, delivered on the UI thread. `error` holds the exception
// the work threw, if any, and `error_message` its what() text; `result` is
// then empty. `cancelled` is true when the work acknowledged a cancellation.
struct GUI_FORMS_API BackgroundWorkerCompleted final {
    BindingValue result{};
    std::exception_ptr error{};
    std::string error_message{};
    bool cancelled{};
};

// Runs one work function at a time on a worker thread and reports progress
// and completion on its window's UI thread (Windows Forms' BackgroundWorker).
// Each run is a gui_forms::Worker: the work is a named target bound with
// Delegate (set_do_work), and it receives a BackgroundWorkerTask. Progress and
// completion travel back through the window's dispatcher, so they arrive in
// order with input, layout and paint, and only while the window's dispatcher
// accepts work.
//
// Execution contexts: every member function except the task's belongs to the
// UI thread of the window given at construction. The work runs on the worker
// thread and touches only its task and its own data.
class GUI_FORMS_API BackgroundWorker final : public Component {
public:
    using Work = Delegate<BackgroundWorkerTask&>;

    // Construct on the window's UI thread.
    explicit BackgroundWorker(Window& window);
    // Requests cancellation and waits for a running work function to return
    // (as dispose does). Cancellation is cooperative, so the UI thread is
    // blocked for as long as the work takes to notice it; a work function
    // must therefore never wait on the UI thread (a synchronous invoke onto
    // its window). Once stopping begins the run posts nothing more, so a
    // full dispatcher queue cannot hold the join.
    ~BackgroundWorker() override;

    // The work function, run on the worker thread. Changing it affects the
    // next run, not one in progress. A run without work completes at once
    // with an empty result.
    void set_do_work(Work work);
    [[nodiscard]] bool has_do_work() const noexcept { return static_cast<bool>(do_work_); }

    [[nodiscard]] bool worker_reports_progress() const noexcept { return reports_progress_; }
    void set_worker_reports_progress(bool reports);
    [[nodiscard]] bool worker_supports_cancellation() const noexcept { return supports_cancellation_; }
    void set_worker_supports_cancellation(bool supports);

    // Whether a run has started and its completion has not been delivered.
    // A run whose completion the dispatcher refused (its window closed) is
    // no longer busy once its work has returned.
    [[nodiscard]] bool is_busy() const noexcept;
    // Whether cancel_async was called during the current run.
    [[nodiscard]] bool cancellation_pending() const noexcept;

    // Starts a run. Throws std::logic_error while busy, after the window
    // shut down, or off the UI thread.
    void run_worker_async(BindingValue argument = {});
    // Requests cancellation of the current run; the work decides when to
    // stop. Throws std::logic_error when the worker does not support
    // cancellation. Does nothing when idle.
    void cancel_async();

    [[nodiscard]] Event<const BackgroundWorkerProgress&>& progress_changed() noexcept {
        return progress_changed_;
    }
    [[nodiscard]] Event<const BackgroundWorkerCompleted&>& run_worker_completed() noexcept {
        return run_worker_completed_;
    }

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void verify_dispose_thread() override;
    void on_dispose() noexcept override;

private:
    using Run = detail::BackgroundWorkerRun;
    friend struct detail::BackgroundWorkerDelivery;

    [[nodiscard]] Window* bound_window() const noexcept;
    void require_ui(std::string_view operation) const;
    void deliver_progress(const std::shared_ptr<Run>& run, const BackgroundWorkerProgress& progress);
    void deliver_completion(const std::shared_ptr<Run>& run);
    // Marks the run as stopping, requests cancellation of a running work
    // function and waits for it.
    void stop_run() noexcept;
    static void run_entry(const CancellationFlag& cancellation, void* context);

    std::weak_ptr<detail::WindowLifetime> window_lifetime_;
    std::shared_ptr<detail::DispatcherState> dispatcher_;
    // The UI-side target of posted callbacks; cleared at disposal so a
    // callback that arrives later finds no owner.
    std::shared_ptr<detail::BackgroundWorkerTarget> target_;
    Work do_work_{};
    bool reports_progress_{};
    bool supports_cancellation_{};
    bool cancellation_requested_{};
    // The current run and its thread. The run outlives the thread: the
    // worker is declared after it, so it is joined first.
    std::shared_ptr<Run> run_{};
    std::unique_ptr<Worker> worker_{};
    Event<const BackgroundWorkerProgress&> progress_changed_;
    Event<const BackgroundWorkerCompleted&> run_worker_completed_;
};

} // namespace gui_forms
