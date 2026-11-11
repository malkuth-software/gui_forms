// Lab 8: the drawing executor (the path a program drawing through the
// System.Drawing-shaped API takes; built libraries of D:\gui_forms_h,
// c3f1824a9, read only).
//
// A. One frame of a waterfall: an opaque 1024x400 bitmap drawn 1:1 onto a
//    1024x600 target, recorded and executed, on a target that owns its
//    pixels and on one that borrows them. Against copying the rows, which
//    is what the draw amounts to; the bytes are compared.
// B. 2,000 lines as 2,000 draw_line commands against one draw_line_segments.
// C. 2,000 rectangles as 2,000 fill_rectangle commands against one
//    fill_rectangles.

#include "drawing_skia.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

using namespace gui_drawing;
using namespace gui_drawing::render;

namespace lab {

using Clock = std::chrono::steady_clock;

template <typename Work>
static double best_seconds(const int repeats, Work work) {
    double best = 1.0e30;
    for (int repeat = 0; repeat < repeats; ++repeat) {
        const Clock::time_point start = Clock::now();
        work();
        const Clock::time_point end = Clock::now();
        best = std::min(best, std::chrono::duration<double>(end - start).count());
    }
    return best;
}

static void waterfall(SkiaExecutor& executor) {
    constexpr std::uint32_t width = 1024U;
    constexpr std::uint32_t image_height = 400U;
    constexpr std::uint32_t height = 600U;
    Bitmap source(width, image_height);
    std::mt19937 rng(3U);
    {
        const BitmapLockView lock = source.lock(BitmapLockMode::write);
        std::uint32_t* const words = reinterpret_cast<std::uint32_t*>(lock.writable_data);
        for (std::size_t index = 0U; index < static_cast<std::size_t>(width) * image_height; ++index) {
            words[index] = rng() | 0xff000000U;
        }
        source.unlock(lock.token);
    }
    const ImageAttributes attributes;
    const RectF destination{0.0, 100.0, static_cast<double>(width), static_cast<double>(image_height)};
    const RectF whole{0.0, 0.0, static_cast<double>(width), static_cast<double>(image_height)};

    Bitmap owned(width, height);
    const double owned_time = best_seconds(9, [&]() {
        GraphicsRecorder recorder;
        recorder.draw_image(source, destination, whole, attributes);
        static_cast<void>(executor.execute(recorder, owned));
    });

    std::vector<std::uint32_t> memory(static_cast<std::size_t>(width) * height, 0xff000000U);
    const std::unique_ptr<Bitmap> borrowed =
        Bitmap::borrow(width, height, static_cast<std::size_t>(width) * 4U, reinterpret_cast<std::byte*>(memory.data()));
    const double borrowed_time = best_seconds(9, [&]() {
        GraphicsRecorder recorder;
        recorder.draw_image(source, destination, whole, attributes);
        static_cast<void>(executor.execute(recorder, *borrowed));
    });

    std::vector<std::uint32_t> copied(static_cast<std::size_t>(width) * height, 0xff000000U);
    const double copy_time = best_seconds(9, [&]() {
        const BitmapLockView lock = source.lock(BitmapLockMode::read);
        for (std::uint32_t row = 0U; row < image_height; ++row) {
            std::memcpy(copied.data() + static_cast<std::size_t>(row + 100U) * width,
                        lock.data + static_cast<std::size_t>(row) * lock.row_bytes, static_cast<std::size_t>(width) * 4U);
        }
        source.unlock(lock.token);
    });
    const bool equal = std::memcmp(memory.data(), copied.data(), memory.size() * 4U) == 0;
    const double megabytes = static_cast<double>(width) * image_height * 4.0 / 1.0e6;
    std::printf("waterfall frame, 1024x400 image (%.1f MB): owned target %6.3f ms | borrowed target %6.3f ms | rows copied %6.3f ms (x%.0f against borrowed; %s)\n",
                megabytes, owned_time * 1.0e3, borrowed_time * 1.0e3, copy_time * 1.0e3, borrowed_time / copy_time,
                equal ? "same bytes" : "different bytes");
}

static void lines(SkiaExecutor& executor) {
    constexpr int count = 2000;
    std::mt19937 rng(4U);
    std::vector<PointF> points(static_cast<std::size_t>(count) * 2U);
    for (PointF& point : points) {
        point.x = static_cast<double>(rng() % 1024U);
        point.y = static_cast<double>(rng() % 600U);
    }
    const Pen pen(Color::from_argb(255U, 30U, 90U, 200U), 1.0);
    std::vector<std::uint32_t> first(1024U * 600U, 0xffffffffU);
    std::vector<std::uint32_t> second(1024U * 600U, 0xffffffffU);
    const std::unique_ptr<Bitmap> one = Bitmap::borrow(1024U, 600U, 4096U, reinterpret_cast<std::byte*>(first.data()));
    const std::unique_ptr<Bitmap> two = Bitmap::borrow(1024U, 600U, 4096U, reinterpret_cast<std::byte*>(second.data()));
    const double each = best_seconds(7, [&]() {
        std::fill(first.begin(), first.end(), 0xffffffffU);
        GraphicsRecorder recorder;
        for (int index = 0; index < count; ++index) {
            recorder.draw_line(pen, points[static_cast<std::size_t>(index) * 2U], points[static_cast<std::size_t>(index) * 2U + 1U]);
        }
        static_cast<void>(executor.execute(recorder, *one));
    });
    const double batch = best_seconds(7, [&]() {
        std::fill(second.begin(), second.end(), 0xffffffffU);
        GraphicsRecorder recorder;
        recorder.draw_line_segments(pen, points);
        static_cast<void>(executor.execute(recorder, *two));
    });
    std::size_t different = 0U;
    for (std::size_t index = 0U; index < first.size(); ++index) different += first[index] != second[index] ? 1U : 0U;
    std::printf("2,000 lines: one command each %7.3f ms (%5.0f ns each) | one batch %7.3f ms x%.1f (%zu pixels differ)\n",
                each * 1.0e3, each * 1.0e9 / count, batch * 1.0e3, each / batch, different);
}

static void rectangles(SkiaExecutor& executor) {
    constexpr int count = 2000;
    std::mt19937 rng(5U);
    std::vector<RectF> rects(count);
    for (RectF& rect : rects) {
        rect = RectF{static_cast<double>(rng() % 1000U), static_cast<double>(rng() % 580U), 12.0, 8.0};
    }
    const SolidBrush brush(Color::from_argb(255U, 200U, 60U, 40U));
    std::vector<std::uint32_t> first(1024U * 600U, 0xffffffffU);
    std::vector<std::uint32_t> second(1024U * 600U, 0xffffffffU);
    const std::unique_ptr<Bitmap> one = Bitmap::borrow(1024U, 600U, 4096U, reinterpret_cast<std::byte*>(first.data()));
    const std::unique_ptr<Bitmap> two = Bitmap::borrow(1024U, 600U, 4096U, reinterpret_cast<std::byte*>(second.data()));
    const double each = best_seconds(7, [&]() {
        GraphicsRecorder recorder;
        for (const RectF& rect : rects) recorder.fill_rectangle(brush, rect);
        static_cast<void>(executor.execute(recorder, *one));
    });
    const double batch = best_seconds(7, [&]() {
        GraphicsRecorder recorder;
        recorder.fill_rectangles(brush, rects);
        static_cast<void>(executor.execute(recorder, *two));
    });
    const bool equal = first == second;
    std::printf("2,000 rectangles: one command each %7.3f ms (%5.0f ns each) | one batch %7.3f ms (%4.0f ns each) x%.1f (%s)\n",
                each * 1.0e3, each * 1.0e9 / count, batch * 1.0e3, batch * 1.0e9 / count, each / batch,
                equal ? "same bytes" : "different bytes");
    // What the pixels themselves cost: 12x8 opaque words each.
    std::vector<std::uint32_t> third(1024U * 600U, 0xffffffffU);
    const double direct = best_seconds(7, [&]() {
        for (const RectF& rect : rects) {
            const std::size_t x = static_cast<std::size_t>(rect.x);
            const std::size_t y = static_cast<std::size_t>(rect.y);
            for (std::size_t row = 0U; row < 8U; ++row) {
                std::uint32_t* const out = third.data() + (y + row) * 1024U + x;
                for (std::size_t column = 0U; column < 12U; ++column) out[column] = 0xffc83c28U;
            }
        }
    });
    std::printf("                  the same pixels written directly %7.3f ms (%4.0f ns each; %s)\n", direct * 1.0e3,
                direct * 1.0e9 / count, third == second ? "same bytes" : "different bytes");
}

} // namespace lab

int main() {
    SkiaExecutor executor;
    lab::waterfall(executor);
    lab::lines(executor);
    lab::rectangles(executor);
    return 0;
}
