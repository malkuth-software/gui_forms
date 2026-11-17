// The Windows host's clocks (cp28 workstream F):
// - the live-surface idle handshake (audit E4): a drain that finds nothing
//   new rests the live clock, no frame is scheduled while it rests, and a
//   producer's publication posts the wake that restarts it;
// - near scheduled wakes on the high-resolution scheduler clock instead of
//   WM_TIMER (audit N63), far ones on WM_TIMER;
// - inline input method composition (audit N61);
// - a dialog path that cannot be converted is a failure, not an empty path
//   (audit N65);
// - the common item dialog's check and create flags follow the request
//   (docs/user/hosts-and-capabilities.md, "File dialog settings"), on the
//   computed options and on real dialog objects (never shown).
// The window is a hidden popup driven message by message; nothing takes the
// foreground.
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/window.hpp"
#include "../src/surface/windows/application/windows_host.cpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace gui_forms::host {
namespace {

constexpr Size scene_size{160.0, 100.0};

struct LiveIdleHostFixture final {
    static void require(bool condition, const std::string& message) {
        if (!condition) throw std::runtime_error(message);
    }

    static void publish(LiveSurface& surface) {
        LiveSurfaceWriteLease write = surface.try_acquire_write();
        require(static_cast<bool>(write), "the producer acquires a buffer");
        require(write.publish() != 0, "the publication is accepted");
    }

    // Dispatches the posted idle wakes for `window`; the count dispatched.
    static unsigned dispatch_idle_wakes(HWND window) {
        unsigned count = 0U;
        MSG message{};
        while (PeekMessageW(&message, window, live_surface_wake_message, live_surface_wake_message, PM_REMOVE) !=
               FALSE) {
            DispatchMessageW(&message);
            count = count + 1U;
        }
        return count;
    }

    static void exercise(const ATOM native_class) {
        const std::shared_ptr<Panel> root = make_control<Panel>(StableId("live-idle.root"));
        const std::shared_ptr<TextBox> field = make_control<TextBox>(StableId("live-idle.field"));
        (*field).set_requested_bounds({10.0, 10.0, 100.0, 24.0});
        (*root).add_child(field);
        std::unique_ptr<Window> owned = std::make_unique<Window>(root, scene_size);
        Window& model = *owned;
        WindowsHostOptions options{};
        options.quit_thread_on_close = false;
        options.print_metrics_on_close = false;
        options.scale_override = 1.0;
        WindowsHostState state(std::move(owned), options);
        HWND window = CreateWindowExW(WS_EX_NOACTIVATE,
                                      reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(native_class)),
                                      L"Live idle fixture", WS_POPUP, 0, 0, static_cast<int>(scene_size.width),
                                      static_cast<int>(scene_size.height), nullptr, nullptr,
                                      GetModuleHandleW(nullptr), &state);
        require(window != nullptr, "fixture native creation");
        require(state.initialize(window), "fixture host initialization");
        InvalidateRect(state.hwnd_, nullptr, FALSE);
        SendMessageW(state.hwnd_, WM_PAINT, 0, 0);

        // E4: the clock runs while frames arrive and rests when none do.
        const std::shared_ptr<LiveSurface> surface = LiveSurface::create({.width = 16, .height = 16});
        require(model.queue_live_surface_presentation(root, surface), "the surface registers");
        state.collect_damage();
        require(state.live_clock_armed_ && !state.live_idle_, "a registered surface arms the live clock");
        publish(*surface);
        state.present_live_surface_updates();
        require(!state.live_idle_, "a drain with a new frame keeps the clock running");
        state.present_live_surface_updates();
        require(state.live_idle_ && !state.live_clock_armed_ && state.live_idle_entries_ == 1U,
                "a drain with nothing new rests the clock");
        state.collect_damage();
        require(!state.live_clock_armed_, "an idle window schedules no live frame");
        require(dispatch_idle_wakes(state.hwnd_) == 0U, "nothing wakes an idle window without a publication");
        publish(*surface);
        require(dispatch_idle_wakes(state.hwnd_) == 1U, "a publication posts one idle wake");
        require(!state.live_idle_ && state.live_clock_armed_, "the wake restarts the live clock");
        state.present_live_surface_updates();
        state.present_live_surface_updates();
        require(state.live_idle_ && state.live_idle_entries_ == 2U, "the clock rests again");
        publish(*surface);
        publish(*surface);
        require(dispatch_idle_wakes(state.hwnd_) == 1U, "two publications while idle post one wake");

        // N63: a far wake (the default 530 ms caret blink) rides WM_TIMER, a
        // near one (a 20 ms blink) the scheduler clock.
        require(model.request_focus(field), "the field takes focus");
        state.collect_damage();
        require(!state.scheduler_clock_armed_, "a far wake keeps WM_TIMER");
        PresentationSettings fast = model.presentation_settings();
        fast.caret = CaretSettings{20U, SettingOrigin::host};
        model.set_presentation_settings(fast);
        require(model.request_focus({}) && model.request_focus(field), "the field is focused again");
        state.collect_damage();
        require(state.scheduler_clock() != nullptr && state.scheduler_clock_armed_,
                "a near wake rides the scheduler clock");
        require(WaitForSingleObject(state.scheduler_clock(), 2000U) == WAIT_OBJECT_0,
                "the scheduler clock fires");
        state.scheduler_clock_signalled();
        require(state.scheduler_clock_signals_ == 1U && state.scheduler_clock_armed_,
                "the signalled wake polls the model and arms the next blink");

        // N61: inline composition. Updates replace the marked text, the
        // result commits once, and an end without a result cancels.
        state.ime_strings(false, std::wstring_view(), true, std::wstring_view(L"k"));
        state.ime_strings(false, std::wstring_view(), true, std::wstring_view(L"ka"));
        require((*field).text.read() == "ka" && state.ime_composing_, "composition updates replace each other");
        const wchar_t ka[] = {static_cast<wchar_t>(0x304B), L'\0'};
        state.ime_strings(true, std::wstring_view(ka), true, std::wstring_view());
        require((*field).text.read() == "\xE3\x81\x8B" && !state.ime_composing_,
                "the result replaces the composition once");
        state.ime_strings(false, std::wstring_view(), true, std::wstring_view(L"z"));
        require((*field).text.read() == "\xE3\x81\x8Bz", "a new composition shows its text");
        SendMessageW(state.hwnd_, WM_IME_ENDCOMPOSITION, 0, 0);
        require((*field).text.read() == "\xE3\x81\x8B" && !state.ime_composing_,
                "an end without a result removes the composition");
        require(windows_capabilities().supports(HostCapability::text_composition), "text_composition is advertised");

        // N64: the window's display period is read without fault (0 on the
        // primary display).
        require(state.window_monitor_period() >= 0, "the display period is a count of ticks");

        DestroyWindow(window);
    }

    static void dialog_paths() {
        const wchar_t lone_surrogate[] = {L'C', L':', L'\\', static_cast<wchar_t>(0xD800), L'a', L'\0'};
        require(!dialog_path_utf8(std::wstring_view(lone_surrogate)).has_value(),
                "an unconvertible path is refused");
        require(!dialog_path_utf8(std::wstring_view()).has_value(), "an empty path is refused");
        const std::optional<std::string> plain = dialog_path_utf8(std::wstring_view(L"C:\\folder\\file.txt"));
        require(plain.has_value() && *plain == "C:\\folder\\file.txt", "a plain path converts");
    }

    // The FOS_* check flags a request controls.
    static constexpr FILEOPENDIALOGOPTIONS check_flags =
        FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_CREATEPROMPT | FOS_OVERWRITEPROMPT | FOS_ALLOWMULTISELECT;

    static bool has(const FILEOPENDIALOGOPTIONS options, const FILEOPENDIALOGOPTIONS flag) {
        return (options & flag) == flag;
    }

    // Each flag is set exactly when its setting holds, whatever the shell's
    // defaults: none, or every check flag.
    static void dialog_option_values() {
        const FILEOPENDIALOGOPTIONS defaults[] = {0U, check_flags};
        for (const FILEOPENDIALOGOPTIONS shell_defaults : defaults) {
            HostOpenFileDialogRequest open{};
            FILEOPENDIALOGOPTIONS options = open_dialog_options(shell_defaults, open);
            require(has(options, FOS_FILEMUSTEXIST) && has(options, FOS_PATHMUSTEXIST),
                    "open defaults check the file and its folder");
            require(!has(options, FOS_ALLOWMULTISELECT), "open selects one file by default");
            require(has(options, FOS_FORCEFILESYSTEM) && has(options, FOS_NOCHANGEDIR),
                    "open keeps file system items and the working directory");
            open.check_file_exists = false;
            options = open_dialog_options(shell_defaults, open);
            require(!has(options, FOS_FILEMUSTEXIST) && has(options, FOS_PATHMUSTEXIST),
                    "ALLOW_MISSING_FILE clears only FOS_FILEMUSTEXIST");
            open.check_path_exists = false;
            open.allow_multiple = true;
            options = open_dialog_options(shell_defaults, open);
            require(!has(options, FOS_PATHMUSTEXIST) && has(options, FOS_ALLOWMULTISELECT),
                    "ALLOW_MISSING_FOLDER clears FOS_PATHMUSTEXIST");

            HostSaveFileDialogRequest save{};
            options = save_dialog_options(shell_defaults, save);
            require(!has(options, FOS_FILEMUSTEXIST) && has(options, FOS_PATHMUSTEXIST),
                    "save defaults check the folder only");
            require(!has(options, FOS_CREATEPROMPT) && has(options, FOS_OVERWRITEPROMPT),
                    "save defaults ask before replacing, not before creating");
            save.check_file_exists = true;
            save.create_prompt = true;
            options = save_dialog_options(shell_defaults, save);
            require(has(options, FOS_FILEMUSTEXIST) && has(options, FOS_CREATEPROMPT),
                    "REQUIRE_EXISTING_FILE and CREATE_PROMPT set their flags");
            save.check_path_exists = false;
            save.confirm_overwrite = false;
            options = save_dialog_options(shell_defaults, save);
            require(!has(options, FOS_PATHMUSTEXIST) && !has(options, FOS_OVERWRITEPROMPT),
                    "ALLOW_MISSING_FOLDER and no overwrite prompt clear their flags");
        }
    }

    // The shell's dialogs take the computed options as they are: what
    // GetOptions reads back after SetOptions carries the same check flags.
    static void dialog_options_on_shell_dialogs() {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        require(SUCCEEDED(com), "COM initializes for the dialog objects");
        HostOpenFileDialogRequest open{};
        HostSaveFileDialogRequest save{};
        const bool settings[] = {false, true};
        for (const bool setting : settings) {
            open.check_file_exists = setting;
            open.check_path_exists = !setting;
            open.allow_multiple = setting;
            ComReference<IFileOpenDialog> open_dialog;
            require(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                               IID_IFileOpenDialog,
                                               reinterpret_cast<void**>(&open_dialog.pointer))) &&
                        open_dialog.pointer != nullptr,
                    "an open dialog object is created");
            FILEOPENDIALOGOPTIONS shell_defaults = 0U;
            require(SUCCEEDED((*open_dialog.pointer).GetOptions(&shell_defaults)), "the open dialog's defaults");
            const FILEOPENDIALOGOPTIONS open_options = open_dialog_options(shell_defaults, open);
            require(SUCCEEDED((*open_dialog.pointer).SetOptions(open_options)), "the open dialog takes the options");
            FILEOPENDIALOGOPTIONS read_back = 0U;
            require(SUCCEEDED((*open_dialog.pointer).GetOptions(&read_back)) &&
                        (read_back & check_flags) == (open_options & check_flags),
                    "the open dialog keeps the request's check flags");

            save.check_file_exists = setting;
            save.check_path_exists = !setting;
            save.create_prompt = setting;
            save.confirm_overwrite = !setting;
            ComReference<IFileSaveDialog> save_dialog;
            require(SUCCEEDED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                               IID_IFileSaveDialog,
                                               reinterpret_cast<void**>(&save_dialog.pointer))) &&
                        save_dialog.pointer != nullptr,
                    "a save dialog object is created");
            require(SUCCEEDED((*save_dialog.pointer).GetOptions(&shell_defaults)), "the save dialog's defaults");
            const FILEOPENDIALOGOPTIONS save_options = save_dialog_options(shell_defaults, save);
            require(SUCCEEDED((*save_dialog.pointer).SetOptions(save_options)), "the save dialog takes the options");
            require(SUCCEEDED((*save_dialog.pointer).GetOptions(&read_back)) &&
                        (read_back & check_flags) == (save_options & check_flags),
                    "the save dialog keeps the request's check flags");
        }
        CoUninitialize();
    }

    static void run() {
        WNDCLASSEXW native_class{};
        native_class.cbSize = sizeof(native_class);
        native_class.lpfnWndProc = window_procedure;
        native_class.hInstance = GetModuleHandleW(nullptr);
        native_class.lpszClassName = L"GUIForms.LiveIdleFixture.v1";
        const ATOM registered_class = RegisterClassExW(&native_class);
        require(registered_class != 0, "fixture class registration");
        exercise(registered_class);
        require(UnregisterClassW(native_class.lpszClassName, native_class.hInstance) != FALSE,
                "fixture class release");
        dialog_paths();
        dialog_option_values();
        dialog_options_on_shell_dialogs();
    }
};

} // namespace
} // namespace gui_forms::host

int main() {
    try {
        gui_forms::host::LiveIdleHostFixture::run();
    } catch (const std::exception& error) {
        std::cerr << "windows live idle failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "windows live idle tests passed\n";
    return EXIT_SUCCESS;
}
