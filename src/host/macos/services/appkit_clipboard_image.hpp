#pragma once
#import <AppKit/AppKit.h>
#include "gui_forms/host.hpp"
namespace gui_forms::host::detail {
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
// Not compiled in a high-integrity build (cmake/HighIntegrity.cmake).
[[nodiscard]] HostClipboardImageResult read_appkit_clipboard_image(NSPasteboard* pasteboard);
[[nodiscard]] HostServiceStatus write_appkit_clipboard_image(NSPasteboard* pasteboard, HostImageView image);
#endif
// A bitmap representation of a copy of the host image's premultiplied BGRA8
// pixels, read as they are (32-bit little-endian, alpha first, sRGB), or nil
// for an invalid image or when Core Graphics refuses it.
[[nodiscard]] NSBitmapImageRep* host_image_representation(HostImageView image);
} // namespace gui_forms::host::detail
