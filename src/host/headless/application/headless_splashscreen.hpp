#pragma once

#include "../../../core/application/splashscreen_surface.hpp"
#include "../../../core/display/recording_painter/recording_painter.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace gui_forms::host {

// What a headless splashscreen showed, kept by the test that made it and
// shared with the surface.
struct HeadlessSplashscreenRecord final {
    std::uint64_t presented{};
    std::string status{};
    std::optional<double> progress{};
    bool closed{};
};

// The headless reference host's splashscreen surface: paints each frame
// with paint_splashscreen into a recording painter (no pixels) and notes
// what it showed in the shared record; its destruction is the window's
// close.
class HeadlessSplashscreenSurface final : public detail::SplashscreenSurface {
public:
    explicit HeadlessSplashscreenSurface(std::shared_ptr<HeadlessSplashscreenRecord> record) noexcept
        : record_(std::move(record)) {}
    ~HeadlessSplashscreenSurface() override {
        if (record_) (*record_).closed = true;
    }
    HeadlessSplashscreenSurface(const HeadlessSplashscreenSurface&) = delete;
    HeadlessSplashscreenSurface& operator=(const HeadlessSplashscreenSurface&) = delete;

    bool present(const detail::SplashscreenFrame& frame) override {
        detail::RecordingPainter painter{};
        detail::paint_splashscreen(painter, frame);
        if (record_) {
            (*record_).presented = (*record_).presented + 1U;
            (*record_).status.assign(frame.status);
            (*record_).progress = frame.progress;
        }
        return true;
    }

private:
    std::shared_ptr<HeadlessSplashscreenRecord> record_;
};

} // namespace gui_forms::host
