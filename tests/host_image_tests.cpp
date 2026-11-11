#include "gui_forms/host.hpp"
#include "headless_host.hpp"
#include "../src/core/host/image/clipboard_image_wire.hpp"
#include "../src/core/host/image/clipboard_dib.hpp"
#include "../src/core/host/image/host_image_pixels.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
using namespace gui_forms;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
class WorkerRead final {
public:
    WorkerRead(HostServices& services, HostServiceError& error) : services_(services), error_(error) {}
    void operator()() const { error_ = services_.read_clipboard_image().status().error; }
private:
    HostServices& services_;
    HostServiceError& error_;
};
class MalformedServices final : public HostServices {
public:
    explicit MalformedServices(bool supported)
        : HostServices({HostCapabilities::current_protocol_version, "malformed-test",
                        supported ? HostCapability::clipboard_images : HostCapability::none}) {}
protected:
    HostMonitorResult query_monitors_impl() override { return {}; }
    HostServiceStatus set_cursor_impl(CursorKind) override { return {}; }
    HostServiceStatus set_pointer_capture_impl(bool, std::uint64_t) override { return {}; }
    HostClipboardTextResult read_clipboard_text_impl() override { return {}; }
    HostServiceStatus write_clipboard_text_impl(std::string_view) override { return {}; }
    HostDialogResult show_dialog_impl(const HostDialogRequest&) override { return {}; }
    HostServiceStatus play_sound_cue_impl(const HostSoundCueRequest&) override { return {}; }
    HostClipboardImageResult read_clipboard_image_impl() override {
        // Malformed on purpose: two rows of eight bytes over four bytes.
        HostImage image{};
        image.width = 2;
        image.height = 2;
        image.row_bytes = 8;
        image.pixels.resize(4);
        return HostClipboardImageResult::image_read(image, 0);
    }
};
void validation_and_wire() {
    const std::array<std::byte, 12> padded{
        std::byte{231}, std::byte{43}, std::byte{19}, std::byte{0},
        std::byte{99}, std::byte{99}, std::byte{99}, std::byte{99},
        std::byte{200}, std::byte{10}, std::byte{5}, std::byte{128}};
    const HostImageView image{1, 2, 8, padded};
    require(validate_host_image(image) == HostImageError::none, "final row padding incorrectly required");
    const std::vector<std::byte> encoded = detail::encode_clipboard_image(image);
    require(encoded.size() == 24U, "wire format included row padding");
    require(encoded[4] == std::byte{2}, "the wire is written as version 2");
    require(encoded[16] == std::byte{231} && encoded[19] == std::byte{0} && encoded[20] == std::byte{200} &&
                encoded[23] == std::byte{128},
            "the version 2 wire did not write every byte");
    // These pixels break the premultiplied contract (colour above alpha), as
    // another process's clipboard bytes may; reading clamps colour to alpha.
    const HostClipboardImageResult decoded = detail::decode_clipboard_image(encoded, 0, false);
    require(decoded.status().accepted() && decoded.image() != nullptr && (*decoded.image()).row_bytes == 4U &&
            (*decoded.image()).pixels[0] == std::byte{0} && (*decoded.image()).pixels[1] == std::byte{0} &&
            (*decoded.image()).pixels[2] == std::byte{0} && (*decoded.image()).pixels[3] == std::byte{0} &&
            (*decoded.image()).pixels[4] == std::byte{128} && (*decoded.image()).pixels[5] == std::byte{10} &&
            (*decoded.image()).pixels[6] == std::byte{5} && (*decoded.image()).pixels[7] == std::byte{128},
            "the version 2 wire did not clamp colour to alpha");
    // A valid premultiplied image reads back byte for byte.
    const std::array<std::byte, 8> valid{
        std::byte{25}, std::byte{50}, std::byte{100}, std::byte{128},
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
    const HostImageView valid_image{2, 1, 8, valid};
    const HostClipboardImageResult valid_decoded =
        detail::decode_clipboard_image(detail::encode_clipboard_image(valid_image), 0, false);
    require(valid_decoded.image() != nullptr &&
                std::equal(valid.begin(), valid.end(), (*valid_decoded.image()).pixels.begin(),
                           (*valid_decoded.image()).pixels.end()),
            "the version 2 wire did not keep a premultiplied image's bytes");
    for (std::size_t length = 0; length < encoded.size(); ++length) {
        const std::span<const std::byte> truncated = std::span<const std::byte>(encoded).first(length);
        require(!detail::decode_clipboard_image(truncated, 0, false).status().accepted(),
                "truncated clipboard image accepted");
    }
    std::vector<std::byte> bad = encoded;
    bad[4] = std::byte{3};
    require(!detail::decode_clipboard_image(bad, 0, false).status().accepted(), "unknown wire version accepted");
    bad[4] = std::byte{0};
    require(!detail::decode_clipboard_image(bad, 0, false).status().accepted(), "wire version 0 accepted");
    bad = encoded;
    bad.push_back(std::byte{0});
    require(!detail::decode_clipboard_image(bad, 0, false).status().accepted(), "trailing wire data accepted");
    require(detail::decode_clipboard_image(bad, 0, true).status().accepted(),
            "native allocation padding was treated as image data");
    require(validate_host_image({0, 1, 4, padded}) == HostImageError::invalid_dimensions,
            "zero width accepted");
    require(validate_host_image({100'000'001U, 1, 400'000'004U, padded}) == HostImageError::too_large,
            "pixel limit ignored");
    require(validate_host_image({1, 2, 3, padded}) == HostImageError::invalid_stride,
            "short stride accepted");
    require(validate_host_image({1, 3, std::uint64_t{1} << 63U, padded}) != HostImageError::none,
            "overflowing source span accepted");
}
// Version 1 of the wire (straight RGBA8, written before SK2) is read through
// the input kernel into premultiplied BGRA8.
void version_one_wire_is_premultiplied() {
    std::vector<std::byte> version_one{std::byte{'G'}, std::byte{'F'}, std::byte{'I'}, std::byte{'M'},
                                       std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0},
                                       std::byte{2}, std::byte{0}, std::byte{0}, std::byte{0},
                                       std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}};
    // Opaque red, then red 200, green 100, blue 50 at alpha 128.
    const std::array<std::byte, 8> straight{std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255},
                                            std::byte{200}, std::byte{100}, std::byte{50}, std::byte{128}};
    version_one.insert(version_one.end(), straight.begin(), straight.end());
    const HostClipboardImageResult read = detail::decode_clipboard_image(version_one, 0, false);
    // round(200 * 128 / 255) = 100, round(100 * 128 / 255) = 50, round(50 * 128 / 255) = 25.
    const std::vector<std::byte> expected{std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255},
                                          std::byte{25}, std::byte{50}, std::byte{100}, std::byte{128}};
    require(read.status().accepted() && read.image() != nullptr && (*read.image()).width == 2U &&
                (*read.image()).pixels == expected,
            "a version 1 image was not premultiplied into BGRA");
}

// Every valid premultiplied channel survives a straight interchange (PNG, DIB,
// icons) exactly; colour under zero alpha becomes zero.
void straight_interchange_is_exact() {
    for (unsigned alpha = 0U; alpha < 256U; ++alpha) {
        for (unsigned channel = 0U; channel <= alpha; ++channel) {
            const std::uint8_t straight =
                detail::straight_channel(static_cast<std::uint8_t>(channel), static_cast<std::uint8_t>(alpha));
            require(detail::premultiplied_channel(straight, static_cast<std::uint8_t>(alpha)) == channel,
                    "a premultiplied channel changed through straight alpha");
        }
    }
    require(detail::straight_channel(200U, 0U) == 0U, "colour under zero alpha was kept");
    require(detail::straight_channel(200U, 100U) == 255U, "colour above alpha was not clamped");
    const std::array<std::byte, 4> premultiplied{std::byte{25}, std::byte{50}, std::byte{100}, std::byte{128}};
    std::array<std::byte, 4> straight{};
    detail::premultiplied_bgra_to_straight_bgra(premultiplied, straight, 1U);
    require(straight == (std::array<std::byte, 4>{std::byte{50}, std::byte{100}, std::byte{199}, std::byte{128}}),
            "the output kernel did not unpremultiply in BGRA order");
}

void native_dib_interchange() {
    // Premultiplied BGRA8: an opaque pixel, then a translucent one.
    const std::array<std::byte, 8> pixels{std::byte{201}, std::byte{33}, std::byte{7}, std::byte{255},
                                      std::byte{101}, std::byte{42}, std::byte{13}, std::byte{128}};
    const std::vector<std::byte> dib = detail::encode_clipboard_dib({1, 2, 4, pixels});
    // The DIB holds straight alpha: 101, 42 and 13 at alpha 128 unpremultiply.
    require(dib[128] == std::byte{201} && dib[129] == std::byte{84} && dib[130] == std::byte{26} &&
                dib[131] == std::byte{128},
            "DIBV5 pixels are not straight BGRA");
    HostClipboardImageResult result = detail::decode_clipboard_dib(dib, 0);
    require(result.status().accepted() && result.image() != nullptr &&
            (*result.image()).pixels == std::vector<std::byte>(pixels.begin(), pixels.end()),
            "DIBV5 changed premultiplied pixels or row order");
    for (std::size_t length = 0; length < dib.size(); ++length) {
        require(!detail::decode_clipboard_dib(std::span<const std::byte>(dib).first(length), 0).status().accepted(),
                "truncated DIB accepted");
    }
    std::vector<std::byte> bottom_up = dib;
    bottom_up[8] = std::byte{2}; bottom_up[9] = bottom_up[10] = bottom_up[11] = std::byte{0};
    for (std::size_t channel = 0; channel < 4; ++channel) std::swap(bottom_up[124 + channel], bottom_up[128 + channel]);
    result = detail::decode_clipboard_dib(bottom_up, 0);
    require(result.status().accepted() && result.image() != nullptr &&
            (*result.image()).pixels == std::vector<std::byte>(pixels.begin(), pixels.end()),
            "bottom-up DIB was inverted");
    std::vector<std::byte> rgb24(48U);
    rgb24[0] = std::byte{40}; rgb24[4] = std::byte{1}; rgb24[8] = std::byte{2};
    rgb24[12] = std::byte{1}; rgb24[14] = std::byte{24};
    rgb24[40] = std::byte{13}; rgb24[41] = std::byte{42}; rgb24[42] = std::byte{101};
    rgb24[44] = std::byte{7}; rgb24[45] = std::byte{33}; rgb24[46] = std::byte{201};
    result = detail::decode_clipboard_dib(rgb24, 0);
    // Rows of blue, green, red bytes: the first image row is red 201, green 33,
    // blue 7, the second red 101, green 42, blue 13.
    require(result.status().accepted() && result.image() != nullptr && (*result.image()).pixels[0] == std::byte{7} &&
            (*result.image()).pixels[2] == std::byte{201} && (*result.image()).pixels[3] == std::byte{255} &&
            (*result.image()).pixels[4] == std::byte{13} && (*result.image()).pixels[6] == std::byte{101},
            "padded 24-bit native DIB was not converted");
    // A 32-bit DIB without an alpha mask (BI_RGB) is opaque whatever its
    // fourth byte holds.
    std::vector<std::byte> rgb32(44U);
    rgb32[0] = std::byte{40}; rgb32[4] = std::byte{1}; rgb32[8] = std::byte{1};
    rgb32[12] = std::byte{1}; rgb32[14] = std::byte{32};
    rgb32[40] = std::byte{13}; rgb32[41] = std::byte{42}; rgb32[42] = std::byte{101}; rgb32[43] = std::byte{9};
    result = detail::decode_clipboard_dib(rgb32, 0);
    require(result.status().accepted() && result.image() != nullptr &&
            (*result.image()).pixels == std::vector<std::byte>{std::byte{13}, std::byte{42}, std::byte{101},
                                                               std::byte{255}},
            "32-bit DIB without an alpha mask was not opaque");
    std::vector<std::byte> invalid = dib;
    invalid[40] = std::byte{1};
    require(detail::decode_clipboard_dib(invalid, 0).status().error == HostServiceError::unsupported,
            "invalid channel mask accepted");
}

void service_ownership_and_failures() {
    host::HeadlessHostServices services;
    std::array<std::byte, 4> pixel{std::byte{211}, std::byte{33}, std::byte{9}, std::byte{0}};
    require(services.read_clipboard_image().status().accepted() && services.read_clipboard_image().image() == nullptr,
            "empty clipboard was reported as an error or image");
    require(services.write_clipboard_image({1, 1, 4, pixel}).accepted(), "image write failed");
    pixel[0] = std::byte{0};
    HostClipboardImageResult first = services.read_clipboard_image();
    require(first.image() != nullptr && (*first.image()).pixels[0] == std::byte{211}, "service borrowed caller pixels");
    // Each read owns its pixels: a released image changed by the caller
    // leaves the clipboard's, and the next read's, as they were.
    std::optional<HostImage> taken = std::move(first).release_image();
    (*taken).pixels[0] = std::byte{0};
    const HostClipboardImageResult second = services.read_clipboard_image();
    require(second.image() != nullptr && (*second.image()).pixels[0] == std::byte{211},
            "read result shared mutable clipboard storage");
    require(services.write_clipboard_image({1, 1, 3, pixel}).error == HostServiceError::invalid_argument,
            "invalid write accepted");
    require(services.read_clipboard_image().generation() == second.generation(),
            "invalid write replaced clipboard contents");
    HostServiceError worker_error = HostServiceError::none;
    std::thread worker(WorkerRead(services, worker_error));
    worker.join();
    require(worker_error == HostServiceError::wrong_thread, "worker accessed clipboard service");
    require(services.write_clipboard_text("text").accepted() && services.read_clipboard_image().image() == nullptr,
            "text publication retained the previous image");
    require(services.write_clipboard_image({1, 1, 4, pixel}).accepted() && !services.read_clipboard_text().has_text,
            "image publication retained the previous text");
    services.shutdown();
    require(services.read_clipboard_image().status().error == HostServiceError::after_shutdown,
            "closed service accepted image read");
    MalformedServices unsupported(false);
    require(unsupported.read_clipboard_image().status().error == HostServiceError::unsupported,
            "missing image capability was ignored");
    MalformedServices malformed(true);
    const HostClipboardImageResult invalid = malformed.read_clipboard_image();
    require(invalid.status().error == HostServiceError::backend_failure && invalid.image() == nullptr,
            "malformed backend pixels reached the consumer");
}
} // namespace
int main() {
    validation_and_wire();
    version_one_wire_is_premultiplied();
    straight_interchange_is_exact();
    native_dib_interchange();
    service_ownership_and_failures();
    std::cout << "host image contracts passed\n";
}
