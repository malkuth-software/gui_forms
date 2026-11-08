// ColorDialog (stage W): the settings and their refusals, and showing the
// dialog through the headless host: the request it sends, accepted,
// cancelled and refused answers, translucent and opaque choices, destruction
// while showing, reentrancy, the wrong thread and reset.

#include "gui_forms/gui_forms.hpp"
#include "headless_host.hpp"

#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

// What the scripted host saw and what it answers next. An answer queue that
// runs out answers with a cancelled dialog.
struct DialogScript final {
    std::vector<HostDialogRequest> requests{};
    std::deque<HostColorDialogResult> answers{};
    HostServiceError error{HostServiceError::none};
};

struct ScriptedDialogs final {
    DialogScript* script{};

    HostDialogResult operator()(const HostDialogRequest& request, host::HeadlessHostServices&) const {
        (*script).requests.push_back(request);
        HostDialogResult result;
        result.request_id = request.request_id;
        if ((*script).error != HostServiceError::none) {
            result.status.error = (*script).error;
            result.payload = HostColorDialogResult{};
            return result;
        }
        if ((*script).answers.empty()) {
            result.payload = HostColorDialogResult{HostDialogOutcome::cancelled, 0U};
            return result;
        }
        result.payload = (*script).answers.front();
        (*script).answers.pop_front();
        return result;
    }
};

HostColorDialogResult accepted(std::uint32_t rgba) {
    HostColorDialogResult result{HostDialogOutcome::accepted, rgba};
    return result;
}

const HostColorDialogRequest* color_request(const HostDialogRequest& request) {
    const HostColorDialogRequest* const result = std::get_if<HostColorDialogRequest>(&request.payload);
    return result;
}

void settings() {
    ColorDialog dialog;
    require(dialog.title.read().empty() && dialog.color.read() == Color::rgba(0U, 0U, 0U, 255U) && !dialog.allow_alpha.read() &&
                dialog.status().accepted(),
            "a new dialog has the defaults: opaque black, no alpha");
    dialog.title = "Accent";
    dialog.color = Color::rgba(0x27U, 0x74U, 0xB8U, 255U);
    dialog.allow_alpha = true;
    bool refused = false;
    try {
        dialog.title = std::string("a\0b", 3U);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && dialog.title.read() == "Accent", "a title holding NUL is refused and the old one kept");
    dialog.reset();
    require(dialog.title.read().empty() && dialog.color.read() == Color::rgba(0U, 0U, 0U, 255U) && !dialog.allow_alpha.read(),
            "reset restores the defaults");
}

void without_host() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("color.unhosted"));
    Window window(root, {320.0, 200.0});
    ColorDialog dialog;
    dialog.color = Color::rgba(1U, 2U, 3U, 255U);
    require(dialog.show_dialog(window) == DialogResult::none &&
                dialog.status().error == HostServiceError::unsupported &&
                dialog.color.read() == Color::rgba(1U, 2U, 3U, 255U),
            "without a host the dialog is not shown and the color stays");
}

void shown_through_host() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("color.root"));
    Window window(root, {320.0, 200.0});
    host::HeadlessHost headless(window);
    host::HeadlessHostServices& services = static_cast<host::HeadlessHostServices&>(headless.services());
    DialogScript script;
    services.set_dialog_handler(ScriptedDialogs{&script});

    ColorDialog dialog;
    dialog.title = "Choose an accent color";
    dialog.color = Color::rgba(0x27U, 0x74U, 0xB8U, 255U);
    script.answers.push_back(accepted(0x315F8980U));
    require(dialog.show_dialog(window) == DialogResult::ok && dialog.status().accepted(),
            "an accepted dialog returns ok");
    const HostColorDialogRequest* const first = color_request(script.requests[0]);
    require(first != nullptr && script.requests[0].request_id != 0U && script.requests[0].owner_id == "color.root" &&
                (*first).title == "Choose an accent color" && (*first).initial_rgba == 0x2774B8FFU &&
                !(*first).allow_alpha,
            "the request carries the title, the starting color as RRGGBBAA and no alpha");
    require(dialog.color.read() == Color::rgba(0x31U, 0x5FU, 0x89U, 255U),
            "without alpha the chosen color is taken as opaque");

    dialog.allow_alpha = true;
    script.answers.push_back(accepted(0x315F8980U));
    require(dialog.show_dialog(window) == DialogResult::ok && dialog.color.read() == Color::rgba(0x31U, 0x5FU, 0x89U, 0x80U),
            "with alpha the chosen color keeps its alpha");
    const HostColorDialogRequest* const second = color_request(script.requests[1]);
    require(second != nullptr && (*second).allow_alpha && (*second).initial_rgba == 0x315F89FFU &&
                script.requests[1].request_id != script.requests[0].request_id,
            "the next request starts at the chosen color, with a new identity");

    require(dialog.show_dialog(window) == DialogResult::cancel &&
                dialog.color.read() == Color::rgba(0x31U, 0x5FU, 0x89U, 0x80U),
            "a cancelled dialog keeps the color");
    script.error = HostServiceError::backend_failure;
    require(dialog.show_dialog(window) == DialogResult::none &&
                dialog.status().error == HostServiceError::backend_failure,
            "a host failure returns none with the host's answer");
    script.error = HostServiceError::none;
    require(services.snapshot().modal_depth == 0U, "no dialog is left on the modal stack");
}

// A host whose modal loop destroys, disposes or reshows the dialog it is
// showing, as a timer or another callback inside a real host's loop could.
struct EndingDialogs final {
    std::shared_ptr<ColorDialog>* owner{};
    Window* window{};
    bool* reentry_refused{};
    int mode{};

    HostDialogResult operator()(const HostDialogRequest& request, host::HeadlessHostServices&) const {
        if (mode == 0) {
            (*owner).reset();
        } else if (mode == 1) {
            (*(*owner)).dispose();
        } else {
            try {
                static_cast<void>((*(*owner)).show_dialog(*window));
            } catch (const std::logic_error&) {
                *reentry_refused = true;
            }
        }
        HostDialogResult result;
        result.request_id = request.request_id;
        result.payload = HostColorDialogResult{HostDialogOutcome::accepted, 0x112233FFU};
        return result;
    }
};

struct ShowFromOtherThread final {
    ColorDialog* dialog{};
    Window* window{};
    bool* refused{};
    void operator()() const {
        try {
            static_cast<void>((*dialog).show_dialog(*window));
        } catch (const std::logic_error&) {
            *refused = true;
        }
    }
};

void ended_while_showing() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("color.ended"));
    Window window(root, {320.0, 200.0});
    host::HeadlessHost headless(window);
    host::HeadlessHostServices& services = static_cast<host::HeadlessHostServices&>(headless.services());
    std::shared_ptr<ColorDialog> dialog = std::make_shared<ColorDialog>();
    bool reentry_refused = false;

    services.set_dialog_handler(EndingDialogs{&dialog, &window, &reentry_refused, 2});
    require((*dialog).show_dialog(window) == DialogResult::ok && reentry_refused &&
                (*dialog).color.read() == Color::rgba(0x11U, 0x22U, 0x33U, 255U),
            "showing a dialog that is already showing is refused");

    (*dialog).color = Color::rgba(9U, 9U, 9U, 255U);
    services.set_dialog_handler(EndingDialogs{&dialog, &window, &reentry_refused, 1});
    require((*dialog).show_dialog(window) == DialogResult::cancel && (*dialog).color.read() == Color::rgba(9U, 9U, 9U, 255U),
            "a dialog disposed while showing returns cancel and takes nothing");

    dialog = std::make_shared<ColorDialog>();
    ColorDialog* const address = dialog.get();
    services.set_dialog_handler(EndingDialogs{&dialog, &window, &reentry_refused, 0});
    require((*address).show_dialog(window) == DialogResult::cancel && !dialog,
            "a dialog destroyed while showing returns cancel without touching itself");

    ColorDialog threaded;
    bool wrong_thread = false;
    std::thread other(ShowFromOtherThread{&threaded, &window, &wrong_thread});
    other.join();
    require(wrong_thread, "showing off the UI thread is refused");
    require(services.snapshot().modal_depth == 0U, "no dialog is left on the modal stack");
}

} // namespace

int main() {
    settings();
    without_host();
    shown_through_host();
    ended_while_showing();
    std::cout << "ColorDialog: settings, refusals, host requests and answers, alpha, destruction, reentrancy "
                 "and threads passed\n";
    return 0;
}
