A component that raises `tick` at a fixed interval on a window's UI thread.

A timer drives periodic UI work, such as refreshing a clock, polling a model or
stepping a simple animation, without a thread of its own. It belongs to one
`Window`, and its ticks run on that window's UI thread between input, layout
and painting, so a tick handler may change controls directly. Use a `Worker`
for computation that would block the UI, and an `AnimationTimer` for a control's
animation, which should stop while the control is not shown.

Parameters
----------
window
    The window whose UI thread runs the ticks. Construct the timer on that
    thread; from another thread the constructor throws `std::logic_error`.
interval
    Time between ticks, 100 ms by default. Less than 1 ms throws
    `std::invalid_argument`.

Properties
----------
interval
    Time between ticks. Setting it while the timer runs restarts the
    countdown, so the next tick comes one new interval from now. Less than
    1 ms throws `std::invalid_argument`.
enabled : default ``false``
    Whether a tick is scheduled. It becomes true with `start` or `start_at`,
    and false with `stop`, with disposal, when the window shuts down and when
    a tick handler throws. There is no setter: call `start` or `stop`.

Events
------
tick
    Raised on the UI thread each time the interval elapses. It carries no
    arguments. When several timers are due at once, they tick in the order in
    which they were scheduled.

Methods
-------
start
    Schedules the first tick one interval from now. On a running timer it
    does nothing; in particular it does not restart the countdown.
start_at
    `start_at(FrameTime)` schedules the first tick at an exact `FrameClock`
    time, replacing any current schedule, even on a running timer. Later
    ticks fall at whole intervals after that deadline. Use it to align
    timers, and in tests that drive `Window::poll_frame_schedule` directly.
stop
    Cancels the next tick. A tick handler may stop, or stop and restart, its
    own timer.

See Also
--------
Worker : Runs computation on a background thread.
AnimationTimeline : Timeline sampling for animations driven by frames.

Notes
-----
Ticks are due on a fixed grid: the first deadline, then one interval after
it, and so on. When the UI thread is late by several intervals, the timer
ticks once and moves on to the next deadline of the grid that lies in the
future; it never fires a burst of ticks to catch up. The skipped ticks are
counted in `FramePollResult::coalesced_requests`.

A stopped timer schedules no wake, so a window whose timers are stopped stays
idle. Ticks continue while the window is occluded; occlusion suppresses only
rendering work.

Ticks are delivered when the window's frame schedule is polled with
`Window::poll_frame_schedule`. A native host does this from its run loop;
without a host, call it yourself, as the first example does.

An exception thrown by a tick handler is caught by the window's scheduler,
which cancels the timer: `enabled` becomes false until `start` is called
again.

`start`, `start_at`, `stop` and setting `interval` must be called on the
window's UI thread; from another thread they throw `std::logic_error` and
change nothing. After disposal, or after the window has shut down, they also
throw `std::logic_error`. Disposing or destroying the timer cancels its
pending tick at once; a timer may safely outlive its window.

Examples
--------
Drive a timer without a host. The UI thread polls 35 ms after a 10 ms timer
was started, so three deadlines have passed; the timer ticks once and keeps
its cadence:

.. code-block:: cpp

   struct Clock final : gf::Component {
       int ticks = 0;
       void on_tick() { ++ticks; }
   };

   void example() {
       std::shared_ptr<gf::Control> root =
           gf::make_control<gf::Control>(gf::StableId("clock"));
       gf::Window window(root, gf::Size{120.0, 40.0});
       gf::Timer timer(window, std::chrono::milliseconds(10));
       Clock clock;
       gf::on(timer.tick(), clock, &Clock::on_tick);

       const gf::FrameTime start{};
       timer.start_at(start + std::chrono::milliseconds(10));
       gf::FramePollResult late =
           window.poll_frame_schedule(start + std::chrono::milliseconds(35));
       // clock.ticks is 1, and late.next_wake is start + 40 ms
       timer.stop();
   }

A component that owns its timer and updates a label once a second while the
host runs:

.. code-block:: cpp

   struct StatusClock final : gf::Component {
       std::shared_ptr<gf::Label> label;
       gf::Timer timer;
       int seconds = 0;

       StatusClock(gf::Window& window, std::shared_ptr<gf::Label> target)
           : label(std::move(target)), timer(window, std::chrono::seconds(1)) {
           gf::on(timer.tick(), *this, &StatusClock::on_tick);
           timer.start();
       }
       void on_tick() {
           ++seconds;
           (*label).set_text(std::to_string(seconds) + " s");
       }
   };

   void example() {
       std::shared_ptr<gf::Label> status =
           gf::make_control<gf::Label>(gf::StableId("status.clock"));
       gf::Window window(status, gf::Size{120.0, 24.0});
       StatusClock clock(window, status);
   }
