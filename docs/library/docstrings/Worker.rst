One function running on a background thread, with cooperative cancellation.

A worker starts its entry function on its own thread as soon as it is
constructed, passes it a `CancellationFlag` and a context pointer, and is
joined by the thread that created it. Use it for work that would block the
UI thread: decoding, searching, computing. The worker never touches controls;
hand results back to the UI thread with `Window::begin_invoke` or
`Control::begin_invoke`, or read them after `join`. For many short tasks
that should finish before the caller continues, use an `AtomicThreadPool`
instead.

Parameters
----------
entry
    A named function `void(const CancellationFlag&, void*)`. It runs once,
    on the worker thread. A null entry throws `std::invalid_argument`.
context
    Passed unchanged to `entry`. The worker borrows it until `join`
    returns or the worker is destroyed, so declare the context before the
    worker.

Methods
-------
request_cancel
    Sets the worker's `CancellationFlag`. It may be called from any thread
    while the worker exists, never throws, and does not wait.
join
    Waits for the entry function to return, without requesting cancellation.
    If the entry threw, the first `join` rethrows that exception; later calls
    return normally. Calling `join` from inside the entry function throws
    `std::logic_error`.

See Also
--------
CancellationFlag : The flag the entry function polls.
AtomicThreadPool : A synchronous pool for batches of short tasks.
Window : Its `begin_invoke` and `invoke` hand work back to the UI thread.

Notes
-----
Destroying a worker requests cancellation and then waits for the entry
function to return, so an entry that never checks its flag blocks the
destructor. An exception the entry threw and nobody collected with `join` is
discarded at destruction. A worker must never destroy itself from its own
entry function; that terminates the program.

`join` and destruction belong to the thread that created the worker; only
`request_cancel` is safe to call concurrently. Writes the entry made to its
context are visible to the creating thread once `join` has returned.

A worker cannot be copied or moved. Workers run on system pthreads on macOS
and Linux and on Win32 threads (STXlib's threads) on Windows, through the shared
`GUIForms::Threading` library.

Examples
--------
Compute a result in the background and read it after joining:

.. code-block:: cpp

   struct Sum final {
       int limit = 0;
       long long total = 0;   // read by the owner only after join
   };

   void add_up(const gf::CancellationFlag& cancel, void* context) {
       Sum& sum = *static_cast<Sum*>(context);
       for (int value = 1; value <= sum.limit && !cancel.requested(); ++value) {
           sum.total += value;
       }
   }

   void example() {
       Sum sum;
       sum.limit = 1000;
       gf::Worker worker(add_up, &sum);   // declared after its context
       worker.join();                     // sum.total == 500500
   }

An exception thrown by the entry function reaches the first `join`:

.. code-block:: cpp

   void parse_input(const gf::CancellationFlag&, void*) {
       throw std::runtime_error("malformed input");
   }

   void example() {
       gf::Worker worker(parse_input, nullptr);
       try {
           worker.join();
       } catch (const std::runtime_error&) {
           // reported once
       }
       worker.join();   // already reported: returns normally
   }
