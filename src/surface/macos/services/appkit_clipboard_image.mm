#include "appkit_clipboard_image.hpp"
#include "../../../core/host/image/clipboard_image_wire.hpp"
#include "../../../core/host/image/clipboard_png.hpp"

#include <cstring>
#include <vector>

namespace gui_forms::host::detail {
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
// Not compiled in a high-integrity build (cmake/HighIntegrity.cmake).

namespace {
// The private type written: GFIM version 2, premultiplied BGRA8.
NSString* const raw_type = @"org.guiforms.bgra8";
// The private type written before SK2 (GFIM version 1, straight RGBA8), still
// read.
NSString* const version_one_type = @"org.guiforms.rgba8";

} // namespace

HostClipboardImageResult read_appkit_clipboard_image(NSPasteboard* pasteboard) {
    if (pasteboard == nil) return HostClipboardImageResult::failed(HostServiceError::backend_failure);
    const std::uint64_t generation = static_cast<std::uint64_t>(pasteboard.changeCount);
    HostClipboardImageResult result = HostClipboardImageResult::nothing_read(generation);
    NSData* raw = [pasteboard dataForType:raw_type];
    if (raw == nil) raw = [pasteboard dataForType:version_one_type];
    if (raw != nil) {
        result = gui_forms::detail::decode_clipboard_image(
            {static_cast<const std::byte*>(raw.bytes), raw.length}, generation, false);
    } else {
        // PNG through the shared owned decoder, as on every host. There is no
        // TIFF fallback (owner decision 4A): PNG and raw pixels only.
        NSData* native = [pasteboard dataForType:NSPasteboardTypePNG];
        if (native != nil) {
            result = gui_forms::detail::decode_clipboard_png(
                {static_cast<const std::byte*>(native.bytes), native.length}, generation);
        }
    }
    // Another program wrote the pasteboard while it was read.
    if (static_cast<std::uint64_t>(pasteboard.changeCount) != generation) {
        return HostClipboardImageResult::failed(HostServiceError::backend_failure);
    }
    return result;
}

HostServiceStatus write_appkit_clipboard_image(NSPasteboard* pasteboard, HostImageView image) {
    if (pasteboard == nil) return {HostServiceError::backend_failure};
    const std::vector<std::byte> raw = gui_forms::detail::encode_clipboard_image(image);
    // PNG through the shared owned encoder, as on every host (it writes an
    // sRGB chunk); no NSBitmapImageRep encoding.
    const std::vector<std::byte> encoded = gui_forms::detail::encode_clipboard_png(image);
    if (raw.empty() || encoded.empty()) return {HostServiceError::backend_failure};
    NSData* png = [NSData dataWithBytes:encoded.data() length:encoded.size()];
    NSData* exact = [NSData dataWithBytes:raw.data() length:raw.size()];
    if (png == nil || exact == nil) return {HostServiceError::backend_failure};
    NSPasteboardItem* item = [[NSPasteboardItem alloc] init];
    if (![item setData:exact forType:raw_type] || ![item setData:png forType:NSPasteboardTypePNG]) {
        return {HostServiceError::backend_failure};
    }
    [pasteboard clearContents];
    return [pasteboard writeObjects:@[item]] ? HostServiceStatus{} :
        HostServiceStatus{HostServiceError::backend_failure};
}
#endif

NSBitmapImageRep* host_image_representation(HostImageView image) {
    if (validate_host_image(image) != HostImageError::none) return nil;
    // A CGImage reads premultiplied BGRA8 as it is (32-bit little-endian,
    // alpha first), as the presents do; NSBitmapImageRep's own planes take
    // only RGBA or ARGB byte order.
    const std::size_t row_length = static_cast<std::size_t>(image.width) * 4U;
    NSMutableData* const data = [NSMutableData dataWithLength:row_length * image.height];
    if (data == nil) return nil;
    unsigned char* const destination = static_cast<unsigned char*>(data.mutableBytes);
    for (std::uint32_t row = 0; row < image.height; ++row) {
        const std::byte* const source = image.pixels.data() + static_cast<std::size_t>(row) * image.row_bytes;
        std::memcpy(destination + static_cast<std::size_t>(row) * row_length, source, row_length);
    }
    CGDataProviderRef provider = CGDataProviderCreateWithCFData((__bridge CFDataRef)data);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGImageRef pixels = nullptr;
    if (provider != nullptr && space != nullptr) {
        pixels = CGImageCreate(image.width, image.height, 8, 32, row_length, space,
                               kCGBitmapByteOrder32Little | static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedFirst),
                               provider, nullptr, false, kCGRenderingIntentDefault);
    }
    if (space != nullptr) CGColorSpaceRelease(space);
    if (provider != nullptr) CGDataProviderRelease(provider);
    if (pixels == nullptr) return nil;
    NSBitmapImageRep* const representation = [[NSBitmapImageRep alloc] initWithCGImage:pixels];
    CGImageRelease(pixels);
    return representation;
}
} // namespace gui_forms::host::detail
