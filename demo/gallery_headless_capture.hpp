#pragma once

// The Gallery's headless page capture (cp32, for previews):
// `--page <id> --capture <path.png> --headless` draws the staged Gallery
// window offscreen, with no host window, and writes it as a PNG. Every host's
// Gallery main calls run_headless_gallery_capture after its staging
// (gallery_staging.hpp) when --headless is given, before it would show the
// window, so the options mean the same on Windows, macOS and Linux.
//
// The sequence: the window is made active and given the --size (the
// Gallery's default size otherwise) and the --scale (1 otherwise); motion is
// reduced and ambient effects are off, so the picture is a still; the window
// is attached to an OffscreenRaster over the bundled faces found where the
// hosts look; frames are painted into one reused framebuffer and the
// scheduler is polled until no procedural pattern or finish tile waits (at
// most maximum_headless_settle_frames; the engine makes them in idle
// frames, E10 and cp31); then the whole window, overlays included (an open
// menu from --open-menu), is captured and encoded.
//
// It prints the hosts' capture line,
// {"automation":"capture","saved":true|false,"path":"..."}, to stdout and
// returns the exit code: 0 when the file was written, 1 otherwise (the raster
// could not be made, the capture or encoding was refused, or the file could
// not be written; the reason goes to stderr). Without --capture it prints
// usage and returns 2.

#include "gallery.hpp"
#include "program_options.hpp"
#include "gui_forms/capture/offscreen_raster/offscreen_raster.hpp"
#include "gui_forms/paint_framebuffer.hpp"
#include "gui_forms/window.hpp"
#include "../src/platform/library_files.hpp"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <stxlib/files.hpp>
#include <stxlib/result.hpp>
#include <vector>

namespace gui_forms::gallery {

// Idle frames the settle may take: one procedural pattern a frame, and
// finish tiles by effort; a page needing more shows the remaining grounds.
// A glint tile is past one frame's effort, so it takes a frame of its own:
// the industrial-surfaces page's dialog enamel and title band take some
// 48 at scale 2 (owner, 2026-11-22), with room to spare here.
inline constexpr std::size_t maximum_headless_settle_frames = 128U;

namespace detail {

// `text` with '"' and '\\' escaped and control characters dropped, for the
// capture line's JSON string.
inline std::string json_escaped(std::string_view text) {
    std::string escaped{};
    escaped.reserve(text.size());
    for (const char character : text) {
        if (character == '"' || character == '\\') {
            escaped.push_back('\\');
            escaped.push_back(character);
        } else if (static_cast<unsigned char>(character) >= 0x20U) {
            escaped.push_back(character);
        }
    }
    return escaped;
}

inline void print_capture_line(bool saved, std::string_view path) {
    const std::string escaped = json_escaped(path);
    std::fprintf(stdout, "{\"automation\":\"capture\",\"saved\":%s,\"path\":\"%s\"}\n", saved ? "true" : "false",
                 escaped.c_str());
    std::fflush(stdout);
}

// Paints the whole window into `framebuffer`, as a host's frame does: the
// paint consumes the window's damage and paint dirt. False when the frame
// could not begin or the paint was declined.
inline bool paint_headless_frame(Window& window, PaintFramebuffer& framebuffer) {
    window.perform_layout();
    static_cast<void>(window.take_damage());
    const Rect whole{0.0, 0.0, window.client_size().width, window.client_size().height};
    if (!framebuffer.begin(window.image_resources(), whole)) return false;
    const stxlib::Optional<PaintReceipt> receipt = window.paint(framebuffer.painter(), whole);
    framebuffer.end();
    const bool painted = receipt.present();
    return painted;
}

inline bool engine_work_waiting(const Window& window) {
    const bool waiting = window.engine_parts().patterns_waiting() || window.engine_parts().finish_tiles_waiting();
    return waiting;
}

// Writes `bytes` to `path`, replacing the file. False when it cannot be
// opened or written whole.
inline bool write_file(const std::string& path, const std::vector<std::byte>& bytes) {
    stxlib::OperationResult<stxlib::File> opened =
        platform::open_file(std::filesystem::path(path), stxlib::FileMode::replace);
    if (opened.failed()) return false;
    stxlib::File& file = *opened.value();
    const stxlib::OperationStatus wrote = file.write_all(platform::stxlib_bytes(bytes));
    const stxlib::OperationStatus closed = file.close();
    const bool written = wrote.succeeded() && closed.succeeded();
    return written;
}

} // namespace detail

// `font_directory` names where the bundled faces are; empty (the mains) looks
// where the hosts look. A test without a host passes the source tree's.
[[nodiscard]] inline int run_headless_gallery_capture(const demo::ProgramOptions& program, Window& window,
                                                      const std::filesystem::path& font_directory = {}) {
    if (program.capture_path.empty()) {
        std::fprintf(stderr, "--headless needs --capture <path.png>\n");
        demo::print_program_usage("gui-forms-gallery");
        return 2;
    }
    const std::string& path = program.capture_path;
    window.set_active(true);
    const Size size = program.capture_size.value_or(gallery_default_size);
    window.resize(size);
    const double scale = program.scale_hint > 0.0 ? program.scale_hint : 1.0;
    // A still: the staging's overrides (its colour scheme) are kept.
    PresentationOverrides still{};
    const PresentationOverrides* const staged = window.presentation_overrides();
    if (staged != nullptr) still = *staged;
    still.reduced_motion.set(true);
    still.ambient_motion.set(false);
    window.set_presentation_overrides(still);

    OffscreenRasterOptions raster_options{};
    raster_options.font_directory = font_directory;
    OffscreenRasterCreateResult made = OffscreenRaster::create(raster_options);
    if (!made.succeeded()) {
        const std::string reason = made.detail();
        std::fprintf(stderr, "headless capture: no raster: %s\n", reason.c_str());
        detail::print_capture_line(false, path);
        return 1;
    }
    // Declared before the attachment, which borrows it and ends first.
    const std::unique_ptr<OffscreenRaster> raster = made.take_raster();
    bool saved = false;
    {
        OffscreenAttachment attached(*raster, window);
        window.set_scale(scale);
        window.perform_layout();
        // One framebuffer for every settle frame; the window's size does not
        // change while it settles.
        const std::unique_ptr<PaintFramebuffer> framebuffer = window.create_framebuffer(window.client_size(), scale);
        if (framebuffer == nullptr) {
            std::fprintf(stderr, "headless capture: the raster made no framebuffer\n");
            detail::print_capture_line(false, path);
            return 1;
        }
        FrameTime now = FrameClock::now();
        std::size_t frame = 0U;
        bool painted = detail::paint_headless_frame(window, *framebuffer);
        while (painted && frame < maximum_headless_settle_frames && detail::engine_work_waiting(window)) {
            now += std::chrono::milliseconds(16);
            static_cast<void>(window.poll_frame_schedule(now));
            painted = detail::paint_headless_frame(window, *framebuffer);
            frame = frame + 1U;
        }
        if (detail::engine_work_waiting(window)) {
            std::fprintf(stderr, "headless capture: patterns or finish tiles still waiting after %zu frames\n", frame);
        }
        OffscreenCaptureOptions options{};
        options.scale = scale;
        options.include_overlays = true;
        const std::optional<WindowCapture> capture = attached.capture_window(options);
        std::vector<std::byte> png{};
        if (!capture.has_value()) {
            std::fprintf(stderr, "headless capture: the window capture was refused\n");
        } else if (!encode_window_capture_png(*capture, png)) {
            std::fprintf(stderr, "headless capture: the PNG encoding was refused\n");
        } else if (!detail::write_file(path, png)) {
            std::fprintf(stderr, "headless capture: cannot write %s\n", path.c_str());
        } else {
            saved = true;
        }
    }
    detail::print_capture_line(saved, path);
    const int exit_code = saved ? 0 : 1;
    return exit_code;
}

} // namespace gui_forms::gallery
