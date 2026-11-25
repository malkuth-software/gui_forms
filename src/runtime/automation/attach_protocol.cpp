// The attach protocol's lines (attach_endpoint.hpp): the bridge's hello and
// the program's answer, written and read before MCP starts on the
// connection. Compiled into the automation and into the gui-forms-attach
// bridge.

#include "attach_endpoint.hpp"

#include <stxlib/result.hpp>
#include <stxlib/types.hpp>
#include <stxlib/view.hpp>

#include <cstddef>
#include <string>
#include <string_view>

namespace gui_forms::automation {

namespace {

constexpr std::string_view hello_prefix = "GUI.Forms attach 1";
constexpr std::string_view accepted_word = "ok";
constexpr std::string_view refused_word = "refused";
constexpr std::size_t maximum_name = 256U;

// `text` with control bytes as spaces, cut to `limit` bytes at the start of
// a UTF-8 sequence.
std::string clean_field(std::string_view text, std::size_t limit) {
    std::string cleaned{};
    for (const char character : text) {
        const unsigned char byte = static_cast<unsigned char>(character);
        cleaned += byte < 0x20U || byte == 0x7fU ? ' ' : character;
    }
    if (cleaned.size() > limit) {
        std::size_t cut = limit;
        // Back over continuation bytes to a sequence's first byte.
        while (cut > 0U && (static_cast<unsigned char>(cleaned[cut]) & 0xc0U) == 0x80U) --cut;
        cleaned.resize(cut);
    }
    return cleaned;
}

// The field of `line` after `start` up to the next tab or the end; `start`
// moves past the tab.
std::string_view next_field(std::string_view line, std::size_t& start) {
    const std::size_t tab = line.find('\t', start);
    const std::size_t end = tab == std::string_view::npos ? line.size() : tab;
    const std::string_view field = line.substr(start, end - start);
    start = tab == std::string_view::npos ? line.size() : tab + 1U;
    return field;
}

} // namespace

std::string attach_hello_line(const AttachHello& hello) {
    std::string line(hello_prefix);
    line += '\t';
    line += clean_field(hello.client_name, maximum_name);
    line += '\t';
    line += clean_field(hello.client_version, maximum_name);
    line += '\n';
    return line;
}

bool parse_attach_hello(std::string_view line, AttachHello& hello) {
    std::size_t start = 0U;
    if (next_field(line, start) != hello_prefix) return false;
    hello.client_name = clean_field(next_field(line, start), maximum_name);
    hello.client_version = clean_field(next_field(line, start), maximum_name);
    return true;
}

std::string attach_answer_line(const AttachAnswer& answer) {
    std::string line(answer.accepted ? accepted_word : refused_word);
    if (!answer.accepted) {
        line += '\t';
        line += clean_field(answer.reason, maximum_attach_line / 2U);
    }
    line += '\n';
    return line;
}

bool parse_attach_answer(std::string_view line, AttachAnswer& answer) {
    std::size_t start = 0U;
    const std::string_view word = next_field(line, start);
    if (word == accepted_word) {
        answer.accepted = true;
        answer.reason.clear();
        return true;
    }
    if (word != refused_word) return false;
    answer.accepted = false;
    answer.reason = std::string(line.substr(start));
    return true;
}

bool read_attach_line(stxlib::PipeReader& input, std::string& line) {
    line.clear();
    while (line.size() < maximum_attach_line) {
        stxlib::Byte byte = 0U;
        const stxlib::OperationResult<stxlib::PipeRead> read = input.read(stxlib::MutableView<stxlib::Byte>(&byte, 1U));
        if (read.failed()) return false;
        if ((*read.value()).is_end()) return false;
        if (byte == static_cast<stxlib::Byte>('\n')) return true;
        line += static_cast<char>(byte);
    }
    return false;
}

bool write_attach_line(stxlib::PipeWriter& output, std::string_view line) {
    const stxlib::View<stxlib::Byte> bytes(reinterpret_cast<const stxlib::Byte*>(line.data()), line.size());
    const stxlib::OperationStatus written = output.write_all(bytes);
    return written.succeeded();
}

} // namespace gui_forms::automation
