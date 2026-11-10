#pragma once

#include "../../../core/display/snapshot/splash_baker.hpp"
#include "../../../core/resources/fonts/bundled_font_set.hpp"

#include <memory>

namespace gui_forms::render {

// The Splash baker's raster through the CPU Skia painter: one SkiaRaster
// made on the baker's worker, with the run's bundled faces registered on it
// (BundledFontSet is any-thread: a fallback face loads on whichever thread
// first shapes with it).
class SkiaSplashBakeRasterFactory final : public gui_forms::detail::SplashBakeRasterFactory {
public:
    // `fonts` may be null: text then measures and draws nothing, as in a
    // window without its faces.
    explicit SkiaSplashBakeRasterFactory(std::shared_ptr<gui_forms::detail::BundledFontSet> fonts) noexcept;
    [[nodiscard]] std::unique_ptr<gui_forms::detail::SplashBakeRaster> make() override;

private:
    std::shared_ptr<gui_forms::detail::BundledFontSet> fonts_;
};

// What a host holds for the length of its run (Part S, phase 2): the run's
// baker over the Skia raster, installed as the run's (current_splash_baker)
// while this object lives. Declare it after the run's font set and before
// the windows, on the UI thread; it uninstalls, then stops and joins its
// worker when it goes, before Application::shutdown ends the Skia library.
class SkiaSplashBakeRun final {
public:
    explicit SkiaSplashBakeRun(std::shared_ptr<gui_forms::detail::BundledFontSet> fonts);
    ~SkiaSplashBakeRun();
    SkiaSplashBakeRun(const SkiaSplashBakeRun&) = delete;
    SkiaSplashBakeRun& operator=(const SkiaSplashBakeRun&) = delete;

private:
    // Destroyed in reverse: the installation goes first, then the baker.
    std::unique_ptr<gui_forms::detail::SplashBaker> baker_;
    std::unique_ptr<gui_forms::detail::SplashBakerInstallation> installation_;
};

} // namespace gui_forms::render
