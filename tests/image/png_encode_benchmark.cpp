// Measures the owned PNG encoder's size and time at every compression level
// over a set of PNG files (decoded first with the owned decoder, so the
// workload is the image, not its previous encoding). Not a test: run by hand.
//   gui_forms_png_encode_benchmark <repetitions> <file.png>...
// Prints CSV: level,files,raw_bytes,encoded_bytes,ratio,median_ms_total,min_ms_total
// where the times are the sum over files of each file's median (or minimum)
// encode time across repetitions.
#include "../../src/core/image/png_pixels.hpp"
#include "../../src/core/image/png_writer.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {
using namespace gui_forms;
using Bytes = std::vector<std::byte>;

struct Image final {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    Bytes pixels{};
};

bool load(const char* path, Image& image_out) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    const std::vector<char> text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    Bytes encoded(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) encoded[index] = static_cast<std::byte>(text[index]);
    image::PngPixelDecoder decoder{};
    image::PngPixelRequest request{};
    request.target = {image::PixelOrder::rgba, image::PixelAlpha::straight};
    request.purpose = image::PngPixelPurpose::clipboard;
    const image::PngPixelResult inspected = decoder.inspect(encoded, request);
    if (inspected.error != image::PngPixelError::none) return false;
    image_out.width = inspected.width;
    image_out.height = inspected.height;
    image_out.pixels.resize(static_cast<std::size_t>(inspected.width) * inspected.height * 4U);
    const image::PngPixelResult decoded = decoder.decode(encoded, request, image_out.pixels,
        static_cast<std::size_t>(inspected.width) * 4U);
    return decoded.error == image::PngPixelError::none;
}

// A decimal count argument: the whole text as an int, else 0 (a value the
// caller refuses). Locale-free, and reports overflow (atoi does neither).
[[nodiscard]] int parsed_count(const std::string_view text) noexcept {
    int value = 0;
    const char* const end = text.data() + text.size();
    const std::from_chars_result parsed = std::from_chars(text.data(), end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        return 0;
    }
    return value;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <repetitions> <file.png>...\n", argv[0]);
        return 2;
    }
    const int repetitions = parsed_count(argv[1]);
    if (repetitions < 1) return 2;
    std::vector<Image> images{};
    for (int index = 2; index < argc; ++index) {
        Image loaded{};
        if (!load(argv[index], loaded)) {
            std::fprintf(stderr, "skipped %s\n", argv[index]);
            continue;
        }
        images.push_back(loaded);
    }
    std::printf("level,files,raw_bytes,encoded_bytes,ratio,median_ms_total,min_ms_total\n");
    std::vector<double> samples(static_cast<std::size_t>(repetitions));
    for (int level = 0; level <= 9; ++level) {
        std::uint64_t raw_total = 0;
        std::uint64_t encoded_total = 0;
        double median_total = 0.0;
        double minimum_total = 0.0;
        for (const Image& item : images) {
            image::PngEncodeInput input{};
            input.width = item.width;
            input.height = item.height;
            input.row_bytes = static_cast<std::uint64_t>(item.width) * 4U;
            input.pixels = item.pixels;
            input.compression_level = level;
            Bytes png{};
            for (int repetition = 0; repetition < repetitions; ++repetition) {
                const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
                const bool encoded = image::encode_png(input, png);
                const std::chrono::steady_clock::time_point stop = std::chrono::steady_clock::now();
                if (!encoded) return 1;
                const std::chrono::duration<double, std::milli> elapsed = stop - start;
                samples[static_cast<std::size_t>(repetition)] = elapsed.count();
            }
            std::sort(samples.begin(), samples.end());
            median_total += samples[samples.size() / 2U];
            minimum_total += samples[0];
            raw_total += item.pixels.size();
            encoded_total += png.size();
        }
        const double ratio = raw_total == 0U ? 0.0 :
            static_cast<double>(encoded_total) / static_cast<double>(raw_total);
        std::printf("%d,%zu,%llu,%llu,%.4f,%.1f,%.1f\n", level, images.size(),
                    static_cast<unsigned long long>(raw_total), static_cast<unsigned long long>(encoded_total),
                    ratio, median_total, minimum_total);
    }
    return 0;
}
