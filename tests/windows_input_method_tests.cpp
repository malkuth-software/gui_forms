// N61: the Windows host and the system input method.
//
// The host does not bridge preedit composition, so it must not advertise
// HostCapability::text_composition. It does place the input method's
// composition window at the focused control's caret
// (SurfaceRoot::text_input_caret) and keeps the candidate window off the
// caret line. A fixture window with one focused text box runs in a real Win32
// window through host::run_windows at scale 1.5; on its UI thread the test
// sends WM_IME_STARTCOMPOSITION and reads the forms back through IMM.

#include "gui_forms/gui_forms.hpp"
#include "gui_forms/platform/windows_host.hpp"
#include "support/windows_own_window.hpp"

#include <windows.h>
#include <imm.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;

constexpr const wchar_t* fixture_title = L"GUI.Forms input method test";
constexpr std::string_view fixture_title_utf8 = "GUI.Forms input method test";
// A scale that is not 1, so logical and device pixels differ.
constexpr double fixture_scale = 1.5;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<TextBox> text;
};

std::unique_ptr<Window> make_fixture(Fixture& fixture) {
    fixture.root = make_control<Panel>(StableId("ime.root"));
    fixture.text = make_control<TextBox>(StableId("ime.text"));
    (*fixture.text).set_requested_bounds({20.0, 30.0, 200.0, 26.0});
    (*fixture.text).text = "Input";
    (*fixture.root).add_child(fixture.text);
    std::unique_ptr<Window> window = std::make_unique<Window>(fixture.root, Size{260.0, 120.0});
    return window;
}

// The whole device pixels covering `logical` in a client area of `client`,
// as the host computes them (WindowsHostState::device_rect).
RECT expected_device_rect(Rect logical, const RECT& client) {
    const RECT device{
        std::clamp<LONG>(static_cast<LONG>(std::floor(logical.x * fixture_scale)),
                         client.left, client.right),
        std::clamp<LONG>(static_cast<LONG>(std::floor(logical.y * fixture_scale)),
                         client.top, client.bottom),
        std::clamp<LONG>(static_cast<LONG>(std::ceil((logical.x + logical.width) * fixture_scale)),
                         client.left, client.right),
        std::clamp<LONG>(static_cast<LONG>(std::ceil((logical.y + logical.height) * fixture_scale)),
                         client.top, client.bottom)};
    return device;
}

bool same_rect(const RECT& left, const RECT& right) {
    return left.left == right.left && left.top == right.top &&
           left.right == right.right && left.bottom == right.bottom;
}

struct TestState final {
    Window* window{};
    Fixture* fixture{};
    std::function<void()> request_close;
    std::vector<std::string> failures;
    int checks{};
};

// Runs on the UI thread from the message loop, after attach.
struct RunChecks final {
    TestState* state{};

    void check() const {
        Window& window = *(*state).window;
        const HWND hwnd = test_support::own_gui_forms_window(fixture_title);
        require(hwnd != nullptr, "the fixture's native window does not exist");
        require(window.request_focus((*(*state).fixture).text), "the text box takes focus");
        window.perform_layout();
        const std::optional<Rect> caret = window.text_input_caret();
        require(caret.has_value(), "a focused text box names an input caret");
        RECT client{};
        require(GetClientRect(hwnd, &client) != FALSE, "the client rectangle");
        const RECT expected = expected_device_rect(*caret, client);
        require(expected.right > expected.left && expected.bottom > expected.top,
                "the caret lies inside the client area");

        static_cast<void>(SendMessageW(hwnd, WM_IME_STARTCOMPOSITION, 0, 0));
        const HIMC context = ImmGetContext(hwnd);
        require(context != nullptr, "the window has an input context");
        COMPOSITIONFORM composition{};
        const BOOL composition_read = ImmGetCompositionWindow(context, &composition);
        CANDIDATEFORM candidate{};
        const BOOL candidate_read = ImmGetCandidateWindow(context, 0, &candidate);
        static_cast<void>(SendMessageW(hwnd, WM_IME_ENDCOMPOSITION, 0, 0));
        static_cast<void>(ImmReleaseContext(hwnd, context));

        require(composition_read != FALSE, "the composition form reads back");
        require(composition.dwStyle == CFS_POINT, "the composition window is placed at a point");
        require(composition.ptCurrentPos.x == expected.left &&
                    composition.ptCurrentPos.y == expected.top,
                "the composition window starts at the caret, in device pixels");
        ++(*state).checks;
        require(candidate_read != FALSE, "the candidate form reads back");
        require(candidate.dwStyle == CFS_EXCLUDE, "the candidate window excludes the caret");
        require(same_rect(candidate.rcArea, expected), "the excluded area is the caret box");
        require(candidate.ptCurrentPos.x == expected.left &&
                    candidate.ptCurrentPos.y == expected.bottom,
                "the candidate window opens below the caret");
        ++(*state).checks;
    }

    void operator()() const {
        try {
            check();
        } catch (const std::exception& error) {
            (*state).failures.push_back(error.what());
        }
        (*state).request_close();
    }
};

// Called on the UI thread once the window is attached: posts the checks, so
// they run from the message loop with the native window complete.
struct HostReady final {
    TestState* state{};
    void operator()(std::function<void()>, std::function<void()> request_close,
                    std::function<HostDialogResult(const HostDialogRequest&)>,
                    std::function<HostClipboardTextResult()>,
                    std::function<HostServiceStatus(std::string_view)>) const {
        (*state).request_close = std::move(request_close);
        // The posted work stays queued when the operation handle is dropped.
        const DispatchOperation posted =
            (*(*state).window).dispatcher().begin_invoke(std::function<void()>(RunChecks{state}));
        static_cast<void>(posted);
    }
};

} // namespace

int main() {
    try {
        require(!host::windows_capabilities().supports(HostCapability::text_composition),
                "the Windows host does not advertise text_composition");
        Fixture fixture;
        std::unique_ptr<Window> window = make_fixture(fixture);
        TestState state;
        state.window = window.get();
        state.fixture = &fixture;
        host::WindowsHostOptions options;
        options.title = std::string(fixture_title_utf8);
        options.initial_size = Size{260.0, 120.0};
        options.scale_override = fixture_scale;
        options.print_metrics_on_close = false;
        options.host_ready = HostReady{&state};
        const int exit_code = host::run_windows(std::move(window), std::move(options));
        require(exit_code == 0, "the fixture window did not run");
        if (!state.failures.empty()) {
            for (const std::string& failure : state.failures) {
                std::cerr << "gui_forms_windows_input_method_tests: " << failure << '\n';
            }
            return EXIT_FAILURE;
        }
        require(state.checks == 2, "every check ran");
        std::cout << "gui_forms_windows_input_method_tests: " << state.checks << " checks passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_windows_input_method_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
