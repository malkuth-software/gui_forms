// The clipboard image decoders over arbitrary bytes (audit I3): the native
// DIB reader, GUI.Forms' own wire format (both with and without the
// allocation padding native clipboards add) and the clipboard PNG path. Each
// input goes to all three. Invariants: a read result holds an image and a
// refused one none; a read image is valid (validate_host_image) and tight
// (row_bytes is four bytes a pixel); every read image is premultiplied (no
// colour channel above its alpha): DIB and PNG convert straight alpha, and
// the wire format clamps colour to alpha (owner ruling, 2026-11-10); a wire
// image read without padding reads the same with it; and every read image
// survives the wire format byte for byte.

#include "../src/core/host/image/clipboard_dib.hpp"
#include "../src/core/host/image/clipboard_image_wire.hpp"
#include "../src/core/host/image/clipboard_png.hpp"
#include "gui_forms/host/image/host_image.hpp"
#include "gui_forms/host/types/host_types.hpp"
#include "fuzz_standalone.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using gui_forms::HostClipboardImageResult;
using gui_forms::HostImage;
using gui_forms::HostServiceError;

// The generation every read is made at; a read result reports it back.
constexpr std::uint64_t read_generation = 7U;

// Names the broken invariant on stderr and stops.
[[noreturn]] void fail(const char* invariant) noexcept {
    std::fputs("clipboard image fuzz: ", stderr);
    std::fputs(invariant, stderr);
    std::fputc('\n', stderr);
    std::abort();
}

// Whether `image` is valid and tight.
bool image_well_formed(const HostImage& image) noexcept {
    if (gui_forms::validate_host_image(image.view()) != gui_forms::HostImageError::none) return false;
    const std::uint64_t tight_row = static_cast<std::uint64_t>(image.width) * 4U;
    const bool tight = image.row_bytes == tight_row && image.pixels.size() == tight_row * image.height;
    return tight;
}

// Whether no colour channel of `image` exceeds its alpha.
bool image_premultiplied(const HostImage& image) noexcept {
    // BGRA: three colour bytes, then alpha.
    for (std::size_t offset = 0U; offset < image.pixels.size(); offset += 4U) {
        const std::uint8_t alpha = static_cast<std::uint8_t>(image.pixels[offset + 3U]);
        for (std::size_t channel = 0U; channel < 3U; ++channel) {
            if (static_cast<std::uint8_t>(image.pixels[offset + channel]) > alpha) return false;
        }
    }
    return true;
}

bool same_image(const HostImage& first, const HostImage& second) noexcept {
    const bool same = first.width == second.width && first.height == second.height &&
                      first.row_bytes == second.row_bytes && first.pixels == second.pixels;
    return same;
}

// Checks one decoder's result and returns its image, or null when it
// refused the bytes.
const HostImage* checked_image(const HostClipboardImageResult& result) {
    const HostImage* const image = result.image();
    const bool read = result.status().error == HostServiceError::none;
    if (read != (image != nullptr)) fail("a read result holds an image and a refused one none");
    if (image == nullptr) return nullptr;
    if (!image_well_formed(*image)) fail("a read image is valid and tight");
    if (!image_premultiplied(*image)) fail("a read image is premultiplied");
    return image;
}

// Writes `image` in the wire format and reads it back: the same image.
void require_wire_round_trip(const HostImage& image) {
    const std::vector<std::byte> wire = gui_forms::detail::encode_clipboard_image(image.view());
    const HostClipboardImageResult again = gui_forms::detail::decode_clipboard_image(wire, read_generation, false);
    const HostImage* const read = again.image();
    if (read == nullptr || !same_image(*read, image)) fail("a read image survives the wire format");
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(data), size);

    const HostClipboardImageResult dib = gui_forms::detail::decode_clipboard_dib(bytes, read_generation);
    const HostImage* const dib_image = checked_image(dib);
    if (dib_image != nullptr) require_wire_round_trip(*dib_image);

    const HostClipboardImageResult tight = gui_forms::detail::decode_clipboard_image(bytes, read_generation, false);
    const HostImage* const tight_image = checked_image(tight);
    const HostClipboardImageResult padded = gui_forms::detail::decode_clipboard_image(bytes, read_generation, true);
    const HostImage* const padded_image = checked_image(padded);
    if (tight_image != nullptr) {
        if (padded_image == nullptr || !same_image(*tight_image, *padded_image)) {
            fail("a wire image read without padding reads the same with it");
        }
        require_wire_round_trip(*tight_image);
    }

    const HostClipboardImageResult png = gui_forms::detail::decode_clipboard_png(bytes, read_generation);
    const HostImage* const png_image = checked_image(png);
    if (png_image != nullptr) require_wire_round_trip(*png_image);
    return 0;
}

#if defined(GUI_FORMS_FUZZ_STANDALONE)
namespace {

// A 3 by 2 premultiplied image with opaque, translucent and clear pixels.
HostImage seed_image() {
    HostImage image{};
    image.width = 3U;
    image.height = 2U;
    image.row_bytes = 12U;
    const std::array<std::uint8_t, 24> pixels{0x10, 0x20, 0x30, 0xff, 0x40, 0x20, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
                                              0xff, 0xff, 0xff, 0xff, 0x01, 0x02, 0x03, 0x04, 0x7f, 0x00, 0x7f, 0x7f};
    image.pixels.reserve(pixels.size());
    for (const std::uint8_t pixel : pixels) image.pixels.push_back(static_cast<std::byte>(pixel));
    return image;
}

std::string as_text(const std::vector<std::byte>& bytes) {
    const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return text;
}

} // namespace

// The seeds are the encoders' own output for one small image: a DIB, the
// wire format and a PNG; the mutations explore around them.
int main(int argument_count, char** argument_values) {
    const HostImage image = seed_image();
    const std::array<std::string, 3> encoded{as_text(gui_forms::detail::encode_clipboard_dib(image.view())),
                                             as_text(gui_forms::detail::encode_clipboard_image(image.view())),
                                             as_text(gui_forms::detail::encode_clipboard_png(image.view()))};
    const std::array<std::string_view, 3> seeds{encoded[0], encoded[1], encoded[2]};
    gui_forms::fuzz::StandaloneRun run{};
    run.name = "clipboard image";
    run.seeds = seeds;
    run.mutated_runs = 30'000U;
    run.random_state = 0x510e527fade682d1ULL;
    run.first_fill = '\0';
    run.second_fill = '\xff';
    gui_forms::fuzz::StandaloneCounts counts{};
    const int status = gui_forms::fuzz::run_standalone(argument_count, argument_values, run, counts);
    return status;
}
#endif
