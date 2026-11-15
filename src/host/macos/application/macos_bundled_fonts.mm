// Locating the bundled fonts for the AppKit host and for
// OffscreenRaster::create (offscreen_font_lookup.hpp).

#include "macos_bundled_fonts.hpp"

// A byte in the module that holds GUI.Forms, whose address names that module to dladdr.
static const char gui_forms_module_anchor{};

// The directory of the image holding this code: the GUI.Forms dynamic library or framework
// binary when another process (a .NET runtime, a plug-in host) loaded it, else the executable
// it is linked into; empty when dladdr cannot say.
static std::filesystem::path gui_forms_module_directory() {
    Dl_info information{};
    const int found = dladdr(&gui_forms_module_anchor, &information);
    if (found == 0 || information.dli_fname == nullptr || information.dli_fname[0] == '\0') return {};
    std::error_code error;
    const std::filesystem::path module = std::filesystem::canonical(information.dli_fname, error);
    if (error) return {};
    return module.parent_path();
}

// Where the bundled faces come from: the embedded font pack, or in a development build
// (GUI_FORMS_DEVELOPMENT_FONT_DIRECTORY) the first of GUI_FORMS_FONT_DIR, the main bundle's
// Contents/Resources/fonts, "fonts" beside the GUI.Forms image and a framework's
// Resources/fonts next to it that holds the required faces. None only when the pack does not
// open, which locate_bundled_fonts counts and reports (bundled_font_diagnostics,
// bundled_font_refusal).
static gui_forms::detail::BundledFontLocation bundle_font_location() {
    std::array<gui_forms::detail::BundledFontCandidate, 4> candidates{};
    const char* override_path = std::getenv("GUI_FORMS_FONT_DIR");
    candidates[0].source = gui_forms::BundledFontSource::environment;
    if (override_path != nullptr) candidates[0].directory = override_path;
    NSString* resources = [[NSBundle mainBundle] resourcePath];
    candidates[1].source = gui_forms::BundledFontSource::application_bundle;
    if (resources != nil)
        candidates[1].directory = std::filesystem::path(resources.fileSystemRepresentation) / "fonts";
    const std::filesystem::path library_directory = gui_forms_module_directory();
    candidates[2].source = gui_forms::BundledFontSource::library_module;
    candidates[3].source = gui_forms::BundledFontSource::library_module;
    if (!library_directory.empty()) {
        candidates[2].directory = library_directory / "fonts";
        // A framework's binary sits in Versions/A beside its Resources folder.
        candidates[3].directory = library_directory / "Resources" / "fonts";
    }
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
