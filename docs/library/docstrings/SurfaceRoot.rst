The root of one retained control tree and the thread that owns it.

A `SurfaceRoot` holds what every root of a control tree has, whatever
presents it: the thread its tree belongs to, the queue of work posted to that
thread, and the instrumentation counters its work reports into. A `Window` is
a surface root presented through a native host. A threaded sub-surface
(stage S2) will be one whose frames go to a `LiveSurface`. The root is bound
to the thread that constructs it and never changes thread.

The threaded sub-surfaces stage S1 moves the control-root part of `Window`
here step by step, with no change of behaviour. So far that is the thread
identity, the dispatcher and the metrics.

Properties
----------
check_access
    Whether the calling thread is the root's thread: the thread that
    constructed it.
invoke_required
    The opposite of `check_access`: whether the caller must post work with
    `begin_invoke` or `invoke` instead of touching the root directly.
metrics_snapshot
    A copy of the root's instrumentation counters: layout passes, flushes,
    mutations, damage, input, focus, disposals, wrong-thread rejections and
    more. `MetricsSnapshot::to_json` serializes it.
dispatcher_snapshot
    Counters of the posted-work queue: posted, invoked, cancelled and
    faulted callbacks, pending work and wake-ups.

Methods
-------
begin_invoke
    Queues a callback to run on the root's thread in a later dispatch turn
    and returns a `DispatchOperation`; it may be called from any thread. The
    overload that takes a control cancels the work if that control is
    detached or disposed before its turn.
invoke
    Runs a callback on the root's thread and waits for it. On that thread it
    runs inline; from another thread it blocks until the host runs it and
    rethrows its exception. It requires a running host when called from
    another thread.
drain_posted_work
    Runs one turn of posted work on the root's thread: the callbacks that
    were queued when it started, up to the given bound. Hosts call it; tests
    call it to run posted work without a host.
set_dispatch_wake_handler
    Installs the host's thread-safe wake. Installing one after work was
    queued wakes the host once at once. The handler runs under the
    dispatcher's lock on whichever thread posted, so it may only signal and
    must never call back into the root. Removing it while a synchronous
    `invoke` waits throws `std::logic_error`.
verify_access
    Throws `std::logic_error` unless the caller is on the root's thread,
    naming the operation, and counts the rejection.
metrics
    The live instrumentation counters, for hosts and renderers that record
    into them.
reset_activity_metrics
    Zeroes the activity counters, keeping the population and depth values,
    so a test can measure one workload.

See Also
--------
Window : The surface root a native host presents.
DispatchOperation : The handle returned by `begin_invoke`.
WindowDispatcher : A handle to the dispatcher that outlives the root.
MetricsSnapshot : The counters a root records.
