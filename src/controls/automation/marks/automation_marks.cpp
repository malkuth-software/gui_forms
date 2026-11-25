#include "gui_forms/automation/marks/automation_marks.hpp"
#include "automation_marks_internal.hpp"

#include "gui_forms/window.hpp"

#include <atomic>
#include <cstddef>
#include <cstring>
#include <stxlib/threads.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::automation {
namespace {

// The longest stable identity and user step text a mark keeps.
constexpr std::size_t maximum_mark_text_bytes = 4096U;

struct WindowMark final {
    WindowRef window{};
    const Window* address{};
    bool drivable{};
    bool destructive{};
};

struct ActionMark final {
    std::string stable_id{};
    SemanticAction action{SemanticAction::press};
};

struct UserStep final {
    std::string stable_id{};
    std::string what{};
};

struct DialogOpener final {
    std::string stable_id{};
    std::string tool{};
};

// A pending state the program declared (set_model_pending_state).
struct PendingMark final {
    std::string key{};
    std::string what{};
    std::string how_to_settle{};
};

// One table for the process. Marks are set and read on the UI thread; the
// mutex keeps a program with several UI threads safe, and nothing is called
// while it is held.
struct MarkTable final {
    stxlib::Mutex mutex{};
    std::vector<WindowMark> windows{};
    std::vector<ActionMark> destructive_actions{};
    std::vector<std::string> hidden_commands{};
    std::vector<UserStep> user_steps{};
    // Every step ever required, done or not (wait until user_step refuses
    // a step never required, round 2).
    std::vector<std::string> known_steps{};
    std::vector<DialogOpener> dialog_openers{};
    std::string identity{};
    std::string app_note{};
    std::vector<PendingMark> pending{};
    // The launch arguments take_mcp_arguments removed: on macOS it edits
    // the C runtime's argv in place, which is where the launch is read.
    std::vector<std::string> taken_arguments{};
};

MarkTable& marks() noexcept {
    // Never destroyed (termination releases nothing): a late call at exit
    // never meets a destroyed lock.
    static MarkTable* const instance = new MarkTable();
    return *instance;
}

std::atomic<bool> clipboard_readable{false};

// Text from a program's pointer: present, at most the limit, and UTF-8.
bool read_text(const char* text, std::string_view& out) noexcept {
    if (text == nullptr) return false;
    const std::size_t length = ::strnlen(text, maximum_mark_text_bytes + 1U);
    if (length == 0U || length > maximum_mark_text_bytes) return false;
    const std::string_view view(text, length);
    if (!validate_utf8(view).valid()) return false;
    out = view;
    return true;
}

// The live mark of `window`, or null. Marks of destroyed windows are
// dropped as they are found, so an address a new window reuses never
// inherits an old mark. Called with the table's lock held.
WindowMark* window_mark(MarkTable& table, const Window& window) noexcept {
    std::size_t index = 0U;
    while (index < table.windows.size()) {
        WindowMark& mark = table.windows[index];
        if (mark.window.get() == nullptr) {
            mark = table.windows.back();
            table.windows.pop_back();
            continue;
        }
        if (mark.address == &window) return &mark;
        ++index;
    }
    return nullptr;
}

WindowMark& ensure_window_mark(MarkTable& table, Window& window) {
    WindowMark* found = window_mark(table, window);
    if (found != nullptr) return *found;
    table.windows.push_back(WindowMark{window.window_ref(), &window, false, false});
    return table.windows.back();
}

bool valid_identity_character(char character) noexcept {
    const bool lower = character >= 'a' && character <= 'z';
    const bool digit = character >= '0' && character <= '9';
    const bool punctuation = character == '.' || character == '-' || character == '_';
    return lower || digit || punctuation;
}

} // namespace

bool mcp_capability_built() noexcept {
#if defined(GUI_FORMS_MCP)
    return true;
#else
    return false;
#endif
}

void set_model_drivable(Window& window, bool drivable) {
    MarkTable& table = marks();
    const stxlib::ScopedLock lock(table.mutex);
    ensure_window_mark(table, window).drivable = drivable;
}

bool model_drivable(const Window& window) noexcept {
    MarkTable& table = marks();
    const stxlib::ScopedLock lock(table.mutex);
    const WindowMark* const mark = window_mark(table, window);
    const bool drivable = mark != nullptr && (*mark).drivable;
    return drivable;
}

void set_window_destructive(Window& window, bool destructive) {
    MarkTable& table = marks();
    const stxlib::ScopedLock lock(table.mutex);
    ensure_window_mark(table, window).destructive = destructive;
}

bool window_destructive(const Window& window) noexcept {
    MarkTable& table = marks();
    const stxlib::ScopedLock lock(table.mutex);
    const WindowMark* const mark = window_mark(table, window);
    const bool destructive = mark != nullptr && (*mark).destructive;
    return destructive;
}

bool set_action_destructive(const char* stable_id, SemanticAction action, bool destructive) {
    std::string_view identity{};
    if (!read_text(stable_id, identity)) return false;
    MarkTable& table = marks();
    const stxlib::ScopedLock lock(table.mutex);
    for (std::size_t index = 0U; index < table.destructive_actions.size(); ++index) {
        const ActionMark& mark = table.destructive_actions[index];
        if (mark.stable_id != identity || mark.action != action) continue;
        if (!destructive) {
            table.destructive_actions.erase(table.destructive_actions.begin() +
                                            static_cast<std::ptrdiff_t>(index));
        }
        return true;
    }
    if (destructive) table.destructive_actions.push_back(ActionMark{std::string(identity), action});
    return true;
}

bool action_destructive(const char* stable_id, SemanticAction action) noexcept {
    std::string_view identity{};
    if (!read_text(stable_id, identity)) return false;
    const bool destructive = detail::action_destructive_mark(identity, action);
    return destructive;
}

bool set_command_hidden_from_model(const char* stable_id, bool hidden) {
    std::string_view identity{};
    if (!read_text(stable_id, identity)) return false;
    MarkTable& table = marks();
    const stxlib::ScopedLock lock(table.mutex);
    for (std::size_t index = 0U; index < table.hidden_commands.size(); ++index) {
        if (table.hidden_commands[index] != identity) continue;
        if (!hidden) table.hidden_commands.erase(table.hidden_commands.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }
    if (hidden) table.hidden_commands.emplace_back(identity);
    return true;
}

bool command_hidden_from_model(const char* stable_id) noexcept {
    std::string_view identity{};
    if (!read_text(stable_id, identity)) return false;
    const bool hidden = detail::command_hidden_mark(identity);
    return hidden;
}

bool require_user_step(const char* stable_id, const char* what_user_must_do) {
    std::string_view identity{};
    std::string_view what{};
    if (!read_text(stable_id, identity) || !read_text(what_user_must_do, what)) return false;
    MarkTable& table = marks();
    const stxlib::ScopedLock lock(table.mutex);
    for (UserStep& step : table.user_steps) {
        if (step.stable_id != identity) continue;
        step.what = std::string(what);
        return true;
    }
    table.user_steps.push_back(UserStep{std::string(identity), std::string(what)});
    bool known = false;
    for (const std::string& step : table.known_steps) known = known || step == identity;
    if (!known) table.known_steps.push_back(std::string(identity));
    return true;
}

void complete_user_step(const char* stable_id) {
    std::string_view identity{};
    if (!read_text(stable_id, identity)) return;
    MarkTable& table = marks();
    const stxlib::ScopedLock lock(table.mutex);
    for (std::size_t index = 0U; index < table.user_steps.size(); ++index) {
        if (table.user_steps[index].stable_id != identity) continue;
        table.user_steps.erase(table.user_steps.begin() + static_cast<std::ptrdiff_t>(index));
        return;
    }
}

bool user_step_pending(const char* stable_id) noexcept {
    std::string_view identity{};
    if (!read_text(stable_id, identity)) return false;
    MarkTable& table = marks();
    const stxlib::ScopedLock lock(table.mutex);
    for (const UserStep& step : table.user_steps) {
        if (step.stable_id == identity) return true;
    }
    return false;
}

bool set_opens_dialog(const char* stable_id, bool opens, const char* dialog_free_tool) {
    std::string_view identity{};
    if (!read_text(stable_id, identity)) return false;
    std::string_view tool{};
    if (dialog_free_tool != nullptr && *dialog_free_tool != '\0' && !read_text(dialog_free_tool, tool)) return false;
    MarkTable& table = marks();
    const stxlib::ScopedLock lock(table.mutex);
    for (std::size_t index = 0U; index < table.dialog_openers.size(); ++index) {
        if (table.dialog_openers[index].stable_id != identity) continue;
        if (opens) {
            table.dialog_openers[index].tool = std::string(tool);
        } else {
            table.dialog_openers.erase(table.dialog_openers.begin() + static_cast<std::ptrdiff_t>(index));
        }
        return true;
    }
    if (opens) table.dialog_openers.push_back(DialogOpener{std::string(identity), std::string(tool)});
    return true;
}

bool opens_dialog(const char* stable_id) noexcept {
    std::string_view identity{};
    if (!read_text(stable_id, identity)) return false;
    MarkTable& table = marks();
    const stxlib::ScopedLock lock(table.mutex);
    for (const DialogOpener& opener : table.dialog_openers) {
        if (opener.stable_id == identity) return true;
    }
    return false;
}

void set_clipboard_readable_by_model(bool readable) noexcept {
    clipboard_readable.store(readable, std::memory_order_release);
}

bool clipboard_readable_by_model() noexcept {
    const bool readable = clipboard_readable.load(std::memory_order_acquire);
    return readable;
}

bool set_mcp_identity(const char* identity) {
    if (identity == nullptr) return false;
    const std::size_t length = ::strnlen(identity, 129U);
    if (length < 3U || length > 128U) return false;
    const std::string_view text(identity, length);
    if (text[0] < 'a' || text[0] > 'z') return false;
    bool dotted = false;
    for (const char character : text) {
        if (!valid_identity_character(character)) return false;
        if (character == '.') dotted = true;
    }
    if (!dotted) return false;
    MarkTable& table = marks();
    const stxlib::ScopedLock lock(table.mutex);
    table.identity = std::string(text);
    return true;
}

const char* mcp_identity_override() noexcept {
    MarkTable& table = marks();
    const stxlib::ScopedLock lock(table.mutex);
    const char* const identity = table.identity.c_str();
    return identity;
}

bool set_model_app_note(const char* utf8_note) {
    std::string_view text{};
    if (utf8_note != nullptr && utf8_note[0] != '\0') {
        const std::size_t length = ::strnlen(utf8_note, 4097U);
        if (length > 4096U) return false;
        text = std::string_view(utf8_note, length);
        if (!validate_utf8(text).valid()) return false;
    }
    MarkTable& table = marks();
    const stxlib::ScopedLock lock(table.mutex);
    table.app_note = std::string(text);
    return true;
}

const char* model_app_note() noexcept {
    MarkTable& table = marks();
    const stxlib::ScopedLock lock(table.mutex);
    const char* const note = table.app_note.c_str();
    return note;
}

namespace {

// `text` as a pending state's part: present, 1 to `limit` bytes, UTF-8.
bool read_pending_part(const char* text, std::size_t limit, std::string& out) {
    if (text == nullptr) return false;
    const std::size_t length = ::strnlen(text, limit + 1U);
    if (length == 0U || length > limit) return false;
    const std::string_view view(text, length);
    if (!validate_utf8(view).valid()) return false;
    out = std::string(view);
    return true;
}

} // namespace

bool set_model_pending_state(const char* key, const char* what, const char* how_to_settle) {
    PendingMark mark{};
    if (!read_pending_part(key, 64U, mark.key)) return false;
    if (!read_pending_part(what, 512U, mark.what)) return false;
    if (!read_pending_part(how_to_settle, 512U, mark.how_to_settle)) return false;
    MarkTable& table = marks();
    const stxlib::ScopedLock lock(table.mutex);
    for (PendingMark& existing : table.pending) {
        if (existing.key != mark.key) continue;
        existing = std::move(mark);
        return true;
    }
    table.pending.push_back(std::move(mark));
    return true;
}

void clear_model_pending_state(const char* key) {
    if (key == nullptr) return;
    const std::string_view text(key, ::strnlen(key, 65U));
    MarkTable& table = marks();
    const stxlib::ScopedLock lock(table.mutex);
    for (std::size_t index = 0U; index < table.pending.size(); ++index) {
        if (table.pending[index].key != text) continue;
        table.pending.erase(table.pending.begin() + static_cast<std::ptrdiff_t>(index));
        return;
    }
}

bool model_pending_state_set() noexcept {
    MarkTable& table = marks();
    const stxlib::ScopedLock lock(table.mutex);
    const bool set = !table.pending.empty();
    return set;
}

namespace {

// Keeps one taken argument for the launch reader; a failure to keep it
// loses only that argument (the program's argv is edited either way).
void keep_taken_argument(std::string_view argument) noexcept {
    try {
        MarkTable& table = marks();
        const stxlib::ScopedLock lock(table.mutex);
        table.taken_arguments.emplace_back(argument);
    } catch (...) {
    }
}

} // namespace

void take_mcp_arguments(int& argc, char** argv) noexcept {
    if (argv == nullptr || argc <= 1) return;
    int write = 1;
    int read = 1;
    while (read < argc) {
        const char* const argument = argv[read];
        const std::string_view text = argument == nullptr ? std::string_view{} : std::string_view(argument);
        if (text == "--mcp" || text == "--mcp-shared" || text == "--model-directed" ||
            text == "--mcp-appearance-review" || text == "--mcp-socket") {
            keep_taken_argument(text);
            ++read;
            continue;
        }
        if (text == "--mcp-script") {
            // The option and its file; a trailing option without a file is
            // dropped alone.
            keep_taken_argument(text);
            if (read + 1 < argc && argv[read + 1] != nullptr) keep_taken_argument(argv[read + 1]);
            read += read + 1 < argc ? 2 : 1;
            continue;
        }
        argv[write] = argv[read];
        ++write;
        ++read;
    }
    argc = write;
    argv[write] = nullptr;
}

namespace detail {

std::vector<std::string> taken_mcp_arguments() {
    MarkTable& table = marks();
    const stxlib::ScopedLock lock(table.mutex);
    std::vector<std::string> taken = table.taken_arguments;
    return taken;
}

bool user_step_known(std::string_view stable_id) {
    MarkTable& table = marks();
    const stxlib::ScopedLock lock(table.mutex);
    for (const std::string& step : table.known_steps) {
        if (step == stable_id) return true;
    }
    return false;
}

void append_pending_states(std::string& text) {
    std::vector<PendingMark> pending{};
    {
        MarkTable& table = marks();
        const stxlib::ScopedLock lock(table.mutex);
        pending = table.pending;
    }
    for (const PendingMark& mark : pending) {
        text += "\npending: " + mark.what + " (" + mark.how_to_settle + ")";
    }
}

std::string app_note_copy() {
    MarkTable& table = marks();
    const stxlib::ScopedLock lock(table.mutex);
    std::string note = table.app_note;
    return note;
}


bool opens_dialog_mark(std::string_view stable_id, std::string& tool) {
    MarkTable& table = marks();
    const stxlib::ScopedLock lock(table.mutex);
    for (const DialogOpener& opener : table.dialog_openers) {
        if (opener.stable_id != stable_id) continue;
        tool = opener.tool;
        return true;
    }
    return false;
}

bool pending_user_step(std::string_view stable_id, std::string& what) {
    MarkTable& table = marks();
    const stxlib::ScopedLock lock(table.mutex);
    for (const UserStep& step : table.user_steps) {
        if (step.stable_id != stable_id) continue;
        what = step.what;
        return true;
    }
    return false;
}

bool action_destructive_mark(std::string_view stable_id, SemanticAction action) {
    MarkTable& table = marks();
    const stxlib::ScopedLock lock(table.mutex);
    for (const ActionMark& mark : table.destructive_actions) {
        if (mark.stable_id == stable_id && mark.action == action) return true;
    }
    return false;
}

bool command_hidden_mark(std::string_view stable_id) {
    MarkTable& table = marks();
    const stxlib::ScopedLock lock(table.mutex);
    for (const std::string& hidden : table.hidden_commands) {
        if (hidden == stable_id) return true;
    }
    return false;
}

} // namespace detail
} // namespace gui_forms::automation
