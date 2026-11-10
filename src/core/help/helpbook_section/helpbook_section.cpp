#include "gui_forms/help/helpbook_section/helpbook_section.hpp"

#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/text/types/text_types.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <utility>

namespace gui_forms {
namespace {

// --- Compilation ------------------------------------------------------------------

[[nodiscard]] bool word_byte(unsigned char byte) noexcept {
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') ||
           byte >= 0x80U;
}

[[nodiscard]] bool ends_with_png(std::string_view name) noexcept {
    if (name.size() < 4U) return false;
    std::string folded;
    fold_help_text(name.substr(name.size() - 4U), folded);
    return folded == ".png";
}

[[nodiscard]] bool sorted_contains(const std::vector<std::string>& sorted, std::string_view value) noexcept {
    const std::vector<std::string>::const_iterator found =
        std::lower_bound(sorted.begin(), sorted.end(), value, std::less<>());
    return found != sorted.end() && *found == value;
}

// One occurrence of a search word, before the occurrences are grouped into
// terms: the word (a view into the section's folded text or a folded title
// kept beside it), its record and whether it is in the title.
struct WordOccurrence final {
    std::string_view word{};
    std::uint32_t record{};
    bool in_title{};
};

struct OccurrenceOrder final {
    bool operator()(const WordOccurrence& left, const WordOccurrence& right) const noexcept {
        if (left.word != right.word) return left.word < right.word;
        if (left.record != right.record) return left.record < right.record;
        return left.in_title && !right.in_title;
    }
};

struct KeywordOrder final {
    bool operator()(const HelpKeywordEntry& left, const HelpKeywordEntry& right) const noexcept {
        if (left.folded != right.folded) return left.folded < right.folded;
        if (left.subkeyword != right.subkeyword) return left.subkeyword < right.subkeyword;
        return left.record < right.record;
    }
};

// Checks one body: parses it, classifies its links and checks its images.
// Appends a rejection for each fault; fills `links` (record index left 0)
// and `plain` when the body passes. Returns whether it passed.
bool check_body(std::string_view stable_id, std::string_view body, const std::vector<std::string>& known_ids,
                const std::vector<std::string>& images, const MarkdownLimits& limits, MarkdownDocument& document,
                std::vector<HelpLinkEntry>& links, std::string& plain,
                std::vector<HelpEntryRejection>& rejections) {
    links.clear();
    plain.clear();
    const MarkdownParseResult parsed = parse_markdown(body, document, limits);
    if (!parsed.succeeded()) {
        HelpEntryRejection rejection{};
        rejection.stable_id = std::string(stable_id);
        rejection.field = HelpField::help;
        rejection.reason = HelpEntryRefusal::markdown_fault;
        rejections.push_back(std::move(rejection));
        return false;
    }
    bool passed = true;
    for (const MarkdownLink& link : document.links) {
        const HelpLinkTarget target = classify_help_link(link.target, link.title);
        if ((target.kind == HelpLinkKind::topic || target.kind == HelpLinkKind::popup) &&
            !sorted_contains(known_ids, target.target)) {
            HelpEntryRejection rejection{};
            rejection.stable_id = std::string(stable_id);
            rejection.field = HelpField::help;
            rejection.reason = HelpEntryRefusal::broken_link;
            rejection.detail = target.target;
            rejections.push_back(std::move(rejection));
            passed = false;
            continue;
        }
        HelpLinkEntry entry{};
        entry.kind = target.kind;
        entry.target = target.target;
        entry.fragment = target.fragment;
        links.push_back(std::move(entry));
    }
    for (const MarkdownImage& image : document.images) {
        if (!ends_with_png(image.source) || !sorted_contains(images, image.source)) {
            HelpEntryRejection rejection{};
            rejection.stable_id = std::string(stable_id);
            rejection.field = HelpField::help;
            rejection.reason = HelpEntryRefusal::missing_image;
            rejection.detail = image.source;
            rejections.push_back(std::move(rejection));
            passed = false;
            continue;
        }
        HelpLinkEntry entry{};
        entry.kind = HelpLinkKind::image;
        entry.target = image.source;
        links.push_back(std::move(entry));
    }
    if (passed) markdown_plain_text(document, plain);
    return passed;
}

// --- Serialization ----------------------------------------------------------------

constexpr std::uint32_t tag_header = 1U;
constexpr std::uint32_t tag_records = 2U;
constexpr std::uint32_t tag_contents = 3U;
constexpr std::uint32_t tag_keywords = 4U;
constexpr std::uint32_t tag_terms = 5U;
constexpr std::uint32_t tag_links = 6U;
constexpr std::uint32_t sub_section_version = 1U;
constexpr std::array<char, 4> section_magic{{'G', 'F', 'H', 'B'}};

// Appends little-endian fields to one byte vector.
struct SectionWriter final {
    std::vector<std::byte>* bytes{};

    void unsigned_value(std::uint64_t value, std::size_t width) {
        for (std::size_t index = 0U; index < width; ++index) {
            (*bytes).push_back(static_cast<std::byte>((value >> (8U * index)) & 0xFFU));
        }
    }
    void u8(std::uint8_t value) { unsigned_value(value, 1U); }
    void u16(std::uint16_t value) { unsigned_value(value, 2U); }
    void u32(std::uint32_t value) { unsigned_value(value, 4U); }
    void u64(std::uint64_t value) { unsigned_value(value, 8U); }
    void text(std::string_view value) {
        u32(static_cast<std::uint32_t>(value.size()));
        const std::byte* first = reinterpret_cast<const std::byte*>(value.data());
        (*bytes).insert((*bytes).end(), first, first + value.size());
    }
    // Starts a sub-section and returns where its length goes.
    [[nodiscard]] std::size_t open(std::uint32_t tag) {
        u32(tag);
        u32(sub_section_version);
        const std::size_t length_at = (*bytes).size();
        u64(0U);
        return length_at;
    }
    void close(std::size_t length_at) {
        const std::uint64_t length = (*bytes).size() - (length_at + 8U);
        for (std::size_t index = 0U; index < 8U; ++index) {
            (*bytes)[length_at + index] = static_cast<std::byte>((length >> (8U * index)) & 0xFFU);
        }
    }
};

// Reads little-endian fields from a bounded range; every read checks the
// bound first and fails without reading past it.
struct SectionReader final {
    std::span<const std::byte> bytes{};
    std::size_t offset{};

    [[nodiscard]] bool unsigned_value(std::size_t width, std::uint64_t& value) noexcept {
        if (bytes.size() - offset < width) return false;
        value = 0U;
        for (std::size_t index = 0U; index < width; ++index) {
            value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + index])) << (8U * index);
        }
        offset += width;
        return true;
    }
    [[nodiscard]] bool u8(std::uint8_t& value) noexcept {
        std::uint64_t read = 0U;
        if (!unsigned_value(1U, read)) return false;
        value = static_cast<std::uint8_t>(read);
        return true;
    }
    [[nodiscard]] bool u16(std::uint16_t& value) noexcept {
        std::uint64_t read = 0U;
        if (!unsigned_value(2U, read)) return false;
        value = static_cast<std::uint16_t>(read);
        return true;
    }
    [[nodiscard]] bool u32(std::uint32_t& value) noexcept {
        std::uint64_t read = 0U;
        if (!unsigned_value(4U, read)) return false;
        value = static_cast<std::uint32_t>(read);
        return true;
    }
    [[nodiscard]] bool u64(std::uint64_t& value) noexcept { return unsigned_value(8U, value); }
    [[nodiscard]] bool text(std::string& value) {
        std::uint32_t length = 0U;
        if (!u32(length)) return false;
        if (bytes.size() - offset < length) return false;
        value.assign(reinterpret_cast<const char*>(bytes.data() + offset), length);
        offset += length;
        const Utf8ValidationResult validation = validate_utf8(value);
        return validation.valid();
    }
    // A count that the remaining bytes could hold at `minimum_size` bytes an
    // entry and that `limit` allows.
    [[nodiscard]] bool count(std::uint32_t& value, std::size_t minimum_size, std::size_t limit) noexcept {
        if (!u32(value)) return false;
        if (value > limit) return false;
        return static_cast<std::size_t>(value) <= (bytes.size() - offset) / minimum_size;
    }
    [[nodiscard]] bool done() const noexcept { return offset == bytes.size(); }
};

void write_records(SectionWriter& writer, const HelpBookSection& section) {
    const std::size_t length_at = writer.open(tag_records);
    writer.u32(static_cast<std::uint32_t>(section.records.size()));
    for (const HelpBookRecord& record : section.records) {
        writer.text(record.stable_id);
        writer.text(record.title);
        writer.text(record.help_line);
        writer.text(record.help);
        writer.text(record.keywords);
        writer.text(record.folded_text);
        writer.u32(record.contents_node);
        writer.u8(record.fallback ? 1U : 0U);
    }
    writer.close(length_at);
}

void write_contents(SectionWriter& writer, const HelpContentsTree& contents) {
    const std::size_t length_at = writer.open(tag_contents);
    writer.u32(static_cast<std::uint32_t>(contents.nodes.size()));
    for (const HelpContentsNode& node : contents.nodes) {
        writer.text(node.stable_id);
        writer.text(node.title);
        writer.u32(node.parent);
        writer.u32(node.depth);
        writer.u8(node.topic ? 1U : 0U);
    }
    writer.u32(static_cast<std::uint32_t>(contents.lines.size()));
    for (const HelpContentsLine& line : contents.lines) {
        writer.u32(line.page);
        writer.text(line.stable_id);
        writer.text(line.title);
        writer.text(line.help_line);
    }
    writer.close(length_at);
}

[[nodiscard]] bool read_header(SectionReader& reader, HelpBookSection& section) {
    if (!reader.text(section.language)) return false;
    return reader.u64(section.catalog_revision);
}

[[nodiscard]] bool read_records(SectionReader& reader, const HelpBookReadLimits& limits, HelpBookSection& section) {
    std::uint32_t count = 0U;
    // Six empty strings, a node and a flag: 29 bytes at least.
    if (!reader.count(count, 29U, limits.maximum_records)) return false;
    section.records.resize(count);
    for (HelpBookRecord& record : section.records) {
        std::uint8_t fallback = 0U;
        if (!reader.text(record.stable_id) || !reader.text(record.title) || !reader.text(record.help_line) ||
            !reader.text(record.help) || !reader.text(record.keywords) || !reader.text(record.folded_text) ||
            !reader.u32(record.contents_node) || !reader.u8(fallback)) {
            return false;
        }
        if (fallback > 1U || !valid_stable_id(record.stable_id)) return false;
        record.fallback = fallback == 1U;
    }
    for (std::size_t index = 1U; index < section.records.size(); ++index) {
        if (!(section.records[index - 1U].stable_id < section.records[index].stable_id)) return false;
    }
    return true;
}

[[nodiscard]] bool read_contents(SectionReader& reader, const HelpBookReadLimits& limits, HelpContentsTree& contents) {
    std::uint32_t count = 0U;
    if (!reader.count(count, 17U, limits.maximum_records)) return false;
    contents.nodes.resize(count);
    for (std::size_t index = 0U; index < contents.nodes.size(); ++index) {
        HelpContentsNode& node = contents.nodes[index];
        std::uint8_t topic = 0U;
        if (!reader.text(node.stable_id) || !reader.text(node.title) || !reader.u32(node.parent) ||
            !reader.u32(node.depth) || !reader.u8(topic)) {
            return false;
        }
        if (topic > 1U || !valid_stable_id(node.stable_id)) return false;
        node.topic = topic == 1U;
        // Preorder: a parent comes first and is one level up.
        if (node.parent == help_no_parent) {
            if (node.depth != 0U) return false;
        } else if (node.parent >= index || contents.nodes[node.parent].depth + 1U != node.depth) {
            return false;
        }
    }
    std::uint32_t line_count = 0U;
    if (!reader.count(line_count, 16U, limits.maximum_entries)) return false;
    contents.lines.resize(line_count);
    for (HelpContentsLine& line : contents.lines) {
        if (!reader.u32(line.page) || !reader.text(line.stable_id) || !reader.text(line.title) ||
            !reader.text(line.help_line)) {
            return false;
        }
        if (line.page >= contents.nodes.size() || !valid_stable_id(line.stable_id)) return false;
    }
    return true;
}

[[nodiscard]] bool read_keywords(SectionReader& reader, const HelpBookReadLimits& limits, HelpBookSection& section) {
    std::uint32_t count = 0U;
    if (!reader.count(count, 16U, limits.maximum_entries)) return false;
    section.keywords.resize(count);
    for (HelpKeywordEntry& entry : section.keywords) {
        if (!reader.text(entry.folded) || !reader.text(entry.keyword) || !reader.text(entry.subkeyword) ||
            !reader.u32(entry.record)) {
            return false;
        }
        if (entry.record >= section.records.size()) return false;
    }
    const KeywordOrder order{};
    for (std::size_t index = 1U; index < section.keywords.size(); ++index) {
        if (order(section.keywords[index], section.keywords[index - 1U])) return false;
    }
    return true;
}

[[nodiscard]] bool read_terms(SectionReader& reader, const HelpBookReadLimits& limits, HelpBookSection& section) {
    std::uint32_t count = 0U;
    if (!reader.count(count, 12U, limits.maximum_entries)) return false;
    section.terms.resize(count);
    for (HelpSearchTerm& term : section.terms) {
        if (!reader.text(term.term) || !reader.u32(term.first_posting) || !reader.u32(term.posting_count)) return false;
    }
    std::uint32_t posting_count = 0U;
    if (!reader.count(posting_count, 9U, limits.maximum_entries)) return false;
    section.postings.resize(posting_count);
    for (HelpSearchPosting& posting : section.postings) {
        std::uint8_t in_title = 0U;
        if (!reader.u32(posting.record) || !reader.u32(posting.count) || !reader.u8(in_title)) return false;
        if (in_title > 1U || posting.record >= section.records.size()) return false;
        posting.in_title = in_title == 1U;
    }
    for (std::size_t index = 0U; index < section.terms.size(); ++index) {
        const HelpSearchTerm& term = section.terms[index];
        if (term.term.empty()) return false;
        if (index > 0U && !(section.terms[index - 1U].term < term.term)) return false;
        const std::uint64_t end = static_cast<std::uint64_t>(term.first_posting) + term.posting_count;
        if (term.posting_count == 0U || end > section.postings.size()) return false;
        for (std::uint64_t posting = term.first_posting + 1U; posting < end; ++posting) {
            if (!(section.postings[posting - 1U].record < section.postings[posting].record)) return false;
        }
    }
    return true;
}

[[nodiscard]] bool read_links(SectionReader& reader, const HelpBookReadLimits& limits, HelpBookSection& section) {
    std::uint32_t count = 0U;
    if (!reader.count(count, 13U, limits.maximum_entries)) return false;
    section.links.resize(count);
    for (HelpLinkEntry& link : section.links) {
        std::uint8_t kind = 0U;
        if (!reader.u32(link.record) || !reader.u8(kind) || !reader.text(link.target) || !reader.text(link.fragment)) {
            return false;
        }
        if (kind > static_cast<std::uint8_t>(HelpLinkKind::image) || link.record >= section.records.size()) {
            return false;
        }
        link.kind = static_cast<HelpLinkKind>(kind);
    }
    return true;
}

} // namespace

std::string_view help_link_kind_name(HelpLinkKind kind) noexcept {
    switch (kind) {
    case HelpLinkKind::topic:
        return "topic";
    case HelpLinkKind::popup:
        return "popup";
    case HelpLinkKind::external:
        return "external";
    case HelpLinkKind::image:
        return "image";
    }
    return {};
}

std::string_view helpbook_read_error_name(HelpBookReadError error) noexcept {
    switch (error) {
    case HelpBookReadError::none:
        return "none";
    case HelpBookReadError::not_a_section:
        return "not_a_section";
    case HelpBookReadError::later_major_version:
        return "later_major_version";
    case HelpBookReadError::malformed:
        return "malformed";
    case HelpBookReadError::limit_exceeded:
        return "limit_exceeded";
    }
    return {};
}

std::optional<std::size_t> HelpBookSection::find(std::string_view stable_id) const noexcept {
    std::size_t low = 0U;
    std::size_t high = records.size();
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2U;
        if (std::string_view(records[middle].stable_id) < stable_id) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    if (low < records.size() && records[low].stable_id == stable_id) return low;
    return std::nullopt;
}

std::size_t HelpBookSection::keyword_position(std::string_view folded_prefix) const noexcept {
    std::size_t low = 0U;
    std::size_t high = keywords.size();
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2U;
        if (std::string_view(keywords[middle].folded) < folded_prefix) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    return low;
}

void fold_help_text(std::string_view text, std::string& folded) {
    folded.assign(text);
    for (char& character : folded) {
        if (character >= 'A' && character <= 'Z') character = static_cast<char>(character - 'A' + 'a');
    }
}

void split_help_words(std::string_view text, std::vector<std::string_view>& words) {
    words.clear();
    std::size_t index = 0U;
    while (index < text.size()) {
        while (index < text.size() && !word_byte(static_cast<unsigned char>(text[index]))) ++index;
        const std::size_t begin = index;
        while (index < text.size() && word_byte(static_cast<unsigned char>(text[index]))) ++index;
        if (index > begin) words.push_back(text.substr(begin, index - begin));
    }
}

HelpLinkTarget classify_help_link(std::string_view destination, std::string_view title) {
    HelpLinkTarget target{};
    const std::size_t hash = destination.find('#');
    const std::string_view identity = hash == std::string_view::npos ? destination : destination.substr(0U, hash);
    if (valid_stable_id(identity)) {
        target.kind = title == "popup" ? HelpLinkKind::popup : HelpLinkKind::topic;
        target.target = std::string(identity);
        if (hash != std::string_view::npos) target.fragment = std::string(destination.substr(hash + 1U));
        return target;
    }
    target.kind = HelpLinkKind::external;
    target.target = std::string(destination);
    return target;
}

HelpBookCompileResult compile_helpbook_section(const HelpCatalog& catalog, std::span<const HelpSkeletonNode> skeleton,
                                               std::span<const HelpTopicPlacement> topics,
                                               const HelpBookCompileOptions& options) {
    HelpBookCompileResult result{};
    HelpBookSection& section = result.section;
    section.language = catalog.language();
    section.catalog_revision = catalog.revision();

    std::vector<std::string> ids;
    catalog.collect_ids(ids);
    // Every record and every node of the structure may be a link target.
    std::vector<std::string> known_ids = ids;
    for (const HelpSkeletonNode& node : skeleton) known_ids.push_back(node.stable_id);
    std::sort(known_ids.begin(), known_ids.end());
    const std::vector<std::string>::iterator known_end = std::unique(known_ids.begin(), known_ids.end());
    known_ids.erase(known_end, known_ids.end());
    std::vector<std::string> images(options.image_names.begin(), options.image_names.end());
    std::sort(images.begin(), images.end());

    // The records as they will be shown, with rejected fields removed: the
    // contents are generated from these, so a rejected body has no page.
    HelpCatalog effective{};
    HelpRecordLayer& shown = effective.built_in();
    MarkdownDocument document{};
    std::vector<HelpLinkEntry> body_links;
    std::string plain;
    std::vector<HelpKeyword> parsed_keywords;
    section.records.reserve(ids.size());
    for (const std::string& stable_id : ids) {
        const HelpResolvedRecord resolved = catalog.resolve(stable_id);
        HelpBookRecord record{};
        record.stable_id = stable_id;
        record.title = resolved.title.text;
        record.help_line = resolved.help_line.text;
        record.fallback = resolved.title.fallback || resolved.help_line.fallback || resolved.help.fallback ||
                          resolved.keywords.fallback;
        const std::uint32_t record_index = static_cast<std::uint32_t>(section.records.size());
        if (!resolved.help.text.empty()) {
            const bool passed = check_body(stable_id, resolved.help.text, known_ids, images, options.markdown,
                                           document, body_links, plain, result.rejections);
            if (passed) {
                record.help = resolved.help.text;
                fold_help_text(plain, record.folded_text);
                for (HelpLinkEntry& link : body_links) {
                    link.record = record_index;
                    section.links.push_back(std::move(link));
                }
            }
        }
        if (!resolved.keywords.text.empty()) {
            if (parse_help_keywords(resolved.keywords.text, parsed_keywords)) {
                record.keywords = resolved.keywords.text;
            } else {
                HelpEntryRejection rejection{};
                rejection.stable_id = stable_id;
                rejection.field = HelpField::keywords;
                rejection.reason = HelpEntryRefusal::invalid_keywords;
                rejection.detail = resolved.keywords.text;
                result.rejections.push_back(std::move(rejection));
            }
        }
        HelpRecordEntry entry{};
        entry.stable_id = stable_id;
        if (!record.title.empty()) entry.title = record.title;
        if (!record.help_line.empty()) entry.help_line = record.help_line;
        if (!record.help.empty()) entry.help = record.help;
        shown.put(std::move(entry));
        section.records.push_back(std::move(record));
    }

    build_help_contents(skeleton, topics, effective, section.contents);
    for (std::size_t node = 0U; node < section.contents.nodes.size(); ++node) {
        const std::optional<std::size_t> record = section.find(section.contents.nodes[node].stable_id);
        if (record.has_value()) section.records[*record].contents_node = static_cast<std::uint32_t>(node);
    }

    for (std::size_t index = 0U; index < section.records.size(); ++index) {
        const HelpBookRecord& record = section.records[index];
        if (record.keywords.empty()) continue;
        if (!parse_help_keywords(record.keywords, parsed_keywords)) continue;
        for (HelpKeyword& keyword : parsed_keywords) {
            HelpKeywordEntry entry{};
            fold_help_text(keyword.keyword, entry.folded);
            entry.keyword = std::move(keyword.keyword);
            entry.subkeyword = std::move(keyword.subkeyword);
            entry.record = static_cast<std::uint32_t>(index);
            section.keywords.push_back(std::move(entry));
        }
    }
    std::sort(section.keywords.begin(), section.keywords.end(), KeywordOrder{});

    // The term table: every word of every title and body, grouped by word,
    // then by record. The folded titles live here until the terms copy the
    // words out.
    std::vector<std::string> folded_titles(section.records.size());
    std::vector<WordOccurrence> occurrences;
    std::vector<std::string_view> words;
    for (std::size_t index = 0U; index < section.records.size(); ++index) {
        const std::uint32_t record = static_cast<std::uint32_t>(index);
        fold_help_text(section.records[index].title, folded_titles[index]);
        split_help_words(folded_titles[index], words);
        for (const std::string_view word : words) occurrences.push_back(WordOccurrence{word, record, true});
        split_help_words(section.records[index].folded_text, words);
        for (const std::string_view word : words) occurrences.push_back(WordOccurrence{word, record, false});
    }
    std::sort(occurrences.begin(), occurrences.end(), OccurrenceOrder{});
    std::size_t position = 0U;
    while (position < occurrences.size()) {
        HelpSearchTerm term{};
        term.term = std::string(occurrences[position].word);
        term.first_posting = static_cast<std::uint32_t>(section.postings.size());
        while (position < occurrences.size() && occurrences[position].word == term.term) {
            HelpSearchPosting posting{};
            posting.record = occurrences[position].record;
            while (position < occurrences.size() && occurrences[position].word == term.term &&
                   occurrences[position].record == posting.record) {
                ++posting.count;
                posting.in_title = posting.in_title || occurrences[position].in_title;
                ++position;
            }
            section.postings.push_back(posting);
        }
        term.posting_count = static_cast<std::uint32_t>(section.postings.size()) - term.first_posting;
        section.terms.push_back(std::move(term));
    }
    return result;
}

void write_helpbook_section(const HelpBookSection& section, std::vector<std::byte>& bytes) {
    bytes.clear();
    SectionWriter writer{&bytes};
    for (const char character : section_magic) writer.u8(static_cast<std::uint8_t>(character));
    writer.u16(section.format_major);
    writer.u16(section.format_minor);

    const std::size_t header_at = writer.open(tag_header);
    writer.text(section.language);
    writer.u64(section.catalog_revision);
    writer.close(header_at);

    write_records(writer, section);
    write_contents(writer, section.contents);

    const std::size_t keywords_at = writer.open(tag_keywords);
    writer.u32(static_cast<std::uint32_t>(section.keywords.size()));
    for (const HelpKeywordEntry& entry : section.keywords) {
        writer.text(entry.folded);
        writer.text(entry.keyword);
        writer.text(entry.subkeyword);
        writer.u32(entry.record);
    }
    writer.close(keywords_at);

    const std::size_t terms_at = writer.open(tag_terms);
    writer.u32(static_cast<std::uint32_t>(section.terms.size()));
    for (const HelpSearchTerm& term : section.terms) {
        writer.text(term.term);
        writer.u32(term.first_posting);
        writer.u32(term.posting_count);
    }
    writer.u32(static_cast<std::uint32_t>(section.postings.size()));
    for (const HelpSearchPosting& posting : section.postings) {
        writer.u32(posting.record);
        writer.u32(posting.count);
        writer.u8(posting.in_title ? 1U : 0U);
    }
    writer.close(terms_at);

    const std::size_t links_at = writer.open(tag_links);
    writer.u32(static_cast<std::uint32_t>(section.links.size()));
    for (const HelpLinkEntry& link : section.links) {
        writer.u32(link.record);
        writer.u8(static_cast<std::uint8_t>(link.kind));
        writer.text(link.target);
        writer.text(link.fragment);
    }
    writer.close(links_at);
}

HelpBookReadResult read_helpbook_section(std::span<const std::byte> bytes, const HelpBookReadLimits& limits) {
    HelpBookReadResult result{};
    if (bytes.size() > limits.maximum_bytes) {
        result.error = HelpBookReadError::limit_exceeded;
        return result;
    }
    SectionReader reader{bytes, 0U};
    for (const char character : section_magic) {
        std::uint8_t byte = 0U;
        if (!reader.u8(byte) || byte != static_cast<std::uint8_t>(character)) {
            result.error = HelpBookReadError::not_a_section;
            return result;
        }
    }
    HelpBookSection section{};
    if (!reader.u16(section.format_major) || !reader.u16(section.format_minor)) {
        result.error = HelpBookReadError::malformed;
        return result;
    }
    if (section.format_major > helpbook_format_major) {
        result.error = HelpBookReadError::later_major_version;
        return result;
    }
    bool has_header = false;
    bool has_records = false;
    bool has_contents = false;
    bool malformed = false;
    while (!reader.done() && !malformed) {
        std::uint32_t tag = 0U;
        std::uint32_t version = 0U;
        std::uint64_t length = 0U;
        if (!reader.u32(tag) || !reader.u32(version) || !reader.u64(length) ||
            length > bytes.size() - reader.offset) {
            malformed = true;
            break;
        }
        SectionReader part{bytes.subspan(reader.offset, static_cast<std::size_t>(length)), 0U};
        reader.offset += static_cast<std::size_t>(length);
        // A later version of a known sub-section is skipped like an unknown
        // one; the required ones are then missing and the section is refused.
        if (version != sub_section_version) continue;
        bool read = true;
        if (tag == tag_header) {
            read = read_header(part, section);
            has_header = read;
        } else if (tag == tag_records) {
            read = read_records(part, limits, section);
            has_records = read;
        } else if (tag == tag_contents) {
            read = has_records && read_contents(part, limits, section.contents);
            has_contents = read;
        } else if (tag == tag_keywords) {
            read = has_records && read_keywords(part, limits, section);
        } else if (tag == tag_terms) {
            read = has_records && read_terms(part, limits, section);
        } else if (tag == tag_links) {
            read = has_records && read_links(part, limits, section);
        } else {
            continue;
        }
        if (!read || !part.done()) malformed = true;
    }
    if (malformed || !has_header || !has_records || !has_contents) {
        result.error = HelpBookReadError::malformed;
        return result;
    }
    for (const HelpBookRecord& record : section.records) {
        if (record.contents_node != help_no_parent && record.contents_node >= section.contents.nodes.size()) {
            result.error = HelpBookReadError::malformed;
            return result;
        }
    }
    result.section = std::move(section);
    return result;
}

} // namespace gui_forms
