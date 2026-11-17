// The codec-neutral resource seam: the registry admits encoded files by
// signature (load_image, replace_image; PNG is the only admitted format)
// with neutral metadata, and refuses files of no admitted format; the
// ResourcePixelDecoder decodes PNG resources through every target layout to
// bytes that match an independent reference.
#include "../../src/core/image/resource_pixels.hpp"
#include "gui_forms/resources.hpp"
#include "stx_png/encoder.hpp"

#include <stxlib/optional.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

using namespace gui_forms;

using Bytes = std::vector<std::byte>;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

constexpr std::uint32_t width = 37;
constexpr std::uint32_t height = 23;

constexpr std::array<image::PixelTarget, 4> targets{{
    {image::PixelOrder::rgba, image::PixelAlpha::straight},
    {image::PixelOrder::rgba, image::PixelAlpha::premultiplied},
    {image::PixelOrder::bgra, image::PixelAlpha::straight},
    {image::PixelOrder::bgra, image::PixelAlpha::premultiplied}}};

// Straight RGBA samples with every alpha class: zero, full and partial.
Bytes rgba_samples() {
    Bytes samples(static_cast<std::size_t>(width) * height * 4U);
    for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(width) * height; ++pixel) {
        samples[pixel * 4U] = static_cast<std::byte>((pixel * 7U) & 255U);
        samples[pixel * 4U + 1U] = static_cast<std::byte>((pixel * 13U + 50U) & 255U);
        samples[pixel * 4U + 2U] = static_cast<std::byte>((pixel / 3U) & 255U);
        std::size_t alpha = pixel % 5U == 0U ? 0U : pixel % 5U == 1U ? 255U : (pixel * 29U) & 255U;
        samples[pixel * 4U + 3U] = static_cast<std::byte>(alpha);
    }
    return samples;
}

Bytes png_file(const Bytes& rgba, stx_png::PngSourceAlpha alpha) {
    stx_png::PngEncodeInput input{};
    input.width = width;
    input.height = height;
    input.row_bytes = static_cast<std::uint64_t>(width) * 4U;
    input.pixels = rgba;
    input.alpha = alpha;
    Bytes file;
    require(stx_png::encode_png(input, file), "fixture: PNG encode");
    return file;
}

// The expected pixels, computed independently of the codec.
Bytes reference(const Bytes& rgba, image::PixelTarget target) {
    Bytes pixels(rgba.size());
    bool bgra = target.order == image::PixelOrder::bgra;
    bool premultiplied = target.alpha == image::PixelAlpha::premultiplied;
    for (std::size_t pixel = 0; pixel < rgba.size() / 4U; ++pixel) {
        unsigned alpha = std::to_integer<unsigned>(rgba[pixel * 4U + 3U]);
        for (std::size_t channel = 0; channel < 3U; ++channel) {
            unsigned value = std::to_integer<unsigned>(rgba[pixel * 4U + channel]);
            if (premultiplied) {
                // Nearest integer to value * alpha / 255 (no tie: 255 is odd).
                unsigned product = value * alpha;
                value = product / 255U + (product % 255U * 2U > 255U ? 1U : 0U);
            }
            std::size_t slot = bgra && channel != 1U ? 2U - channel : channel;
            pixels[pixel * 4U + slot] = static_cast<std::byte>(value);
        }
        pixels[pixel * 4U + 3U] = static_cast<std::byte>(alpha);
    }
    return pixels;
}

Bytes decode(image::ResourcePixelDecoder& decoder, const ImageResourceView& view, image::PixelTarget target) {
    Bytes pixels(static_cast<std::size_t>(width) * height * 4U);
    image::ResourcePixelResult decoded = decoder.decode(view, target, pixels, static_cast<std::size_t>(width) * 4U);
    require(decoded.error == image::ResourcePixelError::none, "resource decodes");
    require(decoded.width == width && decoded.height == height, "decoded dimensions");
    return pixels;
}

ImageResourceView found(const ImageRegistry& registry, ImageLoadResult loaded) {
    require(static_cast<bool>(loaded), "image admitted");
    stxlib::Optional<ImageResourceView> view = registry.find(loaded.image);
    require(view.present(), "admitted image found");
    return (*view.value());
}

void test_admission(const Bytes& rgba) {
    Bytes png = png_file(rgba, stx_png::PngSourceAlpha::straight);
    Bytes opaque_file = png_file(rgba, stx_png::PngSourceAlpha::opaque);
    ImageRegistry registry;

    ImageResourceView interchange = found(registry, registry.load_image(png));
    require(interchange.encoding == ImageResourceEncoding::png, "PNG encoding recorded");
    require(interchange.metadata.width == width && interchange.metadata.height == height, "PNG dimensions");
    require(interchange.metadata.alpha == ImageAlpha::straight, "RGBA PNG has straight alpha");
    require(interchange.metadata.decoded_byte_count == static_cast<std::uint64_t>(width) * height * 4U, "decoded bytes");
    ImageResourceView opaque = found(registry, registry.load_png(opaque_file));
    require(opaque.metadata.alpha == ImageAlpha::opaque, "truecolor PNG is opaque");

    Bytes garbage(64, std::byte{0x5a});
    require(registry.load_image(garbage).error == ImageResourceError::invalid_signature, "no admitted format refused");
    require(registry.load_png(garbage).error == ImageResourceError::invalid_signature, "load_png refuses it too");
    require(registry.load_image(Bytes{}).error == ImageResourceError::empty_input, "empty file refused");
    Bytes truncated(png.begin(), png.end() - 1);
    require(registry.load_image(truncated).error != ImageResourceError::none, "truncated PNG refused");

    ImageRegistryLimits narrow{};
    narrow.maximum_width = width - 1U;
    ImageRegistry limited(narrow);
    require(limited.load_image(png).error == ImageResourceError::dimension_limit_exceeded, "registry limits apply");

    ImageRegistrySnapshot before = registry.snapshot();
    ImageLoadResult replaced = registry.replace_image(interchange.image, opaque_file);
    require(static_cast<bool>(replaced) && replaced.image.value != interchange.image.value, "replace_image takes PNG");
    ImageRegistrySnapshot after = registry.snapshot();
    require(after.encoded_bytes == before.encoded_bytes - png.size() + opaque_file.size(), "replacement accounting");
    require(registry.replace_png(replaced.image, garbage).error == ImageResourceError::invalid_signature,
            "replace_png refuses a non-PNG file");
    require(registry.replace_image(replaced.image, garbage).error == ImageResourceError::invalid_signature,
            "replace_image refuses a file of no admitted format");
    require(registry.find(replaced.image).present(), "refused replacement leaves the image");
    require(image_resource_error_name(ImageResourceError::allocation_failed) == "allocation_failed", "error names");
}

void test_decoding(const Bytes& rgba) {
    ImageRegistry registry;
    ImageResourceView interchange = found(registry, registry.load_image(png_file(rgba, stx_png::PngSourceAlpha::straight)));
    Bytes opaque_rgba = rgba;
    for (std::size_t pixel = 0; pixel < opaque_rgba.size() / 4U; ++pixel) {
        opaque_rgba[pixel * 4U + 3U] = std::byte{255};
    }
    ImageResourceView opaque = found(registry, registry.load_image(png_file(opaque_rgba, stx_png::PngSourceAlpha::opaque)));
    std::unique_ptr<image::ResourcePixelDecoder> decoder = std::make_unique<image::ResourcePixelDecoder>();
    for (const image::PixelTarget& target : targets) {
        require(decode(*decoder, interchange, target) == reference(rgba, target), "PNG decode matches the reference");
        require(decode(*decoder, opaque, target) == reference(opaque_rgba, target), "opaque PNG decodes opaque");
    }
    (*decoder).release();
    require(decode(*decoder, interchange, targets[1]) == reference(rgba, targets[1]), "decoder works after release");

    Bytes small(static_cast<std::size_t>(width) * height * 4U - 1U);
    image::ResourcePixelResult refused = (*decoder).decode(interchange, targets[0], small, static_cast<std::size_t>(width) * 4U);
    require(refused.error == image::ResourcePixelError::insufficient_storage, "short destination refused");
    ImageResourceView pixels = found(registry, registry.load_bgra32_premultiplied(
        width, height, static_cast<std::uint64_t>(width) * 4U, reference(rgba, targets[3])));
    require(pixels.metadata.alpha == ImageAlpha::premultiplied, "BGRA resources are premultiplied");
    Bytes destination(small.size() + 1U);
    require((*decoder).decode(pixels, targets[0], destination, static_cast<std::size_t>(width) * 4U).error ==
                image::ResourcePixelError::unsupported_encoding,
            "BGRA resources are not decoded");
}

} // namespace

int main() {
    try {
        Bytes rgba = rgba_samples();
        test_admission(rgba);
        test_decoding(rgba);
    } catch (const std::exception& error) {
        std::cerr << "resource_pixels_tests: " << error.what() << '\n';
        return 1;
    }
    std::cout << "resource_pixels_tests: pass\n";
    return 0;
}
