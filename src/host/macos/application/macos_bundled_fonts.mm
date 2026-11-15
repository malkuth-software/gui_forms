// Locating the bundled fonts for the AppKit host and for
// OffscreenRaster::create (offscreen_font_lookup.hpp).

#include "macos_bundled_fonts.hpp"

// Where the bundled faces come from: the embedded font pack, or in a development build
// (GUI_FORMS_DEVELOPMENT_FONT_DIRECTORY) the first of GUI_FORMS_FONT_DIR and the checkout's
// assets/fonts that holds the required faces. None only when the pack does not open, which
// locate_bundled_fonts counts and reports (bundled_font_diagnostics, bundled_font_refusal).
static gui_forms::detail::BundledFontLocation bundle_font_location() {
    std::array<gui_forms::detail::BundledFontCandidate, 2> candidates{};
    const char* override_path = std::getenv("GUI_FORMS_FONT_DIR");
    candidates[0].source = gui_forms::BundledFontSource::environment;
    if (override_path != nullptr) candidates[0].directory = override_path;
    candidates[1].source = gui_forms::BundledFontSource::source_tree;
    candidates[1].directory = gui_forms::detail::development_source_font_directory();
    const gui_forms::detail::BundledFontLocation location =
        gui_forms::detail::locate_bundled_fonts(candidates);
    return location;
}

namespace gui_forms::host::macos {

std::shared_ptr<gui_forms::detail::BundledFontSet> bundle_font_set() {
    const gui_forms::detail::BundledFontLocation location = bundle_font_location();
    const std::shared_ptr<gui_forms::detail::BundledFontSet> set =
        gui_forms::detail::shared_bundled_font_set(location, gui_forms::host::posix_sealed_pages());
    return set;
}

} // namespace gui_forms::host::macos

// OffscreenRaster::create's lookup (offscreen_font_lookup.hpp): the windows' own source.
gui_forms::detail::BundledFontLocation gui_forms::host::offscreen_bundled_font_location() {
    const gui_forms::detail::BundledFontLocation location = bundle_font_location();
    return location;
}
