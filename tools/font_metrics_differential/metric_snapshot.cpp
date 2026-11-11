#include "font_metrics_differential.hpp"

#include <charconv>
#include <cmath>
#include <ostream>
#include <system_error>

// Metric snapshots (owner decision F4-1, brief decision 18): a reference
// face's engine measurement as text, so the gate compares bundled candidates
// with Windows reference faces on hosts that do not have them. The format is
// line based and strict: every record appears in a fixed order, numbers use
// std::to_chars (shortest round trip, locale independent), and anything
// unexpected is rejected before a measurement is returned.

namespace gui_forms::tools::font_metrics {

namespace {

constexpr std::string_view snapshot_schema = "gui_forms.font_metrics_snapshot.v1";

void append_double(std::string& output, double value) {
    std::array<char, 64> digits{};
    const std::to_chars_result written = std::to_chars(digits.data(), digits.data() + digits.size(), value);
    if (written.ec != std::errc{}) throw std::runtime_error("a measurement number cannot be written");
    output.append(digits.data(), written.ptr);
}

void append_integer(std::string& output, std::int64_t value) {
    std::array<char, 24> digits{};
    const std::to_chars_result written = std::to_chars(digits.data(), digits.data() + digits.size(), value);
    output.append(digits.data(), written.ptr);
}

void append_hex(std::string& output, std::uint32_t value) {
    std::array<char, 12> digits{};
    const std::to_chars_result written = std::to_chars(digits.data(), digits.data() + digits.size(), value, 16);
    output.append(digits.data(), written.ptr);
}

// A single-line text field: no control characters, so a line reader can
// take the rest of the line back unchanged.
void append_text_field(std::string& output, std::string_view label, std::string_view value) {
    for (const char character : value) {
        if (static_cast<unsigned char>(character) < 0x20U) throw InputRejected("a snapshot text field has a control character");
    }
    output.append(label);
    output.push_back(' ');
    output.append(value);
    output.push_back('\n');
}

// Reads a snapshot one line at a time, splitting fields on single spaces.
class SnapshotReader final {
public:
    explicit SnapshotReader(std::string_view text) : remaining_(text) {}

    // The next line that is not a comment, without its newline.
    [[nodiscard]] std::string_view line() & {
        while (!remaining_.empty()) {
            const std::size_t end = remaining_.find('\n');
            if (end == std::string_view::npos) throw InputRejected("the snapshot does not end with a newline");
            std::string_view current = remaining_.substr(0U, end);
            remaining_.remove_prefix(end + 1U);
            if (!current.empty() && current.back() == '\r') current.remove_suffix(1U);
            if (current.empty() || current.front() == '#') continue;
            return current;
        }
        throw InputRejected("the snapshot ends early");
    }
    std::string_view line() && = delete;

    // The rest of a line after its label and one space.
    [[nodiscard]] std::string_view labelled(std::string_view label) {
        const std::string_view current = line();
        if (current.size() <= label.size() || current.substr(0U, label.size()) != label ||
            current[label.size()] != ' ') {
            throw InputRejected("the snapshot expected the record " + std::string(label));
        }
        const std::string_view rest = current.substr(label.size() + 1U);
        return rest;
    }

    [[nodiscard]] bool at_end() {
        while (!remaining_.empty()) {
            const std::size_t end = remaining_.find('\n');
            const std::string_view current = remaining_.substr(0U, end == std::string_view::npos ? remaining_.size() : end);
            if (!current.empty() && current != "\r" && current.front() != '#') return false;
            remaining_.remove_prefix(end == std::string_view::npos ? remaining_.size() : end + 1U);
        }
        return true;
    }

private:
    std::string_view remaining_;
};

// Splits fields on single spaces; an empty field is rejected.
std::vector<std::string_view> fields_of(std::string_view text) {
    std::vector<std::string_view> fields{};
    while (!text.empty()) {
        const std::size_t space = text.find(' ');
        const std::string_view field = text.substr(0U, space);
        if (field.empty()) throw InputRejected("a snapshot record has an empty field");
        fields.push_back(field);
        if (space == std::string_view::npos) break;
        text.remove_prefix(space + 1U);
        if (text.empty()) throw InputRejected("a snapshot record ends with a space");
    }
    return fields;
}

template <typename Integer>
Integer integer_field(std::string_view field, int base = 10) {
    Integer value{};
    const std::from_chars_result read = std::from_chars(field.data(), field.data() + field.size(), value, base);
    if (read.ec != std::errc{} || read.ptr != field.data() + field.size()) {
        throw InputRejected("a snapshot integer is malformed: " + std::string(field));
    }
    return value;
}

double double_field(std::string_view field) {
    double value{};
    const std::from_chars_result read = std::from_chars(field.data(), field.data() + field.size(), value);
    if (read.ec != std::errc{} || read.ptr != field.data() + field.size() || !std::isfinite(value)) {
        throw InputRejected("a snapshot number is malformed: " + std::string(field));
    }
    return value;
}

void require_count(const std::vector<std::string_view>& fields, std::size_t count, std::string_view record) {
    if (fields.size() != count) throw InputRejected("the snapshot record " + std::string(record) + " has the wrong field count");
}

FaceMeasurement read_measurement(SnapshotReader& reader) {
    FaceMeasurement measurement{};
    if (reader.line() != "face") throw InputRejected("the snapshot expected its face record");
    FaceFacts& facts = measurement.facts;
    facts.file_name = std::string(reader.labelled("file"));
    facts.byte_count = integer_field<std::uint64_t>(reader.labelled("bytes"));
    facts.sha256_hex = std::string(reader.labelled("sha256"));
    if (facts.sha256_hex.size() != 64U) throw InputRejected("the snapshot SHA-256 is malformed");
    facts.family = std::string(reader.labelled("family"));
    facts.version = std::string(reader.labelled("version"));
    const std::vector<std::string_view> tables = fields_of(reader.labelled("tables"));
    require_count(tables, 12U, "tables");
    facts.units_per_em = integer_field<std::uint16_t>(tables[0]);
    facts.weight_class = integer_field<std::uint16_t>(tables[1]);
    facts.italic = integer_field<std::uint8_t>(tables[2]) != 0U;
    facts.hhea_ascender = integer_field<std::int16_t>(tables[3]);
    facts.hhea_descender = integer_field<std::int16_t>(tables[4]);
    facts.hhea_line_gap = integer_field<std::int16_t>(tables[5]);
    facts.typo_ascender = integer_field<std::int16_t>(tables[6]);
    facts.typo_descender = integer_field<std::int16_t>(tables[7]);
    facts.typo_line_gap = integer_field<std::int16_t>(tables[8]);
    facts.win_ascent = integer_field<std::uint16_t>(tables[9]);
    facts.win_descent = integer_field<std::uint16_t>(tables[10]);
    facts.glyph_count = integer_field<std::uint32_t>(tables[11]);

    const std::size_t advance_count = integer_field<std::size_t>(reader.labelled("advances"));
    if (advance_count > 0x10000U) throw InputRejected("the snapshot lists too many advances");
    measurement.advances.reserve(advance_count);
    for (std::size_t index = 0U; index < advance_count; ++index) {
        const std::vector<std::string_view> fields = fields_of(reader.labelled("advance"));
        require_count(fields, 3U, "advance");
        CodepointAdvance advance{};
        advance.codepoint = integer_field<std::uint32_t>(fields[0], 16);
        advance.mapped = integer_field<std::uint8_t>(fields[1]) != 0U;
        advance.advance_units = integer_field<std::int32_t>(fields[2]);
        measurement.advances.push_back(advance);
    }

    const std::size_t size_count = integer_field<std::size_t>(reader.labelled("sizes"));
    if (size_count != measured_sizes_px.size()) throw InputRejected("the snapshot does not cover the size plan");
    measurement.sizes.reserve(size_count);
    for (std::size_t index = 0U; index < size_count; ++index) {
        const std::vector<std::string_view> fields = fields_of(reader.labelled("size"));
        require_count(fields, 5U, "size");
        EngineSizeMeasurement size{};
        size.size_px = double_field(fields[0]);
        size.ascent_px = double_field(fields[1]);
        size.descent_px = double_field(fields[2]);
        size.line_gap_px = double_field(fields[3]);
        size.missing_clusters = integer_field<std::size_t>(fields[4]);
        const std::vector<std::string_view> widths = fields_of(reader.labelled("widths"));
        if (widths.empty()) throw InputRejected("the snapshot widths record is empty");
        const std::size_t width_count = integer_field<std::size_t>(widths[0]);
        require_count(widths, width_count + 1U, "widths");
        if (width_count > maximum_corpus_entries) throw InputRejected("the snapshot lists too many widths");
        size.string_widths_px.reserve(width_count);
        for (std::size_t width = 1U; width < widths.size(); ++width) size.string_widths_px.push_back(double_field(widths[width]));
        const std::size_t set_count = integer_field<std::size_t>(reader.labelled("break_sets"));
        if (set_count > maximum_corpus_entries * wrap_widths_em.size()) throw InputRejected("the snapshot lists too many break sets");
        size.line_starts.reserve(set_count);
        for (std::size_t set = 0U; set < set_count; ++set) {
            const std::string_view rest = reader.labelled("starts");
            const std::vector<std::string_view> starts = fields_of(rest);
            const std::size_t start_count = integer_field<std::size_t>(starts[0]);
            require_count(starts, start_count + 1U, "starts");
            if (start_count > maximum_paragraph_words) throw InputRejected("the snapshot lists too many line starts");
            std::vector<std::uint32_t> line{};
            line.reserve(start_count);
            for (std::size_t start = 1U; start < starts.size(); ++start) line.push_back(integer_field<std::uint32_t>(starts[start]));
            size.line_starts.push_back(std::move(line));
        }
        measurement.sizes.push_back(std::move(size));
    }
    if (reader.line() != "end") throw InputRejected("the snapshot expected its end record");
    measurement.gdi.available = false;
    measurement.gdi.unavailable_reason = "measured from a metric snapshot";
    return measurement;
}

} // namespace

std::string serialize_measurement(const FaceMeasurement& measurement) {
    std::string output{};
    output.append("face\n");
    const FaceFacts& facts = measurement.facts;
    append_text_field(output, "file", facts.file_name);
    output.append("bytes ");
    append_integer(output, static_cast<std::int64_t>(facts.byte_count));
    output.push_back('\n');
    append_text_field(output, "sha256", facts.sha256_hex);
    append_text_field(output, "family", facts.family);
    append_text_field(output, "version", facts.version);
    const std::array<std::int64_t, 12> tables{
        facts.units_per_em, facts.weight_class, facts.italic ? 1 : 0,
        facts.hhea_ascender, facts.hhea_descender, facts.hhea_line_gap,
        facts.typo_ascender, facts.typo_descender, facts.typo_line_gap,
        facts.win_ascent, facts.win_descent, facts.glyph_count,
    };
    output.append("tables");
    for (const std::int64_t value : tables) {
        output.push_back(' ');
        append_integer(output, value);
    }
    output.push_back('\n');
    output.append("advances ");
    append_integer(output, static_cast<std::int64_t>(measurement.advances.size()));
    output.push_back('\n');
    for (const CodepointAdvance& advance : measurement.advances) {
        output.append("advance ");
        append_hex(output, static_cast<std::uint32_t>(advance.codepoint));
        output.append(advance.mapped ? " 1 " : " 0 ");
        append_integer(output, advance.advance_units);
        output.push_back('\n');
    }
    output.append("sizes ");
    append_integer(output, static_cast<std::int64_t>(measurement.sizes.size()));
    output.push_back('\n');
    for (const EngineSizeMeasurement& size : measurement.sizes) {
        output.append("size ");
        append_double(output, size.size_px);
        output.push_back(' ');
        append_double(output, size.ascent_px);
        output.push_back(' ');
        append_double(output, size.descent_px);
        output.push_back(' ');
        append_double(output, size.line_gap_px);
        output.push_back(' ');
        append_integer(output, static_cast<std::int64_t>(size.missing_clusters));
        output.append("\nwidths ");
        append_integer(output, static_cast<std::int64_t>(size.string_widths_px.size()));
        for (const double width : size.string_widths_px) {
            output.push_back(' ');
            append_double(output, width);
        }
        output.append("\nbreak_sets ");
        append_integer(output, static_cast<std::int64_t>(size.line_starts.size()));
        output.push_back('\n');
        for (const std::vector<std::uint32_t>& line : size.line_starts) {
            output.append("starts ");
            append_integer(output, static_cast<std::int64_t>(line.size()));
            for (const std::uint32_t start : line) {
                output.push_back(' ');
                append_integer(output, start);
            }
            output.push_back('\n');
        }
    }
    output.append("end\n");
    return output;
}

std::string corpus_digest(const Corpus& corpus) {
    std::string text("[strings]\n");
    for (const std::string& entry : corpus.strings) {
        text.append(entry);
        text.push_back('\n');
    }
    text.append("[paragraphs]\n");
    for (const std::string& entry : corpus.paragraphs) {
        text.append(entry);
        text.push_back('\n');
    }
    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(text.data()), text.size());
    const std::string digest = sha256_hex(bytes);
    return digest;
}

std::string measurement_digest(const FaceMeasurement& measurement) {
    const std::string text = serialize_measurement(measurement);
    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(text.data()), text.size());
    const std::string digest = sha256_hex(bytes);
    return digest;
}

void write_snapshot(std::ostream& output, const MetricSnapshot& snapshot) {
    std::string text{};
    text.append("# GUI.Forms font metric snapshot (stage F; brief decision 18).\n");
    text.append("# Measurements only: this file holds no outlines, tables or other font data.\n");
    text.append("# tools/font_metrics_differential measured the reference file named below,\n");
    text.append("# identified by its SHA-256, with the bundled FreeType and HarfBuzz engine\n");
    text.append("# (ADR-004): advances in font units, then line metrics, corpus string widths\n");
    text.append("# and line starts per size. The gate measures the bundled candidate on every\n");
    text.append("# host; its measurement digest below must be reproduced exactly.\n");
    append_text_field(text, "schema", snapshot_schema);
    append_text_field(text, "request", snapshot.request_style + " " + snapshot.request_family);
    append_text_field(text, "corpus", snapshot.corpus_digest + " " + snapshot.corpus_name);
    append_text_field(text, "candidate", snapshot.candidate_digest + " " + snapshot.candidate_file);
    text.append(serialize_measurement(snapshot.reference));
    output << text;
}

MetricSnapshot parse_snapshot(std::string_view text) {
    if (text.size() > maximum_corpus_bytes * 4U) throw InputRejected("the snapshot is too large");
    SnapshotReader reader(text);
    MetricSnapshot snapshot{};
    if (reader.labelled("schema") != snapshot_schema) throw InputRejected("the snapshot schema is not v1");
    const std::string_view request = reader.labelled("request");
    const std::size_t request_space = request.find(' ');
    if (request_space == std::string_view::npos || request_space + 1U >= request.size()) {
        throw InputRejected("the snapshot request is malformed");
    }
    snapshot.request_style = std::string(request.substr(0U, request_space));
    snapshot.request_family = std::string(request.substr(request_space + 1U));
    const std::string_view corpus = reader.labelled("corpus");
    if (corpus.size() < 66U || corpus[64] != ' ') throw InputRejected("the snapshot corpus record is malformed");
    snapshot.corpus_digest = std::string(corpus.substr(0U, 64U));
    snapshot.corpus_name = std::string(corpus.substr(65U));
    const std::string_view candidate = reader.labelled("candidate");
    if (candidate.size() < 66U || candidate[64] != ' ') throw InputRejected("the snapshot candidate record is malformed");
    snapshot.candidate_digest = std::string(candidate.substr(0U, 64U));
    snapshot.candidate_file = std::string(candidate.substr(65U));
    snapshot.reference = read_measurement(reader);
    if (!reader.at_end()) throw InputRejected("the snapshot has records after its end");
    return snapshot;
}

} // namespace gui_forms::tools::font_metrics
