#import <AppKit/AppKit.h>
#include "gui_forms/platform/macos_host.hpp"
#include "../src/surface/macos/services/appkit_clipboard_image.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}
int main() {
    @autoreleasepool {
        NSPasteboard* pasteboard = [NSPasteboard pasteboardWithUniqueName];
        gui_forms::host::MacHostServiceOptions options;
        options.clipboard_name = pasteboard.name.UTF8String;
        const std::unique_ptr<gui_forms::HostServices> services = gui_forms::host::make_macos_host_services(options);
        // Premultiplied BGRA8: opaque blue, green and red, then red 200,
        // green 100, blue 50 at alpha 128.
        const std::array<std::byte, 16> pixels{
            std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255},
            std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255},
            std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255},
            std::byte{25}, std::byte{50}, std::byte{100}, std::byte{128}};
        require((*services).write_clipboard_image({2, 2, 8, pixels}).accepted(), "native image write failed");
        NSData* written = [pasteboard dataForType:@"org.guiforms.bgra8"];
        require(written != nil && written.length == 32U && static_cast<const unsigned char*>(written.bytes)[4] == 2U,
                "the private type is not version 2 BGRA");
        gui_forms::HostClipboardImageResult result = (*services).read_clipboard_image();
        require(result.status().accepted() && result.image() != nullptr && (*result.image()).width == 2 &&
                (*result.image()).height == 2 &&
                (*result.image()).pixels == std::vector<std::byte>(pixels.begin(), pixels.end()),
                "private native clipboard roundtrip was not exact");
        require((*services).write_clipboard_image({2, 2, 7, pixels}).error == gui_forms::HostServiceError::invalid_argument,
                "invalid image was published");
        require((*services).read_clipboard_image().generation() == result.generation(),
                "invalid write changed the native pasteboard");
        NSData* png = [pasteboard dataForType:NSPasteboardTypePNG];
        require(png != nil && png.length != 0, "interoperable PNG representation missing");
        [pasteboard clearContents];
        require([pasteboard setData:png forType:NSPasteboardTypePNG], "PNG-only fixture write failed");
        result = (*services).read_clipboard_image();
        require(result.status().accepted() && result.image() != nullptr && (*result.image()).width == 2 &&
                (*result.image()).height == 2,
                "PNG from an external clipboard producer was not decoded");
        // PNG holds straight alpha; a valid premultiplied image reads back exactly.
        require((*result.image()).pixels == std::vector<std::byte>(pixels.begin(), pixels.end()),
                "native PNG changed row orientation or colours");
        // A version 1 private image (straight RGBA8) from an older GUI.Forms
        // is premultiplied into BGRA8 when read.
        [pasteboard clearContents];
        const std::array<unsigned char, 24> version_one{'G', 'F', 'I', 'M', 1, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0,
                                                        255, 0, 0, 255, 200, 100, 50, 128};
        require([pasteboard setData:[NSData dataWithBytes:version_one.data() length:version_one.size()]
                            forType:@"org.guiforms.rgba8"],
                "version 1 fixture write failed");
        result = (*services).read_clipboard_image();
        const std::vector<std::byte> premultiplied{std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255},
                                                   std::byte{25}, std::byte{50}, std::byte{100}, std::byte{128}};
        require(result.status().accepted() && result.image() != nullptr && (*result.image()).width == 1U &&
                    (*result.image()).height == 2U && (*result.image()).pixels == premultiplied,
                "a version 1 private image was not premultiplied into BGRA");
        // Status items show a bitmap holding the same premultiplied pixels.
        NSBitmapImageRep* representation =
            gui_forms::host::detail::host_image_representation({2, 2, 8, pixels});
        require(representation != nil && representation.pixelsWide == 2 && representation.pixelsHigh == 2,
                "the status item bitmap was not made");
        // Its samples are premultiplied 8-bit red, green, blue and alpha, in
        // AppKit's sample order.
        const NSBitmapFormat format = representation.bitmapFormat;
        require(representation.bitsPerSample == 8 && (format & NSBitmapFormatAlphaNonpremultiplied) == 0,
                "the status item bitmap is not premultiplied 8-bit");
        const bool alpha_first = (format & NSBitmapFormatAlphaFirst) != 0;
        for (NSInteger index = 0; index < 4; ++index) {
            NSUInteger samples[4]{};
            [representation getPixel:samples atX:index % 2 y:index / 2];
            const std::size_t at = static_cast<std::size_t>(index) * 4U;
            const NSUInteger red = alpha_first ? samples[1] : samples[0];
            const NSUInteger green = alpha_first ? samples[2] : samples[1];
            const NSUInteger blue = alpha_first ? samples[3] : samples[2];
            const NSUInteger alpha = alpha_first ? samples[0] : samples[3];
            require(blue == std::to_integer<NSUInteger>(pixels[at]) &&
                        green == std::to_integer<NSUInteger>(pixels[at + 1U]) &&
                        red == std::to_integer<NSUInteger>(pixels[at + 2U]) &&
                        alpha == std::to_integer<NSUInteger>(pixels[at + 3U]),
                    "the status item bitmap is not read as premultiplied BGRA");
        }
        require((*services).write_clipboard_text("clipboard image test").accepted() &&
                (*services).read_clipboard_image().image() == nullptr, "native text did not replace image");
        [pasteboard clearContents];
        NSURL* file = [NSURL fileURLWithPath:@"/tmp/Rainstar copied image Ω.png"];
        require([pasteboard writeObjects:@[file]], "file reference fixture failed");
        require([pasteboard setData:png forType:NSPasteboardTypePNG], "file icon fixture failed");
        gui_forms::HostClipboardFilesResult files = (*services).read_clipboard_files();
        require(files.status.accepted() && files.paths_utf8.size() == 1 &&
                files.paths_utf8.front() == "/tmp/Rainstar copied image Ω.png",
                "file reference was lost when the clipboard also offered an icon");
        require((*services).write_clipboard_text("ordinary text").accepted() &&
                (*services).read_clipboard_files().paths_utf8.empty(),
                "ordinary clipboard text must not become a file reference");
        [pasteboard releaseGlobally];
        std::cout << "isolated macOS clipboard image roundtrip and PNG interchange passed\n";
    }
}
