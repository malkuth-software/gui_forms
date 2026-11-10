// BackgroundWorker (stage W): work on a worker thread, progress and completion
// on the UI thread through the window's dispatcher, cancellation, errors,
// restarting from the completion handler, refusals, and what happens when the
// window's dispatcher shuts down or the worker is disposed mid-run.

#include "gui_forms/gui_forms.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

// Work functions and their shared data. Fields the worker thread writes and
// the test thread reads are atomic.
struct Jobs final {
    std::atomic<bool> release{false};
    std::atomic<bool> started{false};
    std::atomic<bool> refused_progress{false};
    std::atomic<bool> flooded{false};
    std::atomic<int> polls{0};

    // Adds 1 to n for the integer argument n.
    void sum(BackgroundWorkerTask& task) {
        const std::int64_t limit = binding_value_to_signed(task.argument()).value_or(0);
        std::int64_t total = 0;
        for (std::int64_t value = 1; value <= limit; ++value) total += value;
        task.set_result(BindingValue{total});
    }

    void report(BackgroundWorkerTask& task) {
        for (int percent = 0; percent <= 100; percent += 25) {
            task.report_progress(percent, BindingValue{std::string("step ") + std::to_string(percent)});
        }
        task.set_result(BindingValue{std::string("reported")});
    }

    void report_refused(BackgroundWorkerTask& task) {
        try {
            task.report_progress(50);
        } catch (const std::logic_error&) {
            refused_progress = true;
        }
    }

    // Runs until cancellation is requested, then acknowledges it.
    void until_cancelled(BackgroundWorkerTask& task) {
        started = true;
        while (!task.cancellation_pending()) {
            ++polls;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        task.set_cancelled(true);
    }

    void fail(BackgroundWorkerTask&) { throw std::runtime_error("malformed input"); }

    // Reports more progress than the dispatcher's queue holds, so the queue
    // is full when the work returns and posts its completion (N2).
    void flood(BackgroundWorkerTask& task) {
        started = true;
        for (std::size_t report = 0; report < maximum_posted_callbacks + 64U; ++report) {
            task.report_progress(50);
        }
        flooded = true;
    }

    // Waits for the test to release it, then reports and completes.
    void gated(BackgroundWorkerTask& task) {
        started = true;
        while (!release && !task.cancellation_pending()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        task.set_result(BindingValue{std::string("late")});
    }
};

struct Recorder final {
    std::vector<BackgroundWorkerProgress> progress;
    std::vector<BackgroundWorkerCompleted> completed;
    std::vector<std::thread::id> threads;
    BackgroundWorker* restart{};
    int restarts_left{};

    void on_progress(const BackgroundWorkerProgress& report) {
        progress.push_back(report);
        threads.push_back(std::this_thread::get_id());
    }
    void on_completed(const BackgroundWorkerCompleted& result) {
        completed.push_back(result);
        threads.push_back(std::this_thread::get_id());
        if (restart != nullptr && restarts_left > 0) {
            --restarts_left;
            require(!(*restart).is_busy(), "the worker is idle when its completion is raised");
            (*restart).run_worker_async(BindingValue{std::int64_t{10}});
        }
    }
};

// Drains the window's dispatcher until `done` holds or five seconds pass.
template <typename Done>
bool pump_until(Window& window, Done done) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        static_cast<void>(window.drain_posted_work());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

struct CompletedCount final {
    const Recorder* recorder{};
    std::size_t count{};
    bool operator()() const { return (*recorder).completed.size() >= count; }
};

struct Started final {
    const Jobs* jobs{};
    bool operator()() const { return (*jobs).started.load(); }
};

struct Idle final {
    const BackgroundWorker* worker{};
    bool operator()() const { return !(*worker).is_busy(); }
};

void subscribe(BackgroundWorker& worker, Recorder& recorder, std::vector<SubscriptionToken>& tokens) {
    tokens.push_back(worker.progress_changed().subscribe(
        Delegate<const BackgroundWorkerProgress&>::bind<Recorder, &Recorder::on_progress>(recorder)));
    tokens.push_back(worker.run_worker_completed().subscribe(
        Delegate<const BackgroundWorkerCompleted&>::bind<Recorder, &Recorder::on_completed>(recorder)));
}

std::shared_ptr<Panel> root(const char* id) {
    std::shared_ptr<Panel> panel = make_control<Panel>(StableId(std::string(id)));
    return panel;
}

void completion_and_restart() {
    Window window(root("bw.root"), {200.0, 100.0});
    Jobs jobs;
    Recorder recorder;
    BackgroundWorker worker(window);
    std::vector<SubscriptionToken> tokens;
    subscribe(worker, recorder, tokens);
    worker.set_do_work(BackgroundWorker::Work::bind<Jobs, &Jobs::sum>(jobs));
    worker.run_worker_async(BindingValue{std::int64_t{100}});
    require(worker.is_busy(), "a started worker is busy");
    bool refused = false;
    try {
        worker.run_worker_async();
    } catch (const std::logic_error&) {
        refused = true;
    }
    require(refused, "a busy worker refuses a second run");
    require(pump_until(window, CompletedCount{&recorder, 1U}), "the completion arrives");
    require(!worker.is_busy() && recorder.completed[0].result == BindingValue{std::int64_t{5050}} &&
                !recorder.completed[0].error && !recorder.completed[0].cancelled,
            "the completion carries the work's result");
    require(recorder.threads[0] == std::this_thread::get_id(), "the completion is raised on the UI thread");

    recorder.restart = &worker;
    recorder.restarts_left = 1;
    worker.run_worker_async(BindingValue{std::int64_t{3}});
    require(pump_until(window, CompletedCount{&recorder, 3U}), "a run started from the completion completes");
    require(recorder.completed[1].result == BindingValue{std::int64_t{6}} &&
                recorder.completed[2].result == BindingValue{std::int64_t{55}},
            "each run reports its own result, in order");

    BackgroundWorker idle(window);
    std::vector<SubscriptionToken> idle_tokens;
    Recorder idle_recorder;
    subscribe(idle, idle_recorder, idle_tokens);
    idle.run_worker_async();
    require(pump_until(window, CompletedCount{&idle_recorder, 1U}) &&
                idle_recorder.completed[0].result == BindingValue{},
            "a run without work completes with an empty result");
}

void progress_reports() {
    Window window(root("bw.progress"), {200.0, 100.0});
    Jobs jobs;
    Recorder recorder;
    BackgroundWorker worker(window);
    std::vector<SubscriptionToken> tokens;
    subscribe(worker, recorder, tokens);
    worker.set_do_work(BackgroundWorker::Work::bind<Jobs, &Jobs::report_refused>(jobs));
    worker.run_worker_async();
    require(pump_until(window, CompletedCount{&recorder, 1U}) && jobs.refused_progress.load() &&
                recorder.progress.empty(),
            "a worker that does not report progress refuses a report");

    worker.set_worker_reports_progress(true);
    worker.set_do_work(BackgroundWorker::Work::bind<Jobs, &Jobs::report>(jobs));
    worker.run_worker_async();
    require(pump_until(window, CompletedCount{&recorder, 2U}), "the reporting run completes");
    require(recorder.progress.size() == 5U && recorder.progress[0].percent == 0 &&
                recorder.progress[4].percent == 100 &&
                recorder.progress[2].user_state == BindingValue{std::string("step 50")},
            "every report arrives in order with its value");
    for (const std::thread::id thread : recorder.threads) {
        require(thread == std::this_thread::get_id(), "progress is raised on the UI thread");
    }
    require(recorder.completed[1].result == BindingValue{std::string("reported")},
            "progress arrives before the completion");
}

void cancellation_and_errors() {
    Window window(root("bw.cancel"), {200.0, 100.0});
    Jobs jobs;
    Recorder recorder;
    BackgroundWorker worker(window);
    std::vector<SubscriptionToken> tokens;
    subscribe(worker, recorder, tokens);
    worker.set_do_work(BackgroundWorker::Work::bind<Jobs, &Jobs::until_cancelled>(jobs));
    worker.run_worker_async();
    bool refused = false;
    try {
        worker.cancel_async();
    } catch (const std::logic_error&) {
        refused = true;
    }
    require(refused, "a worker that does not support cancellation refuses cancel_async");
    worker.set_worker_supports_cancellation(true);
    require(pump_until(window, Started{&jobs}), "the work starts");
    worker.cancel_async();
    require(worker.cancellation_pending(), "cancel_async sets cancellation_pending");
    require(pump_until(window, CompletedCount{&recorder, 1U}) && recorder.completed[0].cancelled &&
                !worker.cancellation_pending(),
            "an acknowledged cancellation completes as cancelled");

    worker.set_do_work(BackgroundWorker::Work::bind<Jobs, &Jobs::fail>(jobs));
    worker.run_worker_async();
    require(pump_until(window, CompletedCount{&recorder, 2U}), "a failing run completes");
    require(recorder.completed[1].error && recorder.completed[1].error_message == "malformed input" &&
                recorder.completed[1].result == BindingValue{},
            "the work's exception is reported with its message");
}

void window_shutdown_and_disposal() {
    Jobs jobs;
    Recorder recorder;
    std::vector<SubscriptionToken> tokens;
    {
        Window window(root("bw.shutdown"), {200.0, 100.0});
        BackgroundWorker worker(window);
        subscribe(worker, recorder, tokens);
        worker.set_worker_reports_progress(true);
        worker.set_do_work(BackgroundWorker::Work::bind<Jobs, &Jobs::gated>(jobs));
        worker.run_worker_async();
        require(pump_until(window, Started{&jobs}), "the gated work starts");
        // The window closes while the work runs.
        window.shutdown_dispatcher();
        jobs.release = true;
        require(pump_until(window, Idle{&worker}), "the run ends once its work returns");
        require(recorder.completed.empty(), "no completion is raised after the dispatcher shut down");
        bool refused = false;
        try {
            worker.run_worker_async();
        } catch (const std::logic_error&) {
            refused = true;
        }
        require(refused, "a worker refuses to start after its dispatcher shut down");
    }

    Jobs held;
    Recorder disposed;
    std::vector<SubscriptionToken> disposed_tokens;
    Window window(root("bw.dispose"), {200.0, 100.0});
    {
        BackgroundWorker worker(window);
        subscribe(worker, disposed, disposed_tokens);
        worker.set_do_work(BackgroundWorker::Work::bind<Jobs, &Jobs::gated>(held));
        worker.run_worker_async();
        require(pump_until(window, Started{&held}), "the held work starts");
        // Disposal requests cancellation and waits for the work to return.
        worker.dispose();
        require(!worker.is_busy(), "a disposed worker is idle");
    }
    static_cast<void>(window.drain_posted_work());
    require(disposed.completed.empty(), "no completion is raised after disposal");
}

// N2: a work function that fills the dispatcher's queue leaves its completion
// waiting for room. Disposal on the UI thread (which is not draining) must
// still return: before the fix the worker waited for room and the UI thread
// waited in the join, for ever.
void disposal_with_a_full_queue() {
    Jobs jobs;
    Recorder recorder;
    std::vector<SubscriptionToken> tokens;
    Window window(root("bw.full"), {200.0, 100.0});
    {
        BackgroundWorker worker(window);
        subscribe(worker, recorder, tokens);
        worker.set_worker_reports_progress(true);
        worker.set_do_work(BackgroundWorker::Work::bind<Jobs, &Jobs::flood>(jobs));
        worker.run_worker_async();
        // Wait, without draining, until the work has filled the queue and
        // returned; its completion is then waiting for room.
        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!jobs.flooded.load() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(jobs.flooded.load(), "the flooding work returns");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        worker.dispose();
        require(!worker.is_busy(), "a worker disposed over a full queue is idle");
    }
    static_cast<void>(window.drain_posted_work());
    require(recorder.completed.empty(), "no completion is raised after disposal over a full queue");
    require(recorder.progress.empty(), "no progress is raised after disposal over a full queue");
}

} // namespace

int main() {
    completion_and_restart();
    progress_reports();
    cancellation_and_errors();
    window_shutdown_and_disposal();
    disposal_with_a_full_queue();
    std::cout << "BackgroundWorker: completion, restart, progress, cancellation, errors, shutdown and disposal passed\n";
    return 0;
}
