#pragma once

// A renderer-free stand-in for a host's CPU raster, for tests of
// Window::snapshot_control and its users: the framebuffer it makes counts
// the commands replayed into it and holds pixels of a fixed pattern
// (premultiplied, the channel order chosen), so a test can tell what the
// registry received. It draws nothing.

#include "gui_forms/resources/image_registry/image_registry.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/paint_framebuffer.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace test_support {

class CountingPainter final : public gui_forms::Painter {
public:
    void save() override { ++commands; }
    void restore() override { ++commands; }
    void translate(gui_forms::Point) override { ++commands; }
    void clip_rect(gui_forms::Rect) override { ++commands; }
    void fill_rect(gui_forms::Rect, gui_forms::Color) override { ++fills; }
    void stroke_rect(gui_forms::Rect, gui_forms::Color, double) override { ++commands; }
    void draw_line(gui_forms::Point, gui_forms::Point, gui_forms::Color, double) override { ++commands; }
    void draw_text_utf8(gui_forms::Point, std::string_view, gui_forms::FontSpec, gui_forms::Color) override {
        ++texts;
    }
    void draw_image(gui_forms::ImageId, gui_forms::Rect, double) override { ++images; }

    std::size_t commands{};
    std::size_t fills{};
    std::size_t texts{};
    std::size_t images{};
};

// Every pixel is the bytes {1, 2, 3, 4}, premultiplied BGRA8. The
// commands replayed into it go to `painter`, borrowed from its FakeRaster,
// which outlives it.
class FakeFramebuffer final : public gui_forms::PaintFramebuffer {
public:
    FakeFramebuffer(std::uint32_t width, std::uint32_t height, CountingPainter& painter)
        : width_(width), height_(height), pixels_(static_cast<std::size_t>(width) * height * 4U),
          painter_(&painter) {
        for (std::size_t index = 0; index < pixels_.size(); ++index) {
            pixels_[index] = static_cast<std::byte>(1U + index % 4U);
        }
    }
    bool begin(const gui_forms::ImageRegistry&, gui_forms::Rect) override {
        ++begun;
        return true;
    }
    void end() override { ++ended; }
    gui_forms::Painter& painter() noexcept override { return *painter_; }
    gui_forms::BorrowedMutableSpan<std::byte> pixels() noexcept override {
        return gui_forms::detail::lend_mutable_view(pixels_.data(), pixels_.size(), lender_);
    }
    std::size_t row_bytes() const noexcept override { return static_cast<std::size_t>(width_) * 4U; }
    std::uint32_t width() const noexcept override { return width_; }
    std::uint32_t height() const noexcept override { return height_; }

    std::size_t begun{};
    std::size_t ended{};

private:
    std::uint32_t width_{};
    std::uint32_t height_{};
    std::vector<std::byte> pixels_;
    gui_forms::detail::BorrowLender lender_;
    CountingPainter* painter_{};
};

// The framebuffer painter a test installs (Window::set_framebuffer_painter):
// it counts the framebuffers it makes, records the last one's pixel size,
// and gathers every framebuffer's replayed commands in `replayed`.
class FakeRaster final : public gui_forms::Painter {
public:
    std::unique_ptr<gui_forms::PaintFramebuffer> create_framebuffer(gui_forms::Size logical_size,
                                                                   double scale) override {
        ++made;
        const std::uint32_t width = static_cast<std::uint32_t>(std::ceil(logical_size.width * scale));
        const std::uint32_t height = static_cast<std::uint32_t>(std::ceil(logical_size.height * scale));
        last_width = width;
        last_height = height;
        std::unique_ptr<FakeFramebuffer> framebuffer =
            std::make_unique<FakeFramebuffer>(width, height, replayed);
        return framebuffer;
    }

    void save() override {}
    void restore() override {}
    void translate(gui_forms::Point) override {}
    void clip_rect(gui_forms::Rect) override {}
    void fill_rect(gui_forms::Rect, gui_forms::Color) override {}
    void stroke_rect(gui_forms::Rect, gui_forms::Color, double) override {}
    void draw_line(gui_forms::Point, gui_forms::Point, gui_forms::Color, double) override {}
    void draw_text_utf8(gui_forms::Point, std::string_view, gui_forms::FontSpec, gui_forms::Color) override {}
    void draw_image(gui_forms::ImageId, gui_forms::Rect, double) override {}

    CountingPainter replayed{};
    std::size_t made{};
    std::uint32_t last_width{};
    std::uint32_t last_height{};
};

} // namespace test_support
