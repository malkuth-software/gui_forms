#pragma once

// The authority of shared application resources over the windows that use
// them (B10): the theme change in two phases (inline restyles collected,
// then committed and the theme applied), the colour scheme switch, and the
// repaint after an image is replaced or removed.
//
// Holder: ApplicationResources.
//
// Stale use: a window is registered with its resources from construction to
// destruction, and the resources reach only windows still listed, so they
// never reach a destroyed root. The functions are static and keep nothing.

#include "gui_forms/surface_root/surface_root.hpp"

#include <memory>
#include <vector>

namespace gui_forms::detail {

class SurfaceResourceAccess final {
public:
    SurfaceResourceAccess() = delete;

    // The inline styles of `root`'s trees compiled again against `theme`
    // (SurfaceRoot::collect_inline_restyles); changes nothing.
    [[nodiscard]] static bool collect_inline_restyles(const SurfaceRoot& root, const Theme& theme,
                                                      std::vector<InlineRestyle>& restyles,
                                                      std::vector<ThemeCompileDiagnostic>& diagnostics) {
        const bool result = root.collect_inline_restyles(theme, restyles, diagnostics);
        return result;
    }
    // Installs what collect_inline_restyles collected.
    static void commit_inline_restyles(std::vector<InlineRestyle>& restyles) {
        SurfaceRoot::commit_inline_restyles(restyles);
    }
    static void apply_theme(SurfaceRoot& root, const std::shared_ptr<const Theme>& theme) { root.apply_theme(theme); }
    static void follow_host_colour_scheme(SurfaceRoot& root, bool automatic) {
        root.follow_host_colour_scheme(automatic);
    }
    static void repaint_for_image_replacement(SurfaceRoot& root) { root.repaint_for_image_replacement(); }
    static void repaint_for_image_removal(SurfaceRoot& root) { root.repaint_for_image_removal(); }
};

} // namespace gui_forms::detail
