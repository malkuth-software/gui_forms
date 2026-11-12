#pragma once

// Main-thread work through the run loop (macos_main_work.mm): the one way
// the AppKit host posts work to itself, so that it runs at any modal depth.

#include "macos_host_private.hpp"

namespace gui_forms::host::macos {

// Runs function(context) on the main thread from the run loop, in the
// common modes. The function adopts the context as dispatch_async_f's
// would. Thread-safe.
void post_main_work(void* context, void (*function)(void*));

// A window the work keeps alive until it runs, as the replaced blocks did.
struct WindowWork final {
    NSWindow* window;
};

// Posts performClose: to `window`, which the work keeps alive until then.
void post_perform_close(NSWindow* window);

} // namespace gui_forms::host::macos

using gui_forms::host::macos::post_main_work;
using gui_forms::host::macos::post_perform_close;
using gui_forms::host::macos::WindowWork;
