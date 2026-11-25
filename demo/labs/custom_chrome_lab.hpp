#pragma once

#include "gui_forms/window.hpp"

#include <memory>

namespace gui_forms::custom_chrome_lab {

// Native-chrome composition demoboard. The model remains portable; each host
// decides how its genuine caption controls participate around the authored
// retained title surface.
[[nodiscard]] std::unique_ptr<Window> make_custom_chrome_lab();

} // namespace gui_forms::custom_chrome_lab
