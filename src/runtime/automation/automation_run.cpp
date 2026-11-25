#include "automation_run_internal.hpp"

#include "../../core/application/application_state.hpp"
#include "../../core/host/session/session_directory.hpp"

#include "gui_forms/allocation/library_memory/program_allocator.hpp"
#include "gui_forms/automation/marks/automation_marks.hpp"
#include "../../controls/automation/marks/automation_marks_internal.hpp"
#include "gui_forms/components/business_card/business_card.hpp"
#include "gui_forms/dispatcher/operation/dispatch_operation.hpp"
#include "gui_forms/window.hpp"

#include <stxlib/clocks.hpp>
#include <stxlib/desktop.hpp>
#include <stxlib/environment.hpp>
#include <stxlib/path.hpp>
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

// Serving starts at the UI thread's turn after a window joined
// (AutomationRun::window_ready).
struct ServeWork final {
    AutomationRun* run{};
    void operator()() const {
        (*run).serve_when_ready();
        (*run).sync_tools();
    }
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

// The console takes new lines from the record (MCP 1.0.5).
struct ConsoleWork final {
    StockPresenter* presenter{};
    void operator()() const noexcept {
        try {
            (*presenter).console_changed();
        } catch (...) {
            // The console could not take the lines (no memory): the next
            // record tries again.
        }
    }
};

// An attach connection's question to the user.
struct AskConnectWork final {
    StockPresenter* presenter{};
    ClientIdentity client{};
    std::shared_ptr<ConnectAnswer> answer{};
    void operator()() const noexcept {
        try {
            (*presenter).ask_connect(client, answer);
        } catch (...) {
            // The question could not be shown: refused.
            (*answer).state.store(ConnectAnswerState::refused);
        }
    }
};

// The question went unanswered too long, or the connection went away.
struct WithdrawConnectWork final {
    StockPresenter* presenter{};
    void operator()() const noexcept { (*presenter).withdraw_connect(); }
};

struct BeginAttachWork final {
    AutomationRun* run{};
    ClientIdentity client{};
    void operator()() const {
        (*run).join_pending();
        (*run).core().begin_session(FrameClock::now());
        (*run).core().set_client(client);
    }
};

struct EndAttachWork final {
    AutomationRun* run{};
    void operator()() const noexcept { (*run).core().end_session(); }
};

struct FillSpecsWork final {
    AutomationRun* run{};
    std::vector<ToolSpec>* specs{};
    std::uint64_t* generation{};
    void operator()() const {
        (*run).core().fill_tool_specs(*specs);
        *generation = (*run).core().tool_generation();
    }
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
    if (argument == "--mcp-socket") launch.print_socket = true;
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

namespace {

// Counts a call being driven for as long as it lives.
struct DrivingCall final {
    explicit DrivingCall(std::atomic<int>* counted) noexcept : count(counted) { ++(*count); }
    std::atomic<int>* count{};
    DrivingCall(const DrivingCall&) = delete;
    DrivingCall& operator=(const DrivingCall&) = delete;
    ~DrivingCall() { --(*count); }
};

// A frame-clock interval as STXlib's Duration, for its sleep.
stxlib::Duration stxlib_interval(const FrameInterval interval) noexcept {
    const stxlib::Duration converted = stxlib::Duration::nanoseconds(
        static_cast<stxlib::i64>(std::chrono::duration_cast<std::chrono::nanoseconds>(interval).count()));
    return converted;
}

} // namespace

AutomationRun::AutomationRun(AutomationLaunch launch)
    : launch_(std::move(launch)), control_(control_settings(launch_), &presenter_), core_(control_) {
    presenter_.bind_control(&control_);
    presenter_.set_unsigned(launch_.appearance_review);
    presenter_.set_console_log(&console_log_);
    // A launch that asked for MCP joins its windows as they become ready;
    // one that did not waits for its first attach connection.
    joins_windows_ = launch_.requested();
}

AutomationRun::~AutomationRun() {
    ended_.store(true);
    gui_forms::detail::install_window_ready_hook(nullptr, nullptr);
    // A call whose window closed under it answers at its next step slice;
    // up to a second for it, then a moment for the transport to write it.
    const FrameTime deadline = FrameClock::now() + std::chrono::seconds(1);
    bool waited = false;
    while (driving_.load() > 0 && FrameClock::now() < deadline) {
        waited = true;
        stxlib::sleep_for(stxlib_interval(stop_slice));
    }
    if (waited) stxlib::sleep_for(stxlib_interval(std::chrono::milliseconds(200)));
    // The attach server, the script and the transport first: their calls
    // end (the windows are gone, so their steps answer window_gone) and
    // their tasks are joined before the core they drive is destroyed.
    attach_.reset();
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
    if (refusal == nullptr && attach_wanted(launch_)) {
        std::string error{};
        attach_ = AttachServer::start(*this, error);
        if (!attach_) std::fprintf(stderr, "GUI.Forms: attach is not available: %s\n", error.c_str());
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
        if (!run.joins_windows_) {
            // No client yet (a program launched without a flag): the window
            // waits for the first attach connection, and only its
            // dispatcher is kept, for posting work to the UI thread.
            if (run.presenter_.presenter_window(*model)) return;
            // The presenter's windows (the connection question) are owned
            // by the first program window.
            if (!passive_popup) run.presenter_.set_windowing(handle, stable_id);
            PendingJoin pending{};
            pending.window = (*model).window_ref();
            pending.stable_id = std::string(stable_id);
            pending.passive_popup = passive_popup;
            pending.handle = handle;
            run.pending_.push_back(std::move(pending));
            const stxlib::ScopedLock lock(run.mutex_);
            run.dispatchers_.push_back((*model).dispatcher());
            return;
        }
        run.join_window(*model, stable_id, passive_popup, handle);
    } catch (const std::exception& failure) {
        // %.*s takes an int; a longer id is cut at INT_MAX bytes.
        const std::size_t shown = std::min(stable_id.size(), static_cast<std::size_t>(INT_MAX));
        std::fprintf(stderr, "GUI.Forms: the automation could not join window %.*s: %s\n",
                     static_cast<int>(shown), stable_id.data(), failure.what());
    }
}

void AutomationRun::join_window(Window& window, std::string_view stable_id, bool passive_popup,
                                const ApplicationWindowHandle& handle) {
    AutomationRun& run = *this;
    Window* const model = &window;
    {
        if (!run.core_.attach_window(*model, std::string(stable_id), passive_popup, handle)) return;
        run.presenter_.set_windowing(handle, stable_id);
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
        // Serving starts at the UI thread's next turn after the first window
        // joined, not here: the hook runs before the window's ready callback,
        // which may register the program's tools, commands and note. Until
        // serving starts nothing reads the client's input, so initialize's
        // answer and the first tools/list carry the built-in set and the
        // first window's tools together (1.0.3 served from launch and bound
        // tools one by one while serving, so a tools/list could see part of
        // the built-in set: macos-arm64, gui_forms_mcp_launch_tests). Each
        // window posts until serving starts, so a window that closes before
        // its turn leaves the next one to start it. A dispatcher that takes
        // no work starts it now.
        if (!run.serving_started()) {
            DispatchOperation posted = (*model).dispatcher().begin_invoke(ServeWork{&run});
            if (!posted.pending()) run.serve_when_ready();
        }
        run.sync_tools();
        // The input closed before this window joined, so no close reached
        // it: it closes at the UI thread's next turn, once it is running.
        if (closing) static_cast<void>((*model).dispatcher().begin_invoke(CloseAllWork{&run}));
    }
}

void AutomationRun::join_pending() {
    if (joins_windows_) return;
    joins_windows_ = true;
    // The dispatchers were kept as the windows became ready; join_window
    // keeps them again, so the list starts over.
    {
        const stxlib::ScopedLock lock(mutex_);
        dispatchers_.clear();
    }
    std::vector<PendingJoin> pending = std::move(pending_);
    pending_.clear();
    for (const PendingJoin& entry : pending) {
        Window* const window = entry.window.get();
        if (window == nullptr) continue;
        try {
            join_window(*window, entry.stable_id, entry.passive_popup, entry.handle);
        } catch (const std::exception& failure) {
            std::fprintf(stderr, "GUI.Forms: the automation could not join window %s: %s\n",
                         entry.stable_id.c_str(), failure.what());
        }
    }
}

WindowDispatcher AutomationRun::home_dispatcher() const {
    const stxlib::ScopedLock lock(mutex_);
    for (const WindowDispatcher& dispatcher : dispatchers_) {
        if (dispatcher.window_alive()) return dispatcher;
    }
    return WindowDispatcher{};
}


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

ToolReply AutomationRun::drive(std::string_view tool, std::string arguments_json, const CallStop* stop,
                               std::string_view typed) {
    const DrivingCall driving(&driving_);
    if (typed.empty()) {
        console_log_.record_call(tool, arguments_json);
    } else {
        console_log_.record_typed(typed);
    }
    post_console();
    ToolReply reply = drive_call(tool, std::move(arguments_json), stop);
    console_log_.record_reply(reply);
    post_console();
    return reply;
}

void AutomationRun::post_console() {
    const WindowDispatcher dispatcher = home_dispatcher();
    if (!dispatcher.window_alive()) return;
    try {
        static_cast<void>(dispatcher.begin_invoke(ConsoleWork{&presenter_}));
    } catch (...) {
        // The window stopped taking work: the console goes with it.
    }
}

void AutomationRun::console_note(std::string_view note) {
    console_log_.record_note(note);
    post_console();
}

ToolReply AutomationRun::drive_call(std::string_view tool, std::string arguments_json, const CallStop* stop) {
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
        const ToolReply closed = ToolReply::success(
            tool == "disconnect" ? "ok: disconnected; every window closed and the program is ending"
                                 : "ok: the window closed during the call; the program may be ending");
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
    // The transport ended with no disconnect call (the client crashed or was
    // killed): a hard disconnect with the program's default, so its save
    // question goes to the user (owner, round 3).
    core_.hard_disconnect();
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

bool AutomationRun::serving_started() const noexcept {
    const bool started = !transport_ || (*transport_).serving();
    return started;
}

void AutomationRun::sync_tools() {
    const bool stdio = transport_ && (*transport_).serving();
    if (attach_ && published_generation_ != core_.tool_generation()) {
        published_generation_ = core_.tool_generation();
        std::vector<ToolSpec> counted{};
        core_.fill_tool_specs(counted);
        (*attach_).publish(counted.size(), detail::app_note_copy());
    }
    const stxlib::ScopedLock lock(mutex_);
    const bool attached = attached_transport_ != nullptr;
    if (!stdio && !attached) return;
    const std::uint64_t generation = core_.tool_generation();
    if (generation == tool_generation_) return;
    tool_generation_ = generation;
    std::vector<ToolSpec> specs{};
    core_.fill_tool_specs(specs);
    if (stdio) (*transport_).bind_tools(specs);
    if (attached) (*attached_transport_).bind_tools(specs);
}

// ---- attach (MCP 1.0.5) ---------------------------------------------------------

namespace {

// The client as it named itself: self-reported, never verified.
ClientIdentity attach_identity(const AttachClient& client) {
    ClientIdentity identity{};
    identity.client_name = client.console ? std::string("console") : client.name;
    identity.client_version = client.version;
    return identity;
}

// The program's instructions for a model: what every program says, then
// its own note.
std::string program_instructions() {
    std::string instructions(base_instructions);
    const std::string note = detail::app_note_copy();
    if (!note.empty()) {
        instructions += "\n\nHow this program works (its own note, also the resource gui://app/note):\n";
        instructions += note;
    }
    return instructions;
}

// What a session is told about its token.
std::string token_note(const std::string& token) {
    if (token.empty()) return std::string{};
    std::string note = "Session token: " + token + ". A session whose connection drops can be resumed within " +
                       std::to_string(attach_resume_milliseconds / 1000) +
                       " seconds: initialize again with params.resume set to the token, or type `resume " + token +
                       "` as a console's first line.";
    return note;
}

// The executable's path; empty when the host cannot say or the name is
// not Unicode text.
std::string executable_path() {
    const stxlib::OperationResult<stxlib::Path> found = stxlib::current_executable();
    if (found.failed()) return std::string{};
    const stxlib::PathText text = (*found.value()).text();
    if (!text.is_unicode()) return std::string{};
    const stxlib::TextView view = text.unicode();
    std::string path(view.data(), static_cast<std::size_t>(view.count()));
    return path;
}

} // namespace

AttachProgram AutomationRun::attach_program() {
    AttachProgram program{};
    program.identity = automation_identity();
    program.name = std::string(program_identity().product);
    if (program.name.empty()) program.name = program.identity;
    program.executable = executable_path();
    return program;
}

std::string AutomationRun::admit_connection(const AttachClient& client, const CallStop* stop) {
    if (attach_live_.load()) return "another client is connected to this program through its socket";
    if (transport_ && stdio_client_seen_.load() && !stdio_ended_.load()) {
        return "the client that launched this program is connected to it";
    }
    if (launch_.mcp || launch_.shared || launch_.appearance_review) return std::string{};
    // Launched without a flag: the user is asked, every connection.
    const WindowDispatcher dispatcher = home_dispatcher();
    if (!dispatcher.window_alive()) return "the program has no window to ask its user from";
    const std::shared_ptr<ConnectAnswer> asked = gui_forms::detail::make_library_shared<ConnectAnswer>();
    try {
        static_cast<void>(dispatcher.begin_invoke(AskConnectWork{&presenter_, attach_identity(client), asked}));
    } catch (...) {
        return "the program could not ask its user";
    }
    const FrameTime deadline = FrameClock::now() + connect_question_limit;
    while ((*asked).state.load() == ConnectAnswerState::waiting) {
        const bool gave_up = ended_.load() || (stop != nullptr && (*stop).stopped()) || FrameClock::now() >= deadline;
        if (gave_up) {
            const WindowDispatcher home = home_dispatcher();
            if (home.window_alive()) {
                try {
                    static_cast<void>(home.begin_invoke(WithdrawConnectWork{&presenter_}));
                } catch (...) {
                    // The window is closing: the question goes with it.
                }
            }
            return "the user did not answer";
        }
        stxlib::sleep_for(stxlib_interval(stop_slice));
    }
    if ((*asked).state.load() != ConnectAnswerState::allowed) return "the user did not allow the connection";
    return std::string{};
}

bool AutomationRun::begin_attached_session(const AttachClient& client, const std::string& label) {
    const WindowDispatcher dispatcher = home_dispatcher();
    if (!dispatcher.window_alive()) return false;
    try {
        if (dispatcher.invoke(BeginAttachWork{this, attach_identity(client)}) != DispatchInvokeResult::completed) {
            return false;
        }
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "GUI.Forms: attach: the session could not begin: %s\n", failure.what());
        return false;
    }
    attach_live_.store(true);
    console_log_.set_session(label);
    const std::string named = client.console ? std::string("a console")
                              : client.name.empty() ? std::string("a client")
                                                    : client.name + " (the name is the client's own, not verified)";
    console_note("connected through the socket: " + named);
    return true;
}

bool AutomationRun::serve_attached(McpTransport& transport, const std::string& token, std::string& error) {
    std::vector<ToolSpec> specs{};
    std::uint64_t generation = 0U;
    const WindowDispatcher dispatcher = home_dispatcher();
    if (!dispatcher.window_alive()) {
        error = "the program has no window";
        return false;
    }
    if (dispatcher.invoke(FillSpecsWork{this, &specs, &generation}) != DispatchInvokeResult::completed) {
        error = "the program's windows are closing";
        return false;
    }
    std::string instructions = program_instructions();
    const std::string session = token_note(token);
    if (!session.empty()) instructions += "\n\n" + session;
    if (!transport.serve(automation_identity(), instructions, detail::app_note_copy(), specs, error)) return false;
    const stxlib::ScopedLock lock(mutex_);
    attached_transport_ = &transport;
    return true;
}

void AutomationRun::detach_transport() noexcept {
    const stxlib::ScopedLock lock(mutex_);
    attached_transport_ = nullptr;
}

void AutomationRun::end_attached_session() {
    detach_transport();
    if (!attach_live_.exchange(false)) return;
    console_note("session ended");
    console_log_.set_session(std::string{});
    const WindowDispatcher dispatcher = home_dispatcher();
    if (!dispatcher.window_alive()) return;
    try {
        static_cast<void>(dispatcher.invoke(EndAttachWork{this}));
    } catch (...) {
        // The window closed meanwhile: the session ends with the run.
    }
}

std::string AutomationRun::console_help(const std::string& token) const {
    std::string help = program_instructions();
    help += "\n\nCommands:\n"
            "  help                  this text\n"
            "  tools                 the tools, one per line\n"
            "  call <tool> [<json object> | key=value ...]\n"
            "                        calls a tool; the reply is the tool's text, as MCP gives it\n"
            "  transcript            this program's console record\n"
            "  resume <token>        as the first line: resumes a session whose connection dropped\n"
            "  quit                  ends the session\n"
            "A first line that is a JSON object makes the connection MCP instead (newline-delimited JSON-RPC).";
    const std::string session = token_note(token);
    if (!session.empty()) help += "\n" + session;
    return help;
}

std::string AutomationRun::console_tools() {
    std::vector<ToolSpec> specs{};
    std::uint64_t generation = 0U;
    const WindowDispatcher dispatcher = home_dispatcher();
    if (!dispatcher.window_alive()) return "the program has no window";
    try {
        if (dispatcher.invoke(FillSpecsWork{this, &specs, &generation}) != DispatchInvokeResult::completed) {
            return "the program's windows are closing";
        }
    } catch (const std::exception& failure) {
        return std::string("the tools could not be read: ") + failure.what();
    }
    std::string listing{};
    for (const ToolSpec& spec : specs) {
        std::string summary = spec.title;
        if (summary.empty()) summary = spec.description.substr(0U, spec.description.find('\n'));
        listing += spec.name;
        if (!summary.empty()) listing += "  " + summary;
        listing += '\n';
    }
    listing += std::to_string(specs.size()) + " tools; `call <tool>` with no arguments, or read the schemas over MCP";
    return listing;
}

bool attach_wanted(const AutomationLaunch& launch) {
#if defined(GUI_FORMS_MCP_BUILT) && !defined(GUI_FORMS_HIGH_INTEGRITY)
    if (!launch.script_path.empty()) return false;
    const bool wanted = environment_value("GUI_FORMS_MCP_ATTACH") != "0";
    return wanted;
#else
    static_cast<void>(launch);
    return false;
#endif
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

// --mcp-socket: the socket of each running instance of this program (by its
// identity), one path a line on standard output, status 0; when none runs,
// the path a launch would listen at, with "<pid>" for its process ID, and
// status 1.
[[noreturn]] void print_socket_and_exit() {
    const std::string identity = automation_identity();
    bool found = false;
    for (const AttachListing& listing : list_attach_endpoints()) {
        if (listing.identity != identity) continue;
        std::fprintf(stdout, "%s\n", listing.address.c_str());
        found = true;
    }
    if (!found) {
        std::string error{};
        std::string path = attach_socket_path(0, error);
        if (path.empty()) {
            std::fprintf(stderr, "GUI.Forms: --mcp-socket: %s\n", error.c_str());
        } else {
            const std::size_t name = path.rfind("0.sock");
            if (name != std::string::npos) path.replace(name, 1U, "<pid>");
            std::fprintf(stdout, "%s\n", path.c_str());
            std::fprintf(stderr, "GUI.Forms: no %s is running; a launch listens at this path, <pid> its process ID\n",
                         identity.c_str());
        }
    }
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(found ? 0 : 1);
}

} // namespace

AutomationRunScope::AutomationRunScope() {
    // Scopes open and close on the UI thread, outermost last.
    if (open_scopes.fetch_add(1) != 0) return;
    outermost_ = true;
    require_library_allocator_if_asked();
    const AutomationLaunch launch = read_automation_launch();
    if (launch.print_socket) print_socket_and_exit();
    // A program launched without a flag still listens for an attach
    // connection (MCP 1.0.5); its windows join nothing until one comes.
    if (!launch.requested() && !attach_wanted(launch)) return;
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
