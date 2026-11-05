#include "support.hpp"

namespace gallery_pages_tests {

void test_states_and_diagnostics(gui_forms::Window& window,
                                 gui_forms::host::HeadlessHost& host) {
    select_page(window, "states-and-diagnostics");
    const std::shared_ptr<gui_forms::Label> status = std::dynamic_pointer_cast<Label>(
        window.find("gallery.dispatcher.status"));
    require(status != nullptr,
            "states page must expose the public dispatcher proving status");
    for (const char* id : {"gallery.dispatcher.post", "gallery.dispatcher.cancel",
                           "gallery.dispatcher.invoke"}) {
        const std::shared_ptr<gui_forms::Button> button =
            std::dynamic_pointer_cast<Button>(window.find(id));
        require(button != nullptr && (*button).command_connected(),
                "dispatcher buttons must bind page-owned Commands");
    }

    click(window, window.find("gallery.dispatcher.post"));
    require((*status).text().find("is pending") != std::string::npos &&
                window.dispatcher_snapshot().pending == 3U,
            "dispatcher gallery click must return before its posted batch runs");
    const DispatchDrainResult first = window.drain_posted_work();
    require(first.invoked == 3U && first.remaining == 1U &&
                (*status).text().find("order ABC") != std::string::npos &&
                (*status).text().find("D remains posted") != std::string::npos,
            "dispatcher gallery turn one must retain FIFO snapshot semantics");
    const DispatchDrainResult second = window.drain_posted_work();
    require(second.invoked == 1U && second.remaining == 0U &&
                (*status).text().find("order ABCD") != std::string::npos,
            "dispatcher gallery nested work must commit on turn two");

    click(window, window.find("gallery.dispatcher.cancel"));
    require((*status).text().find("before dispatch") != std::string::npos,
            "dispatcher gallery must expose explicit pre-dispatch cancellation");
    const DispatchDrainResult cancelled = window.drain_posted_work();
    require(cancelled.invoked == 0U && cancelled.cancelled == 1U &&
                (*status).text().find("ERROR") == std::string::npos,
            "cancelled gallery work must never execute its callback");

    const std::shared_ptr<gui_forms::Button> invoke = std::dynamic_pointer_cast<Button>(
        window.find("gallery.dispatcher.invoke"));
    click(window, invoke);
    // The Command's disabled state projects to its bound button while the
    // worker waits; the posted completion step re-enables the Command.
    require(invoke && !(*invoke).enabled.type() &&
                (*status).text().find("Worker blocked") != std::string::npos,
            "dispatcher gallery must visibly expose a blocking worker Invoke");
    require_eventually(
        DispatcherHasOnePending(window),
        "gallery worker Invoke did not reach the dispatcher queue");
    const DispatchDrainResult invoked = host.pump_dispatcher();
    require(invoked.invoked == 1U && (*invoke).enabled.type() &&
                (*status).text().find("Worker released after UI callback") !=
                    std::string::npos,
            "gallery worker must resume only after its callback ran on the UI thread");
}

} // namespace gallery_pages_tests
