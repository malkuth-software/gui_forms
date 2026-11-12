#pragma once
#include "gui_forms/host/types/host_types.hpp"
#include "gui_forms/export/application_export.hpp"
namespace gui_forms::detail {
// Native clipboard DIB interchange only; not an application file codec.
[[nodiscard]] GUI_FORMS_API std::vector<std::byte> encode_clipboard_dib(HostImageView image);
// The result is read at `generation`.
[[nodiscard]] GUI_FORMS_API HostClipboardImageResult decode_clipboard_dib(std::span<const std::byte> bytes,
                                                            std::uint64_t generation);
}
