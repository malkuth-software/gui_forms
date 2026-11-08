#pragma once

// Test painters that really write pixels without a renderer: FillPainter
// stores fill_rect colours, FillFramebuffer owns pixels and a FillPainter,
// FillRaster makes FillFramebuffers (moved out of sub_surface_tests.cpp for
// the S7 cell-region tests).

#include "gui_forms/gui_forms.hpp"
#include "gui_forms/paint_framebuffer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace gui_forms::test_support {

// A CPU stand-in for a host raster that really writes pixels: fill_rect
// stores the colour (opaque test colours only, no blending) into the
// rectangle under the current translation and clips, at the framebuffer's
// scale. Everything else draws nothing.
class FillPainter final : public Painter {
public:
    FillPainter(std::vector<std::byte>& pixels, std::uint32_t width, std::uint32_t height, double scale)
        : pixels_(pixels), width_(width), height_(height), scale_(scale) {
        states_.push_back(State{Point{}, Rect{0.0, 0.0, static_cast<double>(width) / scale,
                                              static_cast<double>(height) / scale}});
    }
    void save() override { states_.push_back(states_.back()); }
    void restore() override {
        if (states_.size() > 1U) states_.pop_back();
    }
    void translate(Point offset) override {
        states_.back().offset.x += offset.x;
        states_.back().offset.y += offset.y;
    }
    void clip_rect(Rect rect) override {
        const Rect placed{rect.x + states_.back().offset.x, rect.y + states_.back().offset.y, rect.width,
                          rect.height};
        states_.back().clip = Rect::intersection(states_.back().clip, placed);
    }
    void fill_rect(Rect rect, Color color) override {
        const Rect placed{rect.x + states_.back().offset.x, rect.y + states_.back().offset.y, rect.width,
                          rect.height};
        const Rect visible = Rect::intersection(placed, states_.back().clip);
        if (visible.empty()) return;
        const std::uint32_t left = pixel(visible.x, width_);
        const std::uint32_t top = pixel(visible.y, height_);
        const std::uint32_t right = pixel(visible.x + visible.width, width_);
        const std::uint32_t bottom = pixel(visible.y + visible.height, height_);
        for (std::uint32_t row = top; row < bottom; ++row) {
            for (std::uint32_t column = left; column < right; ++column) {
                const std::size_t at = (static_cast<std::size_t>(row) * width_ + column) * 4U;
                pixels_[at] = static_cast<std::byte>(color.blue);
                pixels_[at + 1U] = static_cast<std::byte>(color.green);
                pixels_[at + 2U] = static_cast<std::byte>(color.red);
                pixels_[at + 3U] = static_cast<std::byte>(color.alpha);
            }
        }
    }
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}

private:
    struct State final {
        Point offset{};
        Rect clip{};
    };
    [[nodiscard]] std::uint32_t pixel(double logical, std::uint32_t limit) const noexcept {
        const double device = std::clamp(std::round(logical * scale_), 0.0, static_cast<double>(limit));
        return static_cast<std::uint32_t>(device);
    }

    std::vector<std::byte>& pixels_;
    std::uint32_t width_{};
    std::uint32_t height_{};
    double scale_{1.0};
    std::vector<State> states_;
};

// Owns its pixels and its painter, so it may move to another thread with
// nothing left behind on the one that made it.
class FillFramebuffer final : public PaintFramebuffer {
public:
    FillFramebuffer(std::uint32_t width, std::uint32_t height, double scale)
        : width_(width), height_(height), pixels_(static_cast<std::size_t>(width) * height * 4U),
          painter_(pixels_, width, height, scale) {}
    bool begin(const ImageRegistry&, Rect) override { return true; }
    void end() override {}
    Painter& painter() noexcept override { return painter_; }
    std::span<std::byte> pixels() noexcept override { return pixels_; }
    std::size_t row_bytes() const noexcept override { return static_cast<std::size_t>(width_) * 4U; }
    std::uint32_t width() const noexcept override { return width_; }
    std::uint32_t height() const noexcept override { return height_; }

private:
    std::uint32_t width_{};
    std::uint32_t height_{};
    std::vector<std::byte> pixels_;
    FillPainter painter_;
};

// The window's framebuffer painter: makes FillFramebuffers. It keeps no
// state, so the window's thread may call it while sub-surfaces paint.
class FillRaster final : public Painter {
public:
    std::unique_ptr<PaintFramebuffer> create_framebuffer(Size logical_size, double scale) override {
        const std::uint32_t width = static_cast<std::uint32_t>(std::ceil(logical_size.width * scale));
        const std::uint32_t height = static_cast<std::uint32_t>(std::ceil(logical_size.height * scale));
        if (width == 0U || height == 0U) return {};
        std::unique_ptr<PaintFramebuffer> result = std::make_unique<FillFramebuffer>(width, height, scale);
        return result;
    }
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
};

} // namespace gui_forms::test_support
