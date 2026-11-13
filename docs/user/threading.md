# Threading

A GUI.Forms `Window` has one UI thread, and every retained object attached to
it belongs to that thread. Background work is explicit: you start a
`Worker`, it computes, and it hands immutable results back through the
window's dispatcher. Nothing in the toolkit mutates a control from another
thread, and the toolkit rejects attempts to do so instead of racing.

## The UI thread

The thread that constructs a `Window` is its UI thread. `check_access()`
returns whether the caller is on it; `verify_access()` throws when it is not.
`Window::invoke_required()` is the inverse of `check_access()`;
`Control::invoke_required()` reports whether dispatch to its owner is needed.
A property write or mutation called from the wrong thread on a control attached to a window
throws and increments `MetricsSnapshot::rejected_wrong_thread_operations`; it
never partially applies. A control that belongs to no window has no UI thread
yet and is not checked, so build a tree on one thread before you attach it.

Native hosts drive the UI thread with their own event loop (AppKit's run
loop, the Win32 message loop, the X11 event loop). The headless host used by
tests pumps it explicitly. All of them drain the same renderer-free
dispatcher.

## The dispatcher

`Window::begin_invoke(callback)` posts work to the UI thread from any thread
and returns a `DispatchOperation`. `Control::begin_invoke` does the same but
scopes the work to the control: if the control is detached or disposed before
its turn, the work is cancelled. Posting through a control that is not
attached to a window throws `std::logic_error`. Both wake the running host.

The rules are deliberately simple:

- One dispatch turn consumes one FIFO snapshot of the queue. Work posted by a
  callback waits for the next host turn; it never runs reentrantly inside the
  current one.
- The queue holds at most 4,096 pending callbacks, and one turn runs at most
  1,024. Overflow is rejected, not silently dropped.
- Wake requests coalesce while one is pending.
- A callback that throws has its exception retained on the operation as
  `std::exception_ptr`; later work still runs.
- Dropping the `DispatchOperation` does not cancel the work. `cancel()` is
  explicit, thread-safe and effective only while the operation is pending.
- Window or host shutdown synchronously rejects new work and cancels every
  pending operation.

`Window::invoke(callback)` and `Control::invoke(callback)` run inline when
called on the UI thread. From a worker they enqueue one synchronous
operation, wake the host and block the worker until it completes; the
callback's exception is rethrown on the worker. If the owner detaches or the
dispatcher shuts down first, the worker is woken with
`DispatchCancelledError`. A worker call with no running host is rejected
rather than left waiting forever.

The one deadlock the toolkit does not hide: UI-thread code that blocks on a
worker which is itself blocked in `invoke`. There is no reentrant pumping
(`DoEvents`) to paper over it.

## Timers

`Timer` binds to one live `Window` and fires `tick()` on its UI thread. The
interval is at least 1 ms; `start`, `start_at`, `stop` and interval changes
are supported. Late deadlines coalesce into one callback while keeping their
cadence and registration order. Occlusion does not stop component time, but a
disabled timer publishes no wake. `stop`, disposal and window shutdown revoke
future callbacks synchronously. A `Timer` is not a background worker.

## Workers

`gui_forms/threading.hpp` provides the cooperative worker utilities. They
live in the small shared library `GUIForms::Threading`, which `Core`,
`Application` and `Audio` all link, so there is exactly one implementation in
a process.

`CancellationFlag`
: `request()` sets a release/acquire atomic once and for all; `requested()` is
  safe to poll from the worker. A request does not interrupt I/O, unlock a
  mutex or wake a condition variable. Work checks the flag at bounded task
  boundaries and gives blocking operations their own wake or timeout.

`Worker(entry, context)`
: Runs one named function `void(const CancellationFlag&, void*)` on one
  pthread. The context is borrowed; declare it before the worker so it
  outlives cleanup. `request_cancel()` may be called from any thread while
  the worker lives; `join()` waits, publishes completion and rethrows the
  entry's exception once. Destruction requests cancellation and joins, so
  work that never cooperates blocks destruction. A worker must never destroy
  or join itself.

`AtomicThreadPool`
: A synchronous batch pool with reused task storage. Small batches run on the
  caller; larger batches claim index ranges atomically across pthread
  workers. Tasks are `noexcept`, borrow their contexts and must not reenter
  the pool.

Workers use system pthreads on macOS and Linux and MSYS2 winpthreads on
Windows. On Windows `libgui_forms_threading.dll` links winpthreads
statically, so no `libwinpthread-1.dll` is needed beside it.

## Pattern: compute off-thread, publish on-thread

```cpp
struct Job {
    gf::Window* window;
    std::shared_ptr<gf::Label> status;      // retained parent owns it; we hold a strong ref
    std::string result;
};

// Runs on the UI thread. It carries copies, so nothing is shared with the worker.
struct ShowResult final {
    std::shared_ptr<gf::Label> status;
    std::string text;
    void operator()() const { (*status).text = text; }
};

void entry(const gf::CancellationFlag& cancel, void* raw) {
    Job& job = *static_cast<Job*>(raw);
    for (int step = 0; step < 100 && !cancel.requested(); ++step) {
        job.result = compute_step(step);        // worker-side state only
    }
    static_cast<void>((*job.window).begin_invoke(ShowResult{job.status, job.result}));
}

Job job{&window, status, {}};
gf::Worker worker(entry, &job);   // destructor cancels and joins
```

The worker touches no control. It posts a callback carrying copies, and the
callback runs on the UI thread in a later turn. If the label is disposed
before then, the callback still runs but writing `text` on a disposed control
is rejected; use `Control::begin_invoke` on the label instead to have the
dispatcher drop the work automatically.

## BackgroundWorker

`BackgroundWorker` (`<gui_forms/background_worker/background_worker.hpp>`)
is Windows Forms' BackgroundWorker over a `Worker`. It runs one work
function at a time on a worker thread and delivers progress and completion
on its window's UI thread, through the window's dispatcher, in order with
input, layout and paint.

```cpp
#include <gui_forms/background_worker/background_worker.hpp>
#include <gui_forms/binding/value/binding_value.hpp>

struct Sums final {
    void work(gf::BackgroundWorkerTask& task) {
        const std::int64_t limit = gf::binding_value_to_signed(task.argument()).value_or(0);
        std::int64_t total = 0;
        for (std::int64_t value = 1; value <= limit && !task.cancellation_pending(); ++value) {
            total += value;
        }
        task.set_result(gf::BindingValue{total});
    }
};

Sums sums;
gf::BackgroundWorker worker(window);
worker.set_do_work(gf::BackgroundWorker::Work::bind<Sums, &Sums::work>(sums));
worker.run_worker_async(gf::BindingValue{std::int64_t{100}});
// run_worker_completed reports 5050 when the window's dispatcher runs.
```

- **The work** is a `Delegate` bound with `set_do_work`. It touches only
  its `BackgroundWorkerTask` (the argument, the result, cancellation and
  progress reports) and its own data, never a control.
- **Progress.** With `set_worker_reports_progress(true)`, the work calls
  `task.report_progress(percent, user_state)` and `progress_changed()`
  delivers each report on the UI thread.
- **Cancellation** is cooperative. With
  `set_worker_supports_cancellation(true)`, `cancel_async()` sets the
  task's `cancellation_pending()`, and the work decides when to stop and
  calls `task.set_cancelled(true)`.
- **Completion.** `run_worker_completed()` delivers a
  `BackgroundWorkerCompleted` with the result, or the exception the work
  threw (`error`, `error_message`), or `cancelled`.
- `run_worker_async` throws `std::logic_error` while a run is busy. The
  destructor requests cancellation and waits for the work to return, so a
  work function must never wait on the UI thread.

## Audio

`AudioClip::load_ogg(path, cancellation)` takes a worker's
`const CancellationFlag&` directly, checks it between decode blocks and never
publishes a partial clip. `examples/reference/audio_worker.cpp` shows an
adapter that links only `GUIForms::Audio`, decodes on a worker and joins
before reading the result.
