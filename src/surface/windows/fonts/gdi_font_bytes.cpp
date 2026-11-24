// The Windows host's installed-font probe (gdi_font_bytes.hpp). Moved from
// the raster's per-thread state so that Win32 types stay in
// src/surface/windows/.
#include "gdi_font_bytes.hpp"

#include "../../../platform/library_files.hpp"

#include <windows.h>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <stxlib/buffer.hpp>
#include <stxlib/result.hpp>
#include <stxlib/types.hpp>
#include <stxlib/view.hpp>
#include <utility>
#include <vector>

namespace gui_forms::host {
namespace {

// A file's bytes, empty when it cannot be read, is empty or is larger than
// maximum_bytes.
std::vector<std::byte> read_file(const std::filesystem::path& path, std::size_t maximum_bytes) {
    const stxlib::OperationResult<stxlib::Buffer<stxlib::Byte>> read = platform::read_whole_file(path, maximum_bytes);
    if (read.failed()) return {};
    const stxlib::View<stxlib::Byte> view = (*read.value()).view();
    const std::byte* const first = reinterpret_cast<const std::byte*>(view.data());
    std::vector<std::byte> bytes(first, first + view.count());
    return bytes;
}

std::wstring wide_from_utf8(std::string_view value) {
    if (value.empty() || value.size() > static_cast<std::size_t>(INT_MAX)) return {};
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0);
    if (required <= 0) return {};
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(),
                            required) != required) return {};
    return result;
}

std::string utf8_from_wide(std::wstring_view value) {
    // WideCharToMultiByte takes an int count, as wide_from_utf8's does.
    if (value.empty() || value.size() > static_cast<std::size_t>(INT_MAX)) return {};
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    std::string converted(
        static_cast<std::size_t>(std::max(0, size)), '\0');
    if (size > 0) {
        WideCharToMultiByte(
            CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
            converted.data(), size, nullptr, nullptr);
    }
    return converted;
}

// The installed font file the registry lists under the family's own name
// ("<family> Bold (TrueType)" and so on), so its name matches by
// construction.
std::vector<std::byte> registered_font_bytes(std::wstring_view family,
                                             std::uint32_t style, std::size_t maximum_bytes) {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
                      0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return {};
    struct KeyCloser final {
        HKEY value;
        ~KeyCloser() { if (value != nullptr) RegCloseKey(value); }
    } closer{key};
    std::vector<std::wstring> names;
    if ((style & 1U) != 0U) {
        names.emplace_back(family.data(), family.size());
        names.back() += L" Bold (TrueType)";
    } else if ((style & 2U) != 0U) {
        names.emplace_back(family.data(), family.size());
        names.back() += L" Italic (TrueType)";
        names.emplace_back(family.data(), family.size());
        names.back() += L" Oblique (TrueType)";
    }
    names.emplace_back(family.data(), family.size());
    names.back() += L" (TrueType)";
    for (const std::wstring& name : names) {
        DWORD type{};
        DWORD bytes{};
        if (RegQueryValueExW(key, name.c_str(), nullptr, &type, nullptr, &bytes) !=
                ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t) ||
            bytes > 32768U * sizeof(wchar_t)) continue;
        std::wstring value(bytes / sizeof(wchar_t), L'\0');
        if (RegQueryValueExW(key, name.c_str(), nullptr, &type,
                            reinterpret_cast<BYTE*>(value.data()), &bytes) !=
            ERROR_SUCCESS) continue;
        while (!value.empty() && value.back() == L'\0') value.pop_back();
        if (value.empty()) continue;
        std::filesystem::path path(value);
        if (!path.is_absolute()) {
            std::vector<wchar_t> windows_path(32768);
            const UINT count = GetWindowsDirectoryW(
                windows_path.data(), static_cast<UINT>(windows_path.size()));
            if (count == 0 || count >= windows_path.size()) continue;
            path = std::filesystem::path(
                std::wstring(windows_path.data(), count)) / L"Fonts" / path;
        }
        if (std::vector<std::byte> encoded = read_file(path, maximum_bytes); !encoded.empty()) return encoded;
    }
    return {};
}

} // namespace

GdiFontBytes gdi_font_bytes(std::string_view family, std::uint32_t style, std::size_t maximum_bytes) {
    GdiFontBytes result{};
    const std::wstring requested = wide_from_utf8(family);
    if (requested.empty() || requested.size() >= LF_FACESIZE) return result;
    HDC device = CreateCompatibleDC(nullptr);
    if (device == nullptr) return result;
    const int weight = (style & 1U) != 0U ? FW_BOLD : FW_NORMAL;
    HFONT font = CreateFontW(-16, 0, 0, 0, weight,
                             (style & 2U) != 0U ? TRUE : FALSE,
                             FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH,
                             requested.c_str());
    HGDIOBJ previous = font == nullptr ? nullptr : SelectObject(device, font);
    if (previous != nullptr && previous != HGDI_ERROR) {
        wchar_t selected_name[LF_FACESIZE]{};
        const int selected_count = GetTextFaceW(device, LF_FACESIZE, selected_name);
        const bool named = selected_count > 1 &&
            CompareStringOrdinal(selected_name, -1, requested.c_str(),
                                 static_cast<int>(requested.size()), TRUE) == CSTR_EQUAL;
        if (selected_count > 0) result.selected = utf8_from_wide(selected_name);
        const DWORD size = named ? GetFontData(device, 0, 0, nullptr, 0) : GDI_ERROR;
        if (size != GDI_ERROR && size > 0U && size <= maximum_bytes) {
            std::vector<std::byte> bytes(size);
            if (GetFontData(device, 0, 0, bytes.data(), size) == size) result.bytes = std::move(bytes);
        }
        SelectObject(device, previous);
    }
    if (font != nullptr) DeleteObject(font);
    DeleteDC(device);
    if (result.bytes.empty()) {
        result.bytes = registered_font_bytes(requested, style, maximum_bytes);
        if (!result.bytes.empty()) result.selected = std::string(family) + " (registry)";
    }
    return result;
}

} // namespace gui_forms::host
