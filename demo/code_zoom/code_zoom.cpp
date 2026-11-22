#include "code_zoom.hpp"

#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/event/event/event.hpp"
#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "gui_forms/window/window.hpp"

#include <cstddef>
#include <memory>
#include <string_view>

namespace gui_forms::code_zoom {
namespace {

// code_zoom_embedded_source.inc (cmake/embed_text_file.cmake) defines
// embedded_source_path and embedded_source_bytes, the file's bytes and a
// terminating zero.
#include "code_zoom_embedded_source.inc"

} // namespace

std::string_view embedded_source_name() noexcept {
    const std::string_view name(embedded_source_path);
    return name;
}

std::string_view embedded_source_text() noexcept {
    // The array holds the bytes and a terminating zero.
    const std::size_t length = sizeof(embedded_source_bytes) - 1U;
    const std::string_view text(reinterpret_cast<const char*>(embedded_source_bytes), length);
    return text;
}

std::unique_ptr<Window> make_code_zoom() {
    std::shared_ptr<CodeSheet> sheet = make_control<CodeSheet>(StableId(sheet_id));
    static_cast<void>((*sheet).load_source(embedded_source_name(), embedded_source_text()));
    std::unique_ptr<Window> window =
        std::make_unique<Window>(sheet, window_size, std::make_shared<ApplicationResources>());
    on((*window).key_down(), *sheet, &CodeSheet::window_key_down);
    (*window).perform_layout();
    return window;
}

std::shared_ptr<CodeSheet> find_code_sheet(const Window& window) {
    const std::shared_ptr<CodeSheet> sheet = std::dynamic_pointer_cast<CodeSheet>(window.find(sheet_id));
    return sheet;
}

} // namespace gui_forms::code_zoom
