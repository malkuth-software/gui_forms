#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace gui_forms::host::detail {

#if !defined(GUI_FORMS_HIGH_INTEGRITY)
// Not compiled in a high-integrity build (cmake/HighIntegrity.cmake): no
// frame is written to a file.

// Shared tail of the portable frame-capture contract (see capture_path in the
// public host option structs). Writes the encoded PNG bytes to path, prints
// exactly one JSON line {"automation":"capture","saved":true|false,"path":"..."}
// to stdout and flushes it, and returns whether the file was written. An
// empty encoding is reported as not saved without touching the filesystem.
bool complete_frame_capture(std::string_view path, std::span<const std::byte> encoded_png);

[[nodiscard]] std::string frame_capture_report_json(bool saved, std::string_view path);
#endif

} // namespace gui_forms::host::detail
