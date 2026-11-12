// Main-thread work through the run loop (macos_main_work.hpp).

#include "macos_main_work.hpp"

// Main-thread work through the run loop rather than the main dispatch
// queue. The main queue is serial: while one of its functions runs, a
// nested event loop inside it (a modal window's, HostWindowing::run_modal)
// would never see the queue's later work, so posted work, damage
// collection and close requests would stall until the modal window closed.
// A perform request on the main thread in the common modes is a run loop
// source, which the modal session's run loop mode (a common mode) services
// at any depth. Thread-safe; the target and its context are released after
// the function runs.
@interface GUIFormsMainWork : NSObject
- (instancetype)initWithFunction:(void (*)(void*))function context:(void*)context;
- (void)perform;
// The bodies of this class's native entries (the AppKit overrides and
// targets below): each entry calls its answer through the fail-fast
// boundary (call_failing_fast).
- (void)answerPerform;
@end

namespace {
// The named targets the fail-fast boundary (call_failing_fast) calls for
// GUIFormsMainWork's native entries: each sends the entry's answer message.
void run_main_work_perform(GUIFormsMainWork* object) {
    [object answerPerform];
}
} // namespace

@implementation GUIFormsMainWork {
    void (*_function)(void*);
    void* _context;
}

- (instancetype)initWithFunction:(void (*)(void*))function context:(void*)context {
    self = [super init];
    if (self != nil) {
        _function = function;
        _context = context;
    }
    return self;
}

- (void)perform {
    gui_forms::detail::call_failing_fast("macos.main_work.perform", &run_main_work_perform, self);
}

- (void)answerPerform {
    void (*const function)(void*) = _function;
    void* const context = _context;
    _function = nullptr;
    _context = nullptr;
    if (function != nullptr) function(context);
}
@end

namespace gui_forms::host::macos {

void post_main_work(void* context, void (*function)(void*)) {
    GUIFormsMainWork* const work = [[GUIFormsMainWork alloc] initWithFunction:function context:context];
    NSArray<NSString*>* const modes = @[ NSRunLoopCommonModes ];
    [work performSelectorOnMainThread:@selector(perform) withObject:nil waitUntilDone:NO modes:modes];
}

} // namespace gui_forms::host::macos

namespace {
void perform_close_on_main(void* context) {
    const std::unique_ptr<WindowWork> work(static_cast<WindowWork*>(context));
    [(*work).window performClose:nil];
}
} // namespace

namespace gui_forms::host::macos {

void post_perform_close(NSWindow* window) {
    WindowWork* const work = new WindowWork{window};
    post_main_work(work, &perform_close_on_main);
}

} // namespace gui_forms::host::macos
