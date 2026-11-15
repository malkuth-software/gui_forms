#include "font_metrics_differential.hpp"

#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

// Stage F's metrics differential over bundled faces only, so CI never needs a
// Microsoft font: a face compared with itself is metric-compatible, a wider
// face is not a near substitute, and every malformed input is rejected before
// measurement. On Windows the GDI measurement (information for stages R and S,
// never gated) runs over a privately added face.
// Arguments: the bundled font directory and the corpus file.

namespace {

namespace font_metrics = gui_forms::tools::font_metrics;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::span<const std::byte> as_bytes(std::string_view text) {
    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(text.data()), text.size());
    return bytes;
}

template <typename Call>
bool rejects(Call call) {
    try {
        call();
    } catch (const font_metrics::InputRejected&) {
        return true;
    }
    return false;
}

struct CorpusCall final {
    std::string_view text{};
    void operator()() const { static_cast<void>(font_metrics::parse_corpus(text)); }
};

struct FaceCall final {
    std::span<const std::byte> bytes{};
    void operator()() const { font_metrics::validate_face_bytes(bytes); }
};

struct CompareCall final {
    const font_metrics::FaceMeasurement* reference{};
    const font_metrics::FaceMeasurement* candidate{};
    void operator()() const {
        static_cast<void>(font_metrics::compare_faces(*reference, *candidate, font_metrics::MappingClass::nearest));
    }
};

struct SnapshotCall final {
    std::string_view text{};
    void operator()() const { static_cast<void>(font_metrics::parse_snapshot(text)); }
};

std::string replaced_once(std::string text, std::string_view from, std::string_view to) {
    const std::size_t at = text.find(from);
    require(at != std::string::npos, "snapshot fixture text present");
    text.replace(at, from.size(), to);
    return text;
}

void check_digests() {
    require(font_metrics::sha256_hex(as_bytes("")) ==
                "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "SHA-256 of the empty message");
    require(font_metrics::sha256_hex(as_bytes("abc")) ==
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "SHA-256 of abc");
    // 56 bytes: the length no longer fits the first padded block.
    require(font_metrics::sha256_hex(as_bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
                "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
            "SHA-256 of the two-block FIPS vector");
}

void check_corpus_validation() {
    const font_metrics::Corpus corpus = font_metrics::parse_corpus("# note\r\n[strings]\r\nOK\r\n[paragraphs]\r\none two\r\n");
    require(corpus.strings.size() == 1U && corpus.strings[0] == "OK", "corpus strings parsed without CR");
    require(corpus.paragraphs.size() == 1U && corpus.paragraphs[0] == "one two", "corpus paragraphs parsed");
    require(rejects(CorpusCall{"[strings]\nOK\n[paragraphs]\nbad\xC3\n"}), "invalid UTF-8 rejected");
    require(rejects(CorpusCall{"[strings]\nOK\n[paragraphs]\ntwo  spaces\n"}), "doubled space rejected");
    require(rejects(CorpusCall{"OK\n[strings]\nOK\n[paragraphs]\nwords\n"}), "text before a section rejected");
    require(rejects(CorpusCall{"[strings]\nOK\n"}), "corpus without paragraphs rejected");
    require(rejects(CorpusCall{""}), "empty corpus rejected");
}

// Snapshots (brief decision 18) read back as the measurement they were
// written from, and malformed snapshots are rejected whole.
// `text` with the rest of the first line that begins with `prefix` (which
// starts with a newline) removed, keeping the prefix itself.
std::string emptied_record(std::string text, std::string_view prefix) {
    const std::size_t at = text.find(prefix);
    require(at != std::string::npos, "snapshot fixture record present");
    const std::size_t from = at + prefix.size();
    const std::size_t line_end = text.find('\n', from);
    require(line_end != std::string::npos, "snapshot fixture record ends");
    text.erase(from, line_end - from);
    return text;
}

void check_snapshots(const font_metrics::Corpus& corpus, const font_metrics::FaceMeasurement& carlito) {
    const font_metrics::Corpus crlf = font_metrics::parse_corpus("[strings]\r\nOK\r\n[paragraphs]\r\none two\r\n");
    const font_metrics::Corpus lf = font_metrics::parse_corpus("[strings]\nOK\n[paragraphs]\none two\n");
    require(font_metrics::corpus_digest(crlf) == font_metrics::corpus_digest(lf), "corpus digest ignores line ends");
    require(font_metrics::corpus_digest(lf) != font_metrics::corpus_digest(corpus), "corpus digest names content");

    font_metrics::MetricSnapshot snapshot{};
    snapshot.request_style = "bold_italic";
    snapshot.request_family = "Times New Roman";
    snapshot.corpus_name = "forms_latin_v1.txt";
    snapshot.corpus_digest = font_metrics::corpus_digest(corpus);
    snapshot.candidate_file = carlito.facts.file_name;
    snapshot.candidate_digest = font_metrics::measurement_digest(carlito);
    snapshot.reference = carlito;
    std::ostringstream written{};
    font_metrics::write_snapshot(written, snapshot);
    const std::string text = written.str();
    const font_metrics::MetricSnapshot read = font_metrics::parse_snapshot(text);
    require(read.request_style == "bold_italic" && read.request_family == "Times New Roman", "snapshot request");
    require(read.corpus_digest == snapshot.corpus_digest && read.candidate_file == "Carlito-Regular.ttf",
            "snapshot corpus and candidate");
    require(font_metrics::measurement_digest(read.reference) == snapshot.candidate_digest,
            "a snapshot reads back as its measurement");
    require(font_metrics::serialize_measurement(read.reference) == font_metrics::serialize_measurement(carlito),
            "a snapshot's records round trip");
    const std::string crlf_text = replaced_once(text, "\nface\n", "\r\nface\r\n");
    require(!rejects(SnapshotCall{crlf_text}), "a CRLF checkout of a snapshot reads");

    require(rejects(SnapshotCall{text.substr(0U, text.size() / 2U)}), "a truncated snapshot is rejected");
    require(rejects(SnapshotCall{text.substr(0U, text.size() - 1U)}), "a snapshot without its last newline is rejected");
    require(rejects(SnapshotCall{text + "end\n"}), "records after the end are rejected");
    require(rejects(SnapshotCall{replaced_once(text, "font_metrics_snapshot.v1", "font_metrics_snapshot.v2")}),
            "another schema is rejected");
    require(rejects(SnapshotCall{replaced_once(text, "\nsizes 6\n", "\nsizes 5\n")}), "a wrong size count is rejected");
    require(rejects(SnapshotCall{replaced_once(text, "\nadvance 20 1 ", "\nadvance 20 1 x")}),
            "a malformed advance is rejected");
    require(rejects(SnapshotCall{replaced_once(text, "\nadvance 20 1 ", "\nadvance 20  1 ")}),
            "an empty field is rejected");
    // B-gf-tests-misc-02-F3: a starts record holding only its label.
    require(rejects(SnapshotCall{emptied_record(text, "\nstarts ")}), "an empty starts record is rejected");
}

void check_face_validation(const std::vector<std::byte>& carlito) {
    const std::span<const std::byte> whole(carlito);
    require(!rejects(FaceCall{whole}), "a bundled face validates");
    require(rejects(FaceCall{whole.first(64U)}), "a truncated table directory is rejected");
    require(rejects(FaceCall{whole.first(carlito.size() / 2U)}), "a table past the end is rejected");
    std::vector<std::byte> wrong_tag = carlito;
    wrong_tag[0] = std::byte{0x74};
    require(rejects(FaceCall{std::span<const std::byte>(wrong_tag)}), "a non-sfnt header is rejected");
}

void check_self_comparison(const font_metrics::FaceMeasurement& carlito) {
    require(carlito.facts.family == "Carlito", "family read from the name table");
    require(carlito.facts.units_per_em == 2048U, "Carlito units per em");
    require(carlito.facts.hhea_ascender == 1950 && carlito.facts.win_ascent == 1950U, "Carlito vertical metrics");
    require(carlito.sizes.size() == font_metrics::measured_sizes_px.size(), "every planned size measured");
    for (const font_metrics::EngineSizeMeasurement& size : carlito.sizes) {
        require(size.ascent_px > 0.0 && size.descent_px > 0.0, "engine line metrics are positive");
        require(size.missing_clusters == 0U, "Carlito covers the corpus");
    }
    const font_metrics::Differential same =
        font_metrics::compare_faces(carlito, carlito, font_metrics::MappingClass::metric_compatible);
    require(same.advances_compared > 300U && same.advances_identical == same.advances_compared,
            "a face's advances equal its own");
    require(same.advances == font_metrics::Criterion::passed, "advances criterion passes");
    require(same.line_breaks == font_metrics::Criterion::passed, "line breaks criterion passes");
    require(same.line_width == font_metrics::Criterion::recorded, "line width is recorded for this class");
    require(same.line_height == font_metrics::Criterion::passed, "line height equals its own");
    require(same.gate_passed, "a face is a metric-compatible substitute for itself");
    for (const font_metrics::SizeDifferential& size : same.sizes) {
        require(size.mean_width_deviation == 0.0 && size.max_abs_width_deviation == 0.0, "zero width deviation");
    }
    std::ostringstream report{};
    font_metrics::write_report(report, "forms_latin_v1.txt", carlito, carlito, same);
    const std::string text = report.str();
    require(text.find("\"schema\":\"gui_forms.font_metrics_differential.v1\"") != std::string::npos,
            "report names its schema");
    require(text.find("\"sha256\":\"" + carlito.facts.sha256_hex + "\"") != std::string::npos,
            "report records the face hash");
}

void check_wider_face(const font_metrics::FaceMeasurement& carlito, const font_metrics::FaceMeasurement& cousine) {
    const font_metrics::Differential wider =
        font_metrics::compare_faces(carlito, cousine, font_metrics::MappingClass::nearest);
    require(wider.line_width == font_metrics::Criterion::failed, "a much wider face is not a near substitute");
    require(!wider.gate_passed, "its gate does not pass");
    require(wider.advances == font_metrics::Criterion::recorded, "advances are recorded for a nearest mapping");
    require(!wider.advance_mismatches.empty(), "mismatched advances are listed");
    for (const font_metrics::SizeDifferential& size : wider.sizes) {
        require(size.mean_width_deviation > font_metrics::nearest_mean_width_tolerance, "monospace runs wider");
    }
    font_metrics::FaceMeasurement short_corpus = cousine;
    short_corpus.sizes[0].string_widths_px.pop_back();
    require(rejects(CompareCall{&carlito, &short_corpus}), "measurements of different corpora are rejected");
}

void check_gdi_measurement(const font_metrics::FaceMeasurement& carlito) {
#if defined(_WIN32)
    require(carlito.gdi.available, "GDI measured the privately added face");
    require(carlito.gdi.selected_face == "Carlito", "GDI selected the requested family");
    require(carlito.gdi.bytes_match_file, "GDI measured the file's own bytes");
    require(carlito.gdi.sizes.size() == font_metrics::measured_sizes_px.size(), "GDI measured every size");
    for (const font_metrics::GdiSizeMeasurement& size : carlito.gdi.sizes) {
        require(size.height > 0 && size.height == size.ascent + size.descent, "GDI height is ascent plus descent");
        require(size.string_widths.size() == carlito.sizes[0].string_widths_px.size(), "GDI measured every string");
    }
#else
    require(!carlito.gdi.available && !carlito.gdi.unavailable_reason.empty(), "no GDI off Windows, with a reason");
#endif
}

} // namespace

int main(int count, char** values) {
    try {
        require(count == 3, "usage: font_metrics_differential_tests <font directory> <corpus file>");
        const std::filesystem::path fonts = values[1];
        const std::vector<std::byte> corpus_bytes =
            font_metrics::read_bounded_file(values[2], font_metrics::maximum_corpus_bytes);
        const std::string_view corpus_text(reinterpret_cast<const char*>(corpus_bytes.data()), corpus_bytes.size());
        const font_metrics::Corpus corpus = font_metrics::parse_corpus(corpus_text);
        const std::vector<std::byte> carlito_bytes =
            font_metrics::read_bounded_file(fonts / "Carlito-Regular.ttf", font_metrics::maximum_face_bytes);
        const std::vector<std::byte> cousine_bytes =
            font_metrics::read_bounded_file(fonts / "Cousine-Regular.ttf", font_metrics::maximum_face_bytes);
        check_digests();
        check_corpus_validation();
        check_face_validation(carlito_bytes);
        const font_metrics::FaceMeasurement carlito =
            font_metrics::measure_face("Carlito-Regular.ttf", carlito_bytes, corpus, true);
        const font_metrics::FaceMeasurement cousine =
            font_metrics::measure_face("Cousine-Regular.ttf", cousine_bytes, corpus, false);
        check_self_comparison(carlito);
        check_snapshots(corpus, carlito);
        check_wider_face(carlito, cousine);
        check_gdi_measurement(carlito);
        std::cout << "font metrics differential tests passed\n";
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
