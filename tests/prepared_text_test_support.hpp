#pragma once

#include "gui_forms/prepared_text.hpp"

#include <stxlib/optional.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace prepared_test {
using namespace gui_forms;

inline void require(const bool condition, const char* const message) {
    if (!condition) throw std::runtime_error(message);
}

class Wake final : public PreparedTextWakeTarget {
public:
    std::atomic<unsigned> count{0};
    void post_prepared_text_wake() noexcept override { count.fetch_add(1, std::memory_order_release); }
};

inline std::vector<std::byte> read_font(const std::filesystem::path& directory) {
    const std::filesystem::path path = directory / "Carlito-Regular.ttf";
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(static_cast<bool>(input), "font opens");
    const std::streamsize length = input.tellg();
    require(length > 0 && length <= 4 * 1024 * 1024, "font size");
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), length);
    require(static_cast<bool>(input), "font read");
    return bytes;
}

inline EncodedFontLease make_bank(PreparedTextService& service, const std::span<const std::byte> bytes) {
    const std::array<PreparedFontSource, 1> sources{{{.encoded = bytes, .role = stxlib::Optional<gui_forms::FontRole>::of(FontRole::content)}}};
    EncodedFontLease bank{};
    const PreparedTextStatus status = service.create_font_bank(sources, bank);
    require(status == PreparedTextStatus::success, "font bank admission");
    return bank;
}

inline PreparedTextKey make_key(const PreparedTextService& service, const EncodedFontLease& fonts,
    const std::string_view text, const double size = 16.0, const double scale = 1.0) {
    require(text.size() <= PreparedTextLimits::display_bytes, "test text bound");
    PreparedTextKey key{};
    key.page.revision = {1, 1};
    key.page.serial = 1;
    key.source = {SourceByteOffset(100), SourceByteOffset(100 + text.size())};
    key.page.permitted = key.source;
    key.page.viewport.anchor = key.source.begin;
    key.display_begin = DisplayByteOffset(7);
    key.display_end = DisplayByteOffset(7 + static_cast<std::uint32_t>(text.size()));
    key.layout_serial = 1;
    key.provider_instance = service.instance();
    key.font_set = fonts.identity();
    key.font_generation = fonts.generation();
    key.context_generation = 1;
    key.font.role = FontRole::content;
    key.font.size = size;
    key.scale = scale;
    return key;
}

inline PreparedTextStatus make_input(PreparedTextService& service, const PreparedTextKey& key,
    const std::string_view text, PrepareInput& output, const PreparedParagraphProof proof = {true, true}) {
    const std::array<DocumentMapSpan, 1> mapping{{{
        .source = key.source, .begin = key.display_begin, .end = key.display_end}}};
    const std::array<PreparedSourceEndpoint, 2> endpoints{{
        {key.source.begin, key.display_begin}, {key.source.end, key.display_end}}};
    const std::size_t mapping_count = text.empty() ? 0 : 1;
    const std::size_t endpoint_count = text.empty() ? 1 : 2;
    const std::span<const DocumentMapSpan> map(mapping.data(), mapping_count);
    const std::span<const PreparedSourceEndpoint> edges(endpoints.data(), endpoint_count);
    const PreparedTextStatus status = service.create_input(key, text, map, edges, proof, output);
    return status;
}

inline PreparedTextSessionSnapshot wait_ready(PreparedTextSession& session) {
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
        const PreparedTextSessionSnapshot snapshot = session.inspect_ready();
        if (snapshot.slot == PreparedTextSlot::ready) return snapshot;
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        require(now < deadline, "worker completion timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

inline void prepare(PreparedTextService& service, PreparedTextSession& session, const PreparedTextKey& key,
    const std::string_view text, PreparedTextLayout& output) {
    LayoutAuthority authority{};
    const PreparedTextStatus desired = session.desire(key, authority);
    require(desired == PreparedTextStatus::success, "desire");
    PrepareInput input{};
    const PreparedTextStatus created = make_input(service, key, text, input);
    require(created == PreparedTextStatus::success, "input");
    const PreparedTextStatus submitted = session.submit(authority, input);
    require(submitted == PreparedTextStatus::success && input.empty(), "submit transfers owner");
    const PreparedTextSessionSnapshot ready = wait_ready(session);
    require(ready.completion == PreparedTextStatus::success, "worker prepare");
    const PreparedTextStatus adopted = session.adopt_ready(authority, output);
    require(adopted == PreparedTextStatus::success, "adopt");
}
} // namespace prepared_test
