#pragma once

// The Gallery's capture staging from the shared command line
// (program_options.hpp): --page, --reveal, --open-menu and --scheme, applied
// to the Gallery window after make_gallery and before the host shows it, so
// the first presented (and captured) frame shows them. Every host's Gallery
// main calls this one function, so the options mean the same on Windows,
// macOS and Linux.

#include "gallery.hpp"
#include "program_options.hpp"
#include "gui_forms/window.hpp"

#include <cstddef>
#include <cstdio>
#include <string>

namespace gui_forms::gallery {

// Applies the staging options of `program` to `window` in this order: page,
// reveal, menu, colour scheme. Returns false after printing the refused
// option to stderr; the window then keeps the staging applied before it, and
// main exits 2 without showing it.
[[nodiscard]] inline bool apply_gallery_staging(const demo::ProgramOptions& program, Window& window) {
    if (!program.page.empty() && !select_gallery_page(window, program.page)) {
        std::fprintf(stderr, "unknown Gallery page: %s\n", program.page.c_str());
        return false;
    }
    if (!program.reveal.empty() && !reveal_gallery_control(window, program.reveal)) {
        std::fprintf(stderr, "cannot reveal Gallery control: %s\n", program.reveal.c_str());
        return false;
    }
    if (!program.open_menu.empty()) {
        // <control>[/<item>]: the item is empty when no slash is given.
        const std::string& request = program.open_menu;
        const std::size_t slash = request.find('/');
        const std::string control = request.substr(0U, slash);
        const std::string item = slash == std::string::npos ? std::string() : request.substr(slash + 1U);
        if (!open_gallery_menu(window, control, item)) {
            std::fprintf(stderr, "cannot open Gallery menu: %s\n", request.c_str());
            return false;
        }
    }
    if (!program.scheme.empty()) {
        // parse_program_options admits only "light" and "dark".
        PresentationOverrides overrides{};
        overrides.colour_scheme = program.scheme == "dark" ? ColourScheme::dark : ColourScheme::light;
        window.set_presentation_overrides(overrides);
    }
    return true;
}

}  // namespace gui_forms::gallery
