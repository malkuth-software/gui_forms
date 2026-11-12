#include <gui_forms/audio/audio.hpp>
#include <gui_forms/controls/panel.hpp>
#include <gui_forms/text_mask.hpp>
#include <gui_forms/window.hpp>

#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
void require(const bool condition, const char* const message) {
    if (!condition) throw std::runtime_error(message);
}

void application_text_audio() {
    const std::shared_ptr<gui_forms::Panel> root =
        gui_forms::make_control<gui_forms::Panel>(gui_forms::StableId("source-consumer"));
    gui_forms::Window window(root, {320.0, 200.0});
    window.perform_layout();

    gui_forms::TextMaskService masks{};
    std::unique_ptr<gui_forms::TextMaskSession> session{};
    const gui_forms::TextMaskResult opened = masks.open_session(nullptr, session);
    require(opened.status == gui_forms::TextMaskStatus::success, "Application exports the mask service");
    gui_forms::TextMaskSession& active_session = *session;
    active_session.begin_close();
    active_session.join_and_release();
    masks.begin_close();

#ifdef GUI_FORMS_PREPARED_TEXT
    gui_forms::PreparedTextService prepared{};
    require(prepared.instance() != 0, "Application exports the prepared service");
    prepared.begin_close();
#endif

    gui_forms::AudioEngine audio{};
    const gui_forms::AudioStatus audio_opened = audio.open(true);
    require(audio_opened == gui_forms::AudioStatus::ok, "offline audio opens beside Application");
    gui_forms::AudioLoopTransport loop{};
    const gui_forms::AudioLoopStatus attached = audio.loop_transport(loop);
    require(attached == gui_forms::AudioLoopStatus::ok, "Audio exports the loop transport");
    std::array<float, 16> silence{};
    const gui_forms::AudioStatus rendered = audio.render(silence);
    require(rendered == gui_forms::AudioStatus::ok, "shared Threading and offline audio coexist");
}
} // namespace

int main() {
    try {
        application_text_audio();
        std::cout << "Application, TextMasks, PreparedText and Audio use one Core\n";
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
