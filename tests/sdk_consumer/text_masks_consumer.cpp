// A consumer of GUIForms::TextMasks alone (tests/sdk_consumer/CMakeLists.txt):
// it links nothing else, so the component's interface must bring
// Application's archive, whose members must hold the mask service. The
// one-module SDK once shipped both broken (an empty TextMasks interface, and
// an Application archive without the masks: b68c80ecc, 2d4bb797b); each
// fails this program's link. It also uses the prepared-text vocabulary the
// text-mask headers are written in.

#include "gui_forms/prepared_text/layout/prepared_text_layout.hpp"
#include "gui_forms/text_mask.hpp"

#include <iostream>
#include <memory>
#include <string_view>

namespace {

int failures = 0;

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        failures = failures + 1;
    }
}

void text_masks_and_prepared_text() {
    gui_forms::TextMaskService masks{};
    std::unique_ptr<gui_forms::TextMaskSession> session{};
    const gui_forms::TextMaskResult opened = masks.open_session(nullptr, session);
    require(opened.status == gui_forms::TextMaskStatus::success && session != nullptr,
            "the installed Application archive holds the mask service");
    if (session != nullptr) {
        gui_forms::TextMaskSession& active = *session;
        active.begin_close();
        active.join_and_release();
    }
    masks.begin_close();
    const gui_forms::PreparedTextLayout layout{};
    require(layout.empty(), "a default prepared layout is empty");
    const gui_forms::PrepareInput input{};
    require(input.empty(), "a default prepare input is empty");
}

} // namespace

int main() {
    text_masks_and_prepared_text();
    if (failures != 0) {
        std::cerr << failures << " text mask consumer check(s) failed\n";
        return 1;
    }
    std::cout << "sdk_text_masks_consumer: TextMasks alone links and runs\n";
    return 0;
}
