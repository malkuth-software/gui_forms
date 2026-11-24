#include "automation_run_internal.hpp"

#include "../../core/application/application_state.hpp"
#include "../../core/host/session/session_directory.hpp"

#include "gui_forms/allocation/library_memory/library_memory.hpp"
#include "gui_forms/automation/marks/automation_marks.hpp"
#include "../../controls/automation/marks/automation_marks_internal.hpp"
#include "gui_forms/components/business_card/business_card.hpp"
#include "gui_forms/window.hpp"

#include <stxlib/clocks.hpp>
#include <stxlib/environment.hpp>
#include <stxlib/process.hpp>
#include <stxlib/result.hpp>
#include <stxlib/text.hpp>
#include <stxlib/text_view.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <utility>
#include <vector>
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

namespace {

// One launch argument read into `launch`; `next` is the argument after it
// (null for none). True when it took `next` too.
bool read_launch_argument(std::string_view argument, const std::string* next, AutomationLaunch& launch) {
    // --model-directed is --mcp's old name (model.directed folds into the
    // AI-led launch, owner's control model of 2026-11-24).
    if (argument == "--mcp" || argument == "--model-directed") launch.mcp = true;
    if (argument == "--mcp-shared") launch.shared = true;
    if (argument == "--mcp-appearance-review") launch.appearance_review = true;
    if (argument == "--mcp-script" && next != nullptr) {
        launch.script_path = *next;
        return true;
    }
    return false;
}

void read_launch_arguments(const std::vector<std::string>& arguments, AutomationLaunch& launch) {
    for (std::size_t index = 0U; index < arguments.size(); ++index) {
        const std::string* const next = index + 1U < arguments.size() ? &arguments[index + 1U] : nullptr;
        if (read_launch_argument(arguments[index], next, launch)) ++index;
    }
}

} // namespace

AutomationLaunch read_automation_launch() {
    AutomationLaunch launch{};
#if defined(GUI_FORMS_MCP_BUILT) && !defined(GUI_FORMS_HIGH_INTEGRITY)
    const stxlib::OperationResult<stxlib::ProcessArguments> read = stxlib::process_arguments();
    if (read.succeeded()) {
        const stxlib::ProcessArguments& host = *read.value();
        std::vector<std::string> arguments{};
        for (stxlib::usize index = 1; index < host.count(); ++index) {
            const stxlib::View<char> bytes = host.argument(index).bytes();
            arguments.emplace_back(bytes.data(), static_cast<std::size_t>(bytes.count()));
        }
        read_launch_arguments(arguments, launch);
    }
    // What a program took from its argv first (on macOS the process's own
    // arguments are that argv, edited in place).
    read_launch_arguments(detail::taken_mcp_arguments(), launch);
    const std::string variable = environment_value("GUI_FORMS_MCP");
    if (variable == "1") launch.mcp = true;
    if (variable == "shared") launch.shared = true;
    // Both asked: AI-led, the stronger.
    if (launch.mcp) launch.shared = false;
#endif
    return launch;
}

const char* automation_launch_refusal(const AutomationLaunch& launch, bool test_controls) noexcept {
    if (launch.appearance_review && !test_controls) {
        return "--mcp-appearance-review is not available in this build (debug and test builds only)";
    }
    return nullptr;
}

// What every program tells a model first; the program's own note follows.
constexpr std::string_view base_instructions =
    "A GUI.Forms program. Read it with windows and snapshot; act with act, pointer and keyboard; each act replies "
    "with what changed. Destructive acts ask the user once per launch. Dialogs a model opens are closed as Cancel, "
    "never submitted; one the program opened is the user's to answer.";

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

// The launch's control settings: --mcp and a script are AI-led (a script
// has no user to ask), --mcp-shared is the default control model, and an
// appearance review needs no control.
namespace {

ControlSettings control_settings(const AutomationLaunch& launch) noexcept {
    ControlSettings settings{};
    settings.ai_led = launch.mcp || (!launch.shared && !launch.script_path.empty());
    settings.appearance_review = launch.appearance_review;
    return settings;
}

} // namespace

// ---- the run ----------------------------------------------------------------------

AutomationRun::AutomationRun(AutomationLaunch launch)
    : launch_(std::move(launch)), control_(control_settings(launch_), &presenter_), core_(control_) {
    presenter_.bind_control(&control_);
    presenter_.set_unsigned(launch_.appearance_review);
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
    // AI-led: the model holds control from the start.
    control_.begin_ai_led(FrameClock::now());
    const char* const refusal = automation_launch_refusal(launch_, test_controls_built());
    if (refusal != nullptr) {
        // Refused outright: a review launch never falls back to a signed
        // one, nor a release build to an unsigned one.
        std::fprintf(stderr, "GUI.Forms: %s\n", refusal);
    } else if (launch_.mcp || launch_.shared || launch_.appearance_review) {
        std::string error{};
        transport_ = McpTransport::claim(*this, error);
        if (!transport_) std::fprintf(stderr, "GUI.Forms: MCP is not available: %s\n", error.c_str());
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
        const std::shared_ptr<gui_forms::detail::ApplicationWindowState> state =
            gui_forms::detail::ApplicationHandleAccess::state(handle);
        const bool passive_popup = state != nullptr && (*state).passive_popup;
        if (!run.core_.attach_window(*model, std::string(stable_id), passive_popup, handle)) return;
        run.presenter_.set_windowing(handle, stable_id);
        run.handles_.push_back(handle);
        bool closing = false;
        bool client_seen = false;
        ClientIdentity client{};
        {
            const stxlib::ScopedLock lock(run.mutex_);
            run.dispatchers_.push_back((*model).dispatcher());
            closing = run.closing_;
            client_seen = run.client_seen_;
            client = run.client_;
        }
        // A client seen before this window joined (set_client ignores the
        // same client posted again).
        if (client_seen) run.core_.set_client(client);
        // Serving starts with the first window, when the program has
        // registered its tools, commands and note.
        run.serve_when_ready();
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
        // A window gone after the call began closed during it: a model's
        // close of the program's last window ends here (round 3).
        const ToolReply closed = ToolReply::success("ok: the window closed during the call; the program may be "
                                                    "ending");
        dispatcher = home_dispatcher();
        if (!dispatcher.window_alive()) return closed;
        try {
            if (dispatcher.invoke(StepWork{&core_, start.id, &outcome}) != DispatchInvokeResult::completed) {
                return closed;
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
    {
        // Kept before the dispatcher is looked up: a window that joins
        // between the two takes the client as it joins (window_ready).
        const stxlib::ScopedLock lock(mutex_);
        client_seen_ = true;
        client_ = client;
    }
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

void AutomationRun::serve_when_ready() {
    if (!transport_ || (*transport_).serving()) return;
    std::vector<ToolSpec> specs{};
    core_.fill_tool_specs(specs);
    tool_generation_ = core_.tool_generation();
    std::string instructions(base_instructions);
    const std::string note = detail::app_note_copy();
    if (!note.empty()) {
        instructions += "\n\nHow this program works (its own note, also the resource gui://app/note):\n";
        instructions += note;
    }
    std::string error{};
    if (!(*transport_).serve(automation_identity(), instructions, note, specs, error)) {
        std::fprintf(stderr, "GUI.Forms: MCP is not available: %s\n", error.c_str());
    }
}

void AutomationRun::sync_tools() {
    if (!transport_ || !(*transport_).serving()) return;
    const std::uint64_t generation = core_.tool_generation();
    if (generation == tool_generation_) return;
    tool_generation_ = generation;
    std::vector<ToolSpec> specs{};
    core_.fill_tool_specs(specs);
    (*transport_).bind_tools(specs);
}

// ---- the scope ---------------------------------------------------------------------

namespace {

// The AutomationRunScope objects open now (AutomationRunScope).
constinit std::atomic<int> open_scopes{0};

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
    // Scopes open and close on the UI thread, outermost last.
    if (open_scopes.fetch_add(1) != 0) return;
    outermost_ = true;
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

AutomationRunScope::~AutomationRunScope() {
    run_.reset();
    open_scopes.fetch_sub(1);
}

bool AutomationRunScope::outermost() const noexcept {
    return outermost_;
}

} // namespace gui_forms::automation
