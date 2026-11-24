#include "automation_run_internal.hpp"

#include "../../core/host/session/session_directory.hpp"

#include "gui_forms/allocation/library_memory/library_memory.hpp"
#include "gui_forms/automation/marks/automation_marks.hpp"
#include "gui_forms/components/business_card/business_card.hpp"
#include "gui_forms/window.hpp"

#include <stxlib/clocks.hpp>
#include <stxlib/environment.hpp>
#include <stxlib/process.hpp>
#include <stxlib/result.hpp>
#include <stxlib/text.hpp>
#include <stxlib/text_view.hpp>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <utility>
#include <stxlib/threads.hpp>

namespace gui_forms::automation {

CallStop::~CallStop() = default;

namespace {

// The longest a driver sleeps before looking at its stop again.
constexpr std::chrono::milliseconds stop_slice{10};

#if defined(GUI_FORMS_MCP_BUILT) && !defined(GUI_FORMS_HIGH_INTEGRITY)
// The variable `name`'s value, or empty (unset, unreadable or not UTF-8).
std::string environment_value(const char* name) {
    const std::string_view text(name);
    const stxlib::OperationResult<stxlib::TextView> checked = stxlib::validate_utf8(text.data(), text.size());
    if (checked.failed()) return std::string{};
    const stxlib::OperationResult<stxlib::Utf8String> value =
        stxlib::read_variable(*checked.value(), stxlib::maximum_environment_bytes);
    if (value.failed()) return std::string{};
    const stxlib::TextView read = (*value.value()).view();
    std::string made(read.data(), static_cast<std::size_t>(read.count()));
    return made;
}
#endif

// Letters and digits of `text`, lowercased.
std::string identity_part(std::string_view text) {
    std::string part{};
    for (const char character : text) {
        if (character >= 'A' && character <= 'Z') {
            part += static_cast<char>(character - 'A' + 'a');
        } else if ((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9')) {
            part += character;
        }
    }
    return part;
}

// ---- the steps a driver runs on the UI thread -----------------------------------

struct BeginWork final {
    AutomationCore* core{};
    std::string_view tool{};
    std::string* arguments{};
    CallStart* start{};
    void operator()() const {
        *start = (*core).begin(tool, std::move(*arguments), FrameClock::now());
    }
};

struct StepWork final {
    AutomationCore* core{};
    CallId id{};
    StepOutcome* outcome{};
    void operator()() const { *outcome = (*core).step(id, FrameClock::now()); }
};

struct CancelWork final {
    AutomationCore* core{};
    CallId id{};
    void operator()() const noexcept { (*core).cancel(id); }
};

struct SyncWork final {
    AutomationRun* run{};
    void operator()() const { (*run).sync_tools(); }
};

struct ClientWork final {
    AutomationRun* run{};
    ClientIdentity client{};
    void operator()() const { (*run).core().set_client(client); }
};

struct CloseAllWork final {
    AutomationRun* run{};
    void operator()() const noexcept { (*run).close_all(); }
};

} // namespace

AutomationLaunch read_automation_launch() {
    AutomationLaunch launch{};
#if defined(GUI_FORMS_MCP_BUILT) && !defined(GUI_FORMS_HIGH_INTEGRITY)
    const stxlib::OperationResult<stxlib::ProcessArguments> read = stxlib::process_arguments();
    if (read.succeeded()) {
        const stxlib::ProcessArguments& host = *read.value();
        for (stxlib::usize index = 1; index < host.count(); ++index) {
            const stxlib::View<char> bytes = host.argument(index).bytes();
            const std::string_view argument(bytes.data(), static_cast<std::size_t>(bytes.count()));
            if (argument == "--mcp") launch.mcp = true;
            if (argument == "--model-directed") launch.model_directed = true;
            if (argument == "--mcp-script" && index + 1 < host.count()) {
                const stxlib::View<char> path = host.argument(index + 1).bytes();
                launch.script_path.assign(path.data(), static_cast<std::size_t>(path.count()));
                ++index;
            }
        }
    }
    if (environment_value("GUI_FORMS_MCP") == "1") launch.mcp = true;
#endif
    return launch;
}

std::string automation_identity() {
    const std::string_view own(mcp_identity_override());
    if (!own.empty()) return std::string(own);
    const ProgramIdentity& program = program_identity();
    const std::string vendor = identity_part(program.vendor);
    const std::string product = identity_part(program.product);
    if (vendor.empty() || product.empty()) return std::string("app.guiforms.program");
    std::string identity = "app." + vendor + "." + product;
    if (identity.size() > 128U) identity.resize(128U);
    return identity;
}

// ---- the run ----------------------------------------------------------------------

AutomationRun::AutomationRun(AutomationLaunch launch)
    : launch_(std::move(launch)), control_(ControlSettings{launch_.model_directed}, &presenter_),
      core_(control_) {
    presenter_.bind_control(&control_);
}

AutomationRun::~AutomationRun() {
    ended_.store(true);
    gui_forms::detail::install_window_ready_hook(nullptr, nullptr);
    // The script and the transport first: their calls end (the windows are
    // gone, so their steps answer window_gone) and their tasks are joined
    // before the core they drive is destroyed.
    script_.reset();
    if (transport_) (*transport_).stop();
    transport_.reset();
    control_.disconnect();
    presenter_.bind_control(nullptr);
}

void AutomationRun::start() {
    gui_forms::detail::install_window_ready_hook(&AutomationRun::window_ready, this);
    if (launch_.mcp) {
        std::string error{};
        transport_ = McpTransport::start(*this, automation_identity(), error);
        if (!transport_) std::fprintf(stderr, "GUI.Forms: --mcp is not available: %s\n", error.c_str());
    }
    if (!launch_.script_path.empty()) {
        std::string error{};
        script_ = ScriptRunner::start(*this, launch_.script_path, error);
        if (!script_) std::fprintf(stderr, "GUI.Forms: --mcp-script: %s\n", error.c_str());
    }
    sync_tools();
}

void AutomationRun::window_ready(void* context, SurfaceRoot& window, std::string_view stable_id,
                                 const ApplicationWindowHandle& handle) noexcept {
    AutomationRun& run = *static_cast<AutomationRun*>(context);
    // Only application windows reach the hook, and every one is a Window
    // (attach_application_window's callers).
    Window* const model = static_cast<Window*>(&window);
    try {
        if (!run.core_.attach_window(*model, std::string(stable_id))) return;
        run.presenter_.set_windowing(handle, stable_id);
        run.handles_.push_back(handle);
        bool closing = false;
        {
            const stxlib::ScopedLock lock(run.mutex_);
            run.dispatchers_.push_back((*model).dispatcher());
            closing = run.closing_;
        }
        run.sync_tools();
        // The input closed before this window joined, so no close reached
        // it: it closes at the UI thread's next turn, once it is running.
        if (closing) static_cast<void>((*model).dispatcher().begin_invoke(CloseAllWork{&run}));
    } catch (const std::exception& failure) {
        // %.*s takes an int; a longer id is cut at INT_MAX bytes.
        const std::size_t shown = std::min(stable_id.size(), static_cast<std::size_t>(INT_MAX));
        std::fprintf(stderr, "GUI.Forms: the automation could not join window %.*s: %s\n",
                     static_cast<int>(shown), stable_id.data(), failure.what());
    }
}

WindowDispatcher AutomationRun::home_dispatcher() const {
    const stxlib::ScopedLock lock(mutex_);
    for (const WindowDispatcher& dispatcher : dispatchers_) {
        if (dispatcher.window_alive()) return dispatcher;
    }
    return WindowDispatcher{};
}

namespace {

// A frame-clock interval as STXlib's Duration, for its sleep.
stxlib::Duration stxlib_interval(const FrameInterval interval) noexcept {
    const stxlib::Duration converted = stxlib::Duration::nanoseconds(
        static_cast<stxlib::i64>(std::chrono::duration_cast<std::chrono::nanoseconds>(interval).count()));
    return converted;
}

} // namespace

bool AutomationRun::wait_for_window(const CallStop* stop) const {
    while (!ended_.load()) {
        if (stop != nullptr && (*stop).stopped()) return false;
        {
            const stxlib::ScopedLock lock(mutex_);
            if (!dispatchers_.empty()) return true;
        }
        stxlib::sleep_for(stxlib_interval(stop_slice));
    }
    return false;
}

ToolReply AutomationRun::drive(std::string_view tool, std::string arguments_json, const CallStop* stop) {
    const ToolReply gone = ToolReply::failure(ToolError::window_gone, "no window is open",
                                              "the program has no window, or is closing");
    WindowDispatcher dispatcher = home_dispatcher();
    if (!dispatcher.window_alive()) return gone;
    CallStart start{};
    try {
        if (dispatcher.invoke(BeginWork{&core_, tool, &arguments_json, &start}) != DispatchInvokeResult::completed) {
            return gone;
        }
    } catch (const std::exception& failure) {
        return ToolReply::failure(ToolError::internal, failure.what());
    }
    StepOutcome outcome = std::move(start.outcome);
    while (!outcome.done()) {
        const FrameTime resume = *outcome.resume_time();
        // Sleep until the step's time, looking at the stop every slice.
        while (FrameClock::now() < resume) {
            if (stop != nullptr && (*stop).stopped()) break;
            const FrameInterval left = resume - FrameClock::now();
            stxlib::sleep_for(stxlib_interval(std::min<FrameInterval>(left, stop_slice)));
        }
        if (stop != nullptr && (*stop).stopped()) {
            dispatcher = home_dispatcher();
            if (dispatcher.window_alive()) {
                try {
                    static_cast<void>(dispatcher.invoke(CancelWork{&core_, start.id}));
                } catch (...) {
                    // The window closed while cancelling: the core ends the
                    // call with the run.
                }
            }
            return ToolReply::failure(ToolError::cancelled, "the client cancelled the call");
        }
        dispatcher = home_dispatcher();
        if (!dispatcher.window_alive()) return gone;
        try {
            if (dispatcher.invoke(StepWork{&core_, start.id, &outcome}) != DispatchInvokeResult::completed) {
                return gone;
            }
        } catch (const std::exception& failure) {
            return ToolReply::failure(ToolError::internal, failure.what());
        }
    }
    try {
        static_cast<void>(dispatcher.invoke(SyncWork{this}));
    } catch (...) {
        // The tool list is bound again with the next call.
    }
    ToolReply reply = *outcome.reply();
    return reply;
}

void AutomationRun::post_client(const ClientIdentity& client) {
    const WindowDispatcher dispatcher = home_dispatcher();
    if (!dispatcher.window_alive()) return;
    static_cast<void>(dispatcher.begin_invoke(ClientWork{this, client}));
}

void AutomationRun::post_close_all() {
    {
        // Before the dispatcher is looked up: a window that joins between the
        // two sees the flag and closes itself (window_ready).
        const stxlib::ScopedLock lock(mutex_);
        closing_ = true;
    }
    const WindowDispatcher dispatcher = home_dispatcher();
    if (!dispatcher.window_alive()) return;
    static_cast<void>(dispatcher.begin_invoke(CloseAllWork{this}));
}

void AutomationRun::close_all() noexcept {
    control_.disconnect();
    for (const ApplicationWindowHandle& handle : handles_) {
        if (!handle.active()) continue;
        try {
            static_cast<void>(handle.request_close());
        } catch (...) {
            // A window that cannot be asked closes with the run.
        }
    }
}

void AutomationRun::sync_tools() {
    if (!transport_) return;
    const std::uint64_t generation = core_.tool_generation();
    if (generation == tool_generation_) return;
    tool_generation_ = generation;
    std::vector<ToolSpec> specs{};
    core_.fill_tool_specs(specs);
    (*transport_).bind_tools(specs);
}

// ---- the scope ---------------------------------------------------------------------

namespace {

// The CI smokes' startup check (tools/check_mcp_launch.py
// --require-library-allocator): with GUI_FORMS_REQUIRE_LIBRARY_ALLOCATOR=1,
// a program whose operator new is not STXlib's allocator
// (gui_forms_use_library_allocator, gui_forms::detail::program_allocator)
// says so and exits with status 70 before its window opens.
void require_library_allocator_if_asked() {
#if defined(GUI_FORMS_MCP_BUILT) && !defined(GUI_FORMS_HIGH_INTEGRITY)
    if (environment_value("GUI_FORMS_REQUIRE_LIBRARY_ALLOCATOR") != "1") return;
    if (gui_forms::detail::program_allocator() == gui_forms::detail::ProgramAllocator::replaced) return;
    std::fputs("GUI.Forms: GUI_FORMS_REQUIRE_LIBRARY_ALLOCATOR=1, but this program's operator new is not STXlib's "
               "allocator (gui_forms_use_library_allocator)\n",
               stderr);
    std::fflush(stderr);
    std::_Exit(70);
#endif
}

} // namespace

AutomationRunScope::AutomationRunScope() {
    require_library_allocator_if_asked();
    const AutomationLaunch launch = read_automation_launch();
    if (!launch.requested()) return;
    try {
        run_ = std::make_unique<AutomationRun>(launch);
        (*run_).start();
    } catch (const std::exception& failure) {
        run_.reset();
        std::fprintf(stderr, "GUI.Forms: the automation did not start: %s\n", failure.what());
    }
}

AutomationRunScope::~AutomationRunScope() = default;

} // namespace gui_forms::automation
