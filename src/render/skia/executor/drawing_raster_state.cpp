// The raster's per-thread state (drawing_raster_state.hpp): the thread's
// drawing executor and the font registrations made on it, and the font
// resolution both raster entries share (the C ABI's raster service in
// src/abi/drawing_skia_c_api.cpp and the C++ execute_recorder).
#include "drawing_raster_state.hpp"

#include "gui_forms/drawing/font_substitution/font_substitution.hpp"
#if defined(__APPLE__)
#include "core_text_font_bytes.hpp"
#endif

#include <stxlib/error.hpp>
#include <stxlib/result.hpp>
#include <stxlib/thread_records.hpp>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace gui_drawing::render {
namespace {


    bool face_loaded{};
};

// The raster module's per-thread state, one STXlib thread record
// (stxlib/thread_records.hpp; the core and the drawing library keep their
// own, as each library loads separately): the thread's executor and the
// font registrations made on it. The executor is thread-affine and its
// typefaces are its own, so the registrations live and die with it.
struct RasterThreadRecord final {
    // Made on the thread's first raster call: its construction allocates,
    // which a record's initialization may not.
    std::optional<SkiaExecutor> executor{};
    // Bundled faces loaded into the executor, by folded family and slot
    // (load_bundled_face).
    std::unordered_map<std::string, bool> loaded_faces{};
    // Family requests answered and registered, by folded family and slot
    // (ensure_font).
    std::unordered_map<std::string, FontRequest> font_requests{};
    // Whether the packaged faces are registered (register_packaged_fonts).
    bool packaged_fonts_registered{};
};

// The record type's callbacks, run by STXlib on the record's thread:
// construction in zero-filled memory neither allocates nor throws (the
// executor waits for first use); release destroys the executor and the
// maps, which frees memory only.
void initialize_raster_thread_record(RasterThreadRecord* record) noexcept {
    ::new (static_cast<void*>(record)) RasterThreadRecord();
}

void release_raster_thread_record(RasterThreadRecord* record) noexcept {
    (*record).~RasterThreadRecord();
}

using RasterThreadRecordType = stxlib::ThreadRecord<RasterThreadRecord>;
using RasterThreadRecordRegistration = stxlib::Result<RasterThreadRecordType>;

// Registered once per process, by the first thread that rasterizes;
// permanent, as the module stays loaded once its service is installed.
const RasterThreadRecordRegistration& raster_thread_record_registration() noexcept {
    static const RasterThreadRecordRegistration registration =
        RasterThreadRecordType::register_type<&initialize_raster_thread_record,
                                              &release_raster_thread_record>();
    return registration;
}



// The calling thread's record, created on first use.
RasterThreadRecord& raster_thread_record() {
    const RasterThreadRecordType* const type = raster_thread_record_registration().value();
    if (type == nullptr) throw RasterRecordUnavailable(RasterRecordRefusal::not_available);
    stxlib::Result<RasterThreadRecord*> found = (*type).get();
    if (found.failed()) {
        const bool out_of_memory = (*found.error()).kind() == stxlib::ErrorKind::out_of_memory;
        throw RasterRecordUnavailable(out_of_memory ? RasterRecordRefusal::out_of_memory
                                                     : RasterRecordRefusal::not_available);
    }
    RasterThreadRecord* const record = std::move(found).take_value();
    return *record;
}

SkiaExecutor& thread_executor() {
    RasterThreadRecord& record = raster_thread_record();
    if (!record.executor) record.executor.emplace();
    return *record.executor;
}

// The largest font file read or accepted from a host.
constexpr std::size_t maximum_font_bytes = 64U * 1024U * 1024U;

std::vector<std::byte> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return {};
    const std::streamoff length = input.tellg();
    if (length <= 0 || length > static_cast<std::streamoff>(maximum_font_bytes)) {
        return {};
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), length);
    return input ? std::move(bytes) : std::vector<std::byte>{};
}

// Whether GUI_DRAWING_TRACE_FONTS is 1 (B3): read once, as getenv walks the
// environment on every call.
[[nodiscard]] bool read_trace_fonts() {
    const char* const trace = std::getenv("GUI_DRAWING_TRACE_FONTS");
    const bool result = trace != nullptr && std::strcmp(trace, "1") == 0;
    return result;
}

[[nodiscard]] bool trace_fonts() {
    static const bool traced = read_trace_fonts();
    return traced;
}

[[nodiscard]] std::string ascii_folded(std::string_view value) {
    std::string result(value);
    for (char& character : result) {
        if (character >= 'A' && character <= 'Z') character = static_cast<char>(character - 'A' + 'a');
    }
    return result;
}

// Loads one bundled face into a family's slot, once per thread: the
// executor is the thread's, so its registrations are too. The face comes
// from the library's embedded font pack. False when the pack does not hold
// it or its bytes are not a font.
bool load_bundled_face(std::string_view family, std::string_view file, FontFaceStyle slot) {
    std::unordered_map<std::string, bool>& loaded = raster_thread_record().loaded_faces;
    std::string key = ascii_folded(family);
    key += '\x1f';
    key += static_cast<char>('0' + static_cast<int>(slot));
    const std::unordered_map<std::string, bool>::const_iterator found = loaded.find(key);
    if (found != loaded.end()) return (*found).second;
    // From the font pack compiled into the library (font pack stage 4), not from files.
    const bool result = thread_executor().register_bundled_typeface(family, file, static_cast<std::uint32_t>(slot));
    loaded.emplace(std::move(key), result);
    return result;
}

// A host face whose own name is the requested family (decision 10), the
// executor slot its bytes fill, and the host's name for what it selected.
// The bytes are empty when the host has no face of that name.
struct HostFace final {
    std::vector<std::byte> bytes;
    std::uint32_t slot{};
    std::string selected;
};

#if defined(_WIN32) || defined(__APPLE__)
[[nodiscard]] std::uint32_t big_endian(const std::vector<std::byte>& bytes, const std::size_t offset,
                                       const std::size_t count) {
    std::uint32_t result = 0U;
    for (std::size_t index = 0; index < count; ++index) {
        result = (result << 8U) | std::to_integer<std::uint32_t>(bytes[offset + index]);
    }
    return result;
}

// The executor slot a host face fills: the bold (1) and italic (2) bits of
// the face's own OS/2 weight and selection flags, so a weight or slant the
// host only simulated is synthesized by the executor instead of lost. The
// requested slot when the bytes have no readable OS/2 table; every offset is
// checked against the buffer.
[[nodiscard]] std::uint32_t host_face_slot(const std::vector<std::byte>& bytes, const std::uint32_t requested) {
    constexpr std::size_t header_bytes = 12U;
    constexpr std::size_t record_bytes = 16U;
    constexpr std::uint32_t os2_tag = 0x4F532F32U; // 'OS/2'
    constexpr std::size_t selection_end = 64U;     // fsSelection is at offset 62
    if (bytes.size() < header_bytes) return requested;
    const std::size_t count = big_endian(bytes, 4U, 2U);
    if ((bytes.size() - header_bytes) / record_bytes < count) return requested;
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t record = header_bytes + record_bytes * index;
        if (big_endian(bytes, record, 4U) != os2_tag) continue;
        const std::size_t offset = big_endian(bytes, record + 8U, 4U);
        if (offset > bytes.size() || bytes.size() - offset < selection_end) return requested;
        const std::uint32_t weight = big_endian(bytes, offset + 4U, 2U);
        const std::uint32_t selection = big_endian(bytes, offset + 62U, 2U);
        const bool bold = weight != 0U ? weight >= 600U : (selection & 0x20U) != 0U;
        const bool italic = (selection & 0x201U) != 0U; // ITALIC or OBLIQUE
        const std::uint32_t result = (bold ? 1U : 0U) | (italic ? 2U : 0U);
        return result;
    }
    return requested;
}
#endif

#if defined(_WIN32)
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
    if (value.empty()) return {};
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
                                             std::uint32_t style) {
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
        if (std::vector<std::byte> encoded = read_file(path); !encoded.empty()) return encoded;
    }
    return {};
}

// GDI's face for the request, accepted only when GetTextFaceW names the
// requested family; GDI otherwise selects an arbitrary fallback. Then the
// registry's file for that name.
HostFace host_face(std::string_view family, std::uint32_t style) {
    HostFace result{};
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
        if (size != GDI_ERROR && size > 0U && size <= maximum_font_bytes) {
            std::vector<std::byte> bytes(size);
            if (GetFontData(device, 0, 0, bytes.data(), size) == size) result.bytes = std::move(bytes);
        }
        SelectObject(device, previous);
    }
    if (font != nullptr) DeleteObject(font);
    DeleteDC(device);
    if (result.bytes.empty()) {
        result.bytes = registered_font_bytes(requested, style);
        if (!result.bytes.empty()) result.selected = std::string(family) + " (registry)";
    }
    result.slot = host_face_slot(result.bytes, style & 3U);
    return result;
}
#elif defined(__APPLE__)
// CoreText's face for the family, matched with the family name mandatory so
// CoreText cannot substitute another family. It is upright, so an italic
// request is sheared; CoreText may answer another weight than requested (a
// variable family's default instance), which the face's own weight files.
HostFace host_face(std::string_view family, std::uint32_t style) {
    HostFace result{};
    std::optional<std::vector<std::byte>> located =
        gui_drawing::render::core_text_font_bytes(family, (style & 1U) != 0U, maximum_font_bytes);
    if (located) {
        result.bytes = std::move(*located);
        result.slot = host_face_slot(result.bytes, style & 1U);
        result.selected = std::string(family);
    }
    return result;
}
#else
// No host font catalog is consulted here.
HostFace host_face(std::string_view, std::uint32_t) {
    HostFace result{};
    return result;
}
#endif

void trace_font_request(std::string_view family, const FontRequest& request,
                        std::string_view selected) {
    const FontFamilyResolution& resolution = request.resolution;
    std::fprintf(stderr,
                 "gui-drawing-font=request:%.*s|style:%u|source:%s|mapping:%s|family:%.*s|face:%.*s|"
                 "face-style:%u|host-selected:%.*s|loaded:%s\n",
                 static_cast<int>(family.size()), family.data(),
                 static_cast<unsigned>(resolution.requested_style),
                 gui_drawing::font_resolution_source_name(resolution.source),
                 gui_drawing::font_mapping_class_name(resolution.mapping),
                 static_cast<int>(resolution.family.size()), resolution.family.data(),
                 static_cast<int>(resolution.face_file.size()), resolution.face_file.data(),
                 static_cast<unsigned>(resolution.face_style),
                 static_cast<int>(selected.size()), selected.data(),
                 request.face_loaded ? "true" : "false");
    std::fflush(stderr);
}

} // namespace

SkiaExecutor& raster_executor() {
    SkiaExecutor& result = thread_executor();
    return result;
}

// Resolves a family request once per thread and registers its face: the
// table first; an unmapped family takes host bytes only under the
// requested name, else the content family substitutes (decisions 2 and 10).
// A bundled face fills the requested slot even when it is the family's
// nearest style, so nothing is synthesized for it (decision 7).
const FontRequest& ensure_font(std::string_view family, std::uint32_t style) {
    std::unordered_map<std::string, FontRequest>& requests = raster_thread_record().font_requests;
    const FontFaceStyle face_style = gui_drawing::font_face_style(style);
    std::string key = ascii_folded(family);
    key += '\x1f';
    key += static_cast<char>('0' + static_cast<int>(face_style));
    const std::unordered_map<std::string, FontRequest>::const_iterator found = requests.find(key);
    if (found != requests.end()) return (*found).second;

    FontRequest request{};
    request.resolution = gui_drawing::resolve_font_family(family, face_style);
    std::string selected;
    if (request.resolution.source == FontResolutionSource::unmapped) {
        const HostFace host = host_face(family, style);
        selected = host.selected;
        if (!host.bytes.empty() && thread_executor().register_typeface(family, host.bytes, host.slot)) {
            request.resolution.source = FontResolutionSource::host;
            request.face_loaded = true;
        } else {
            request.resolution = gui_drawing::content_font_resolution(family, face_style);
        }
    }
    if (request.resolution.source != FontResolutionSource::host) {
        const bool face = load_bundled_face(request.resolution.family, request.resolution.face_file, face_style);
        request.face_loaded = face && thread_executor().alias_family(family, request.resolution.family);
    }
    request.resolution.requested = {};
    if (trace_fonts()) trace_font_request(family, request, selected);
    const std::pair<std::unordered_map<std::string, FontRequest>::iterator, bool> inserted =
        requests.emplace(std::move(key), request);
    return (*inserted.first).second;
}

void register_packaged_fonts() {
    // The raster executor is the thread's. Native initialization can happen while
    // a resource is decoded on a loader thread, before the UI thread performs its
    // first paint, so font registration must have the same per-thread lifetime.
    // Portsmouth Rapids is the executor's last resort when a face cannot load.
    // Marked only once both are in, so a registration that throws is tried again.
    RasterThreadRecord& record = raster_thread_record();
    if (record.packaged_fonts_registered) return;
    static_cast<void>(ensure_font("Portsmouth Rapids", 0U));
    static_cast<void>(ensure_font("Portsmouth Rapids", 1U));
    record.packaged_fonts_registered = true;
}

void ensure_recorder_fonts(const GraphicsRecorder& recorder,
                           std::uint64_t first_command) {
    const std::span<const gui_drawing::DrawingCommand> commands =
        recorder.commands();
    if (first_command >= commands.size()) return;
    for (const gui_drawing::DrawingCommand& command : commands.subspan(
             static_cast<std::size_t>(first_command))) {
        if (command.kind == gui_drawing::CommandKind::draw_string) {
            static_cast<void>(ensure_font(command.font.family, command.font.style));
        }
    }
}

} // namespace gui_drawing::render
