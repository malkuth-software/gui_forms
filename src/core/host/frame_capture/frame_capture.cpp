#include "frame_capture.hpp"

#include "../../../platform/library_files.hpp"

#include <cstdio>
#include <filesystem>
#include <stxlib/files.hpp>
#include <stxlib/result.hpp>

namespace gui_forms::host::detail {
namespace {

std::string json_escape(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char character : text) {
        switch (character) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (static_cast<unsigned char>(character) < 0x20U) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x",
                              static_cast<unsigned>(static_cast<unsigned char>(character)));
                result += buffer;
            } else {
                result += character;
            }
            break;
        }
    }
    return result;
}

bool write_file(std::string_view path, std::span<const std::byte> bytes) {
    if (path.empty() || bytes.empty()) return false;
    try {
        const std::filesystem::path target{std::string(path)};
        // Created or truncated in place, as the stream opened it; no flush
        // to the device was asked before and none is now.
        stxlib::OperationResult<stxlib::File> opened = platform::open_file(target, stxlib::FileMode::replace);
        if (opened.failed()) return false;
        stxlib::File& output = *opened.value();
        const stxlib::OperationStatus written = output.write_all(platform::stxlib_bytes(bytes));
        const stxlib::OperationStatus closed = output.close();
        return written.succeeded() && closed.succeeded();
    } catch (...) {
        return false;
    }
}

} // namespace

std::string frame_capture_report_json(bool saved, std::string_view path) {
    std::string result = "{\"automation\":\"capture\",\"saved\":";
    result += saved ? "true" : "false";
    result += ",\"path\":\"";
    result += json_escape(path);
    result += "\"}";
    return result;
}

bool complete_frame_capture(std::string_view path, std::span<const std::byte> encoded_png) {
    const bool saved = write_file(path, encoded_png);
    const std::string line = frame_capture_report_json(saved, path);
    std::fprintf(stdout, "%s\n", line.c_str());
    std::fflush(stdout);
    return saved;
}

} // namespace gui_forms::host::detail
