#include "gui_forms/text/locale_file/locale_file.hpp"

#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/text/text_store/text_store.hpp"
#include "gui_forms/text/types/text_types.hpp"
#include "gui_forms/xml/xml_reader/xml_reader.hpp"
#include "gui_forms/xml/xml_writer/xml_writer.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

// The fields written as `<control>` attributes, in the order the writer
// puts them. `help` and `keywords` belong to the `<help>` child.
constexpr std::array<LocaleField, 9U> attribute_fields{
    LocaleField::text,         LocaleField::tool_tip,        LocaleField::access_key,
    LocaleField::title,        LocaleField::placeholder,     LocaleField::header,
    LocaleField::accessible_name, LocaleField::accessible_description, LocaleField::help_line,
};

[[nodiscard]] std::size_t field_index(LocaleField field) noexcept {
    const std::size_t index = static_cast<std::size_t>(field);
    return index;
}

[[nodiscard]] std::string_view trim_spaces(std::string_view text) noexcept {
    std::size_t begin = 0U;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
    while (end > begin && (text[end - 1U] == ' ' || text[end - 1U] == '\t')) --end;
    return text.substr(begin, end - begin);
}

// Whether `segment` is one stable-ID segment (no '.').
[[nodiscard]] bool valid_segment(std::string_view segment) noexcept {
    return segment.find('.') == std::string_view::npos && valid_stable_id(segment);
}

// A C0 control other than tab and line feed, or DEL, anywhere in `text`.
[[nodiscard]] bool holds_control_character(std::string_view text) noexcept {
    for (const char character : text) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte == 0x09U || byte == 0x0AU) continue;
        if (byte < 0x20U || byte == 0x7FU) return true;
    }
    return false;
}

// Whether every embedding or override (LRE, RLE, LRO, RLO) is closed by a
// PDF, and every isolate (LRI, RLI, FSI) by a PDI, within its line (a line
// feed ends a paragraph, which ends them all). `text` is valid UTF-8.
[[nodiscard]] bool bidi_balanced(std::string_view text) noexcept {
    std::size_t embeddings = 0U;
    std::size_t isolates = 0U;
    std::size_t offset = 0U;
    while (offset < text.size()) {
        const Utf8ScalarDecode decoded = decode_utf8_scalar(text, Utf8Offset(offset));
        offset += decoded.length;
        const char32_t scalar = decoded.scalar;
        if (scalar == U'\n') {
            if (embeddings != 0U || isolates != 0U) return false;
        } else if (scalar == U'‪' || scalar == U'‫' || scalar == U'‭' || scalar == U'‮') {
            ++embeddings;
        } else if (scalar == U'‬') {
            if (embeddings == 0U) return false;
            --embeddings;
        } else if (scalar == U'⁦' || scalar == U'⁧' || scalar == U'⁨') {
            ++isolates;
        } else if (scalar == U'⁩') {
            if (isolates == 0U) return false;
            --isolates;
        }
    }
    return embeddings == 0U && isolates == 0U;
}

// The keyword rule (help system study, section 4): ';' separates keywords,
// ',' a keyword from its one subkeyword; no part is empty. An empty or
// all-space list is valid.
[[nodiscard]] bool keyword_list_valid(std::string_view field) noexcept {
    if (trim_spaces(field).empty()) return true;
    std::size_t begin = 0U;
    while (begin <= field.size()) {
        std::size_t end = field.find(';', begin);
        if (end == std::string_view::npos) end = field.size();
        const std::string_view item = trim_spaces(field.substr(begin, end - begin));
        if (item.empty()) return false;
        const std::size_t comma = item.find(',');
        if (comma != std::string_view::npos) {
            if (item.find(',', comma + 1U) != std::string_view::npos) return false;
            if (trim_spaces(item.substr(0U, comma)).empty() || trim_spaces(item.substr(comma + 1U)).empty()) {
                return false;
            }
        }
        if (end == field.size()) break;
        begin = end + 1U;
    }
    return true;
}

// One grapheme that is not white space.
[[nodiscard]] bool access_key_valid(std::string_view value) {
    if (value.empty() || trim_spaces(value).size() != value.size() || value.find('\n') != std::string_view::npos) {
        return false;
    }
    const TextStore store(value);
    const bool one = store.grapheme_count() == GraphemeIndex(1U);
    return one;
}

// Orders stable IDs segment by segment ('.' before every segment
// character), so a record's descendants follow it before its next sibling:
// the order write_locale nests in.
struct SegmentOrder final {
    [[nodiscard]] bool operator()(const LocaleRecord* left, const LocaleRecord* right) const noexcept {
        const std::string_view a((*left).stable_id);
        const std::string_view b((*right).stable_id);
        const std::size_t shared = std::min(a.size(), b.size());
        for (std::size_t index = 0U; index < shared; ++index) {
            if (a[index] == b[index]) continue;
            if (a[index] == '.') return true;
            if (b[index] == '.') return false;
            return static_cast<unsigned char>(a[index]) < static_cast<unsigned char>(b[index]);
        }
        return a.size() < b.size();
    }
};

struct StableIdOrder final {
    [[nodiscard]] bool operator()(const LocaleRecord& left, const LocaleRecord& right) const noexcept {
        return left.stable_id < right.stable_id;
    }
};

// The reader's state for one file: the segments down to the element being
// read, the records found so far (in document order) and the diagnostics.
struct LocaleWalk final {
    const LocaleLimits* limits{};
    std::uint32_t maximum_depth{};
    std::vector<std::string_view> path{};
    std::string joined{};
    std::vector<LocaleRecord> found{};
    std::vector<LocaleRejection>* rejections{};
    std::vector<std::string>* ignored{};
    bool too_many{};
};

void join_path(LocaleWalk& walk) {
    walk.joined.clear();
    for (std::size_t index = 0U; index < walk.path.size(); ++index) {
        if (index > 0U) walk.joined.push_back('.');
        walk.joined.append(walk.path[index]);
    }
}

void reject(LocaleWalk& walk, LocaleField field, LocaleRefusal reason, std::string_view detail) {
    LocaleRejection rejection{};
    rejection.stable_id = walk.joined;
    rejection.field = field;
    rejection.reason = reason;
    rejection.detail = std::string(detail);
    (*walk.rejections).push_back(std::move(rejection));
}

void note_ignored(LocaleWalk& walk, std::string_view name) {
    if ((*walk.ignored).size() >= locale_maximum_ignored_diagnostics) return;
    std::string entry = walk.joined;
    entry.push_back('@');
    entry.append(name);
    (*walk.ignored).push_back(std::move(entry));
}

// Checks one field's value and stores it in `record`, or rejects it.
void accept_field(LocaleWalk& walk, LocaleRecord& record, LocaleField field, std::string value) {
    const std::optional<LocaleRefusal> refusal = check_locale_value(field, value, *walk.limits);
    if (refusal.has_value()) {
        const bool show_value = *refusal == LocaleRefusal::invalid_keywords;
        reject(walk, field, *refusal, show_value ? std::string_view(value) : std::string_view{});
        return;
    }
    locale_record_field(record, field) = std::move(value);
}

// Attributes a `<control>` may carry that are not fields: its segment and
// the skeleton export's metadata.
[[nodiscard]] bool control_metadata(std::string_view name) noexcept {
    return name == "name" || name == "where" || name == "ambiguous";
}

// Child elements of `<control>` the reader leaves for later stages.
[[nodiscard]] bool reserved_control_child(std::string_view name) noexcept {
    return name == "variant" || name == "item" || name == "unaddressable" || name == "unplaced";
}

void read_control(LocaleWalk& walk, const XmlElement& element, std::uint32_t depth);

void read_controls(LocaleWalk& walk, const XmlElement& element, std::uint32_t depth) {
    for (const XmlElement& child : element.children) {
        if (walk.too_many) return;
        if (child.name == "control") {
            read_control(walk, child, depth + 1U);
        } else if (child.name != "help" && !reserved_control_child(child.name)) {
            note_ignored(walk, child.name);
        }
    }
}

void read_control(LocaleWalk& walk, const XmlElement& element, std::uint32_t depth) {
    // The XML reader refused anything deeper before this walk began; the
    // check keeps the bound explicit here too.
    if (depth > walk.maximum_depth) return;
    const XmlAttribute* name = element.find_attribute("name");
    const std::string_view segment = name != nullptr ? std::string_view((*name).value) : std::string_view{};
    if (!valid_segment(segment)) {
        join_path(walk);
        reject(walk, LocaleField::text, LocaleRefusal::invalid_stable_id, segment);
        return;
    }
    walk.path.push_back(segment);
    join_path(walk);
    LocaleRecord record{};
    record.stable_id = walk.joined;
    for (const XmlAttribute& attribute : element.attributes) {
        if (control_metadata(attribute.name)) continue;
        const std::optional<LocaleField> field = find_locale_field(attribute.name);
        if (!field.has_value() || *field == LocaleField::help || *field == LocaleField::keywords) {
            note_ignored(walk, attribute.name);
            continue;
        }
        accept_field(walk, record, *field, attribute.value);
    }
    bool help_seen = false;
    for (const XmlElement& child : element.children) {
        if (child.name != "help") continue;
        if (help_seen) {
            note_ignored(walk, "help");
            continue;
        }
        help_seen = true;
        std::string body;
        normalize_locale_help_body(child.text, body);
        if (!body.empty()) accept_field(walk, record, LocaleField::help, std::move(body));
        for (const XmlAttribute& attribute : child.attributes) {
            if (attribute.name == "keywords") {
                accept_field(walk, record, LocaleField::keywords, attribute.value);
            } else {
                note_ignored(walk, attribute.name);
            }
        }
    }
    if (!locale_record_empty(record)) {
        if (walk.found.size() >= (*walk.limits).maximum_records) {
            walk.too_many = true;
        } else {
            walk.found.push_back(std::move(record));
        }
    }
    read_controls(walk, element, depth);
    walk.path.pop_back();
    join_path(walk);
}

// The element at `index_path` below `root` (each index a child position).
// Found again from the root for every record: a reference kept across an
// append to a parent's children would dangle.
XmlElement& element_at(XmlElement& root, std::span<const std::size_t> index_path) {
    XmlElement* element = &root;
    for (const std::size_t index : index_path) element = &(*element).children[index];
    return *element;
}

void split_segments(std::string_view stable_id, std::vector<std::string_view>& segments) {
    segments.clear();
    std::string_view rest = stable_id;
    while (true) {
        const std::size_t dot = rest.find('.');
        if (dot == std::string_view::npos) {
            segments.push_back(rest);
            return;
        }
        segments.push_back(rest.substr(0U, dot));
        rest.remove_prefix(dot + 1U);
    }
}

} // namespace

void normalize_locale_help_body(std::string_view raw, std::string& body) {
    body.clear();
    // Line ends become LF, so the indentation scan sees one kind.
    std::string unified;
    unified.reserve(raw.size());
    for (std::size_t index = 0U; index < raw.size(); ++index) {
        const char character = raw[index];
        if (character == '\r') {
            unified.push_back('\n');
            if (index + 1U < raw.size() && raw[index + 1U] == '\n') ++index;
            continue;
        }
        unified.push_back(character);
    }
    std::string_view text(unified);
    // The leading blank lines and the trailing white space go.
    while (true) {
        const std::size_t line_end = text.find('\n');
        if (line_end == std::string_view::npos) break;
        if (!trim_spaces(text.substr(0U, line_end)).empty()) break;
        text.remove_prefix(line_end + 1U);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\n')) text.remove_suffix(1U);
    // The indentation every non-blank line shares, in spaces and tabs as
    // written (a tab counts as one column here).
    std::size_t shared = std::string_view::npos;
    std::size_t begin = 0U;
    while (begin <= text.size()) {
        std::size_t end = text.find('\n', begin);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = text.substr(begin, end - begin);
        if (!trim_spaces(line).empty()) {
            std::size_t indent = 0U;
            while (indent < line.size() && (line[indent] == ' ' || line[indent] == '\t')) ++indent;
            shared = std::min(shared, indent);
        }
        if (end == text.size()) break;
        begin = end + 1U;
    }
    if (shared == std::string_view::npos) shared = 0U;
    body.reserve(text.size());
    begin = 0U;
    while (begin <= text.size()) {
        std::size_t end = text.find('\n', begin);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = text.substr(begin, end - begin);
        if (line.size() > shared) body.append(line.substr(shared));
        if (end == text.size()) break;
        body.push_back('\n');
        begin = end + 1U;
    }
}

// --- Fields ---------------------------------------------------------------------

std::string_view locale_field_name(LocaleField field) noexcept {
    switch (field) {
    case LocaleField::text:
        return "text";
    case LocaleField::tool_tip:
        return "tool-tip";
    case LocaleField::access_key:
        return "access-key";
    case LocaleField::title:
        return "title";
    case LocaleField::placeholder:
        return "placeholder";
    case LocaleField::header:
        return "header";
    case LocaleField::accessible_name:
        return "accessible-name";
    case LocaleField::accessible_description:
        return "accessible-description";
    case LocaleField::help_line:
        return "help-line";
    case LocaleField::help:
        return "help";
    case LocaleField::keywords:
        return "keywords";
    }
    return {};
}

std::optional<LocaleField> find_locale_field(std::string_view name) noexcept {
    for (std::size_t index = 0U; index < locale_field_count; ++index) {
        const LocaleField field = static_cast<LocaleField>(index);
        if (locale_field_name(field) == name) return field;
    }
    return std::nullopt;
}

XmlLimits locale_xml_limits() noexcept {
    XmlLimits limits{};
    limits.maximum_input_bytes = 1024U * 1024U;
    limits.maximum_depth = 256U;
    limits.maximum_elements = 65536U;
    limits.maximum_attributes_per_element = 64U;
    limits.maximum_name_bytes = 256U;
    limits.maximum_value_bytes = 64U * 1024U;
    return limits;
}

const std::optional<std::string>& locale_record_field(const LocaleRecord& record, LocaleField field) noexcept {
    return record.fields[field_index(field) % locale_field_count];
}

std::optional<std::string>& locale_record_field(LocaleRecord& record, LocaleField field) noexcept {
    return record.fields[field_index(field) % locale_field_count];
}

bool locale_record_empty(const LocaleRecord& record) noexcept {
    for (const std::optional<std::string>& value : record.fields) {
        if (value.has_value()) return false;
    }
    return true;
}

std::string_view locale_refusal_name(LocaleRefusal refusal) noexcept {
    switch (refusal) {
    case LocaleRefusal::invalid_stable_id:
        return "invalid_stable_id";
    case LocaleRefusal::too_long:
        return "too_long";
    case LocaleRefusal::invalid_text:
        return "invalid_text";
    case LocaleRefusal::control_character:
        return "control_character";
    case LocaleRefusal::unbalanced_bidi:
        return "unbalanced_bidi";
    case LocaleRefusal::invalid_keywords:
        return "invalid_keywords";
    case LocaleRefusal::invalid_access_key:
        return "invalid_access_key";
    case LocaleRefusal::duplicate_record:
        return "duplicate_record";
    }
    return {};
}

std::optional<LocaleRefusal> check_locale_value(LocaleField field, std::string_view value,
                                                const LocaleLimits& limits) {
    if (value.find('\0') != std::string_view::npos) return LocaleRefusal::invalid_text;
    const Utf8ValidationResult validation = validate_utf8(value);
    if (!validation.valid()) return LocaleRefusal::invalid_text;
    if (value.size() > limits.maximum_field_bytes[field_index(field) % locale_field_count]) {
        return LocaleRefusal::too_long;
    }
    if (holds_control_character(value)) return LocaleRefusal::control_character;
    if (!bidi_balanced(value)) return LocaleRefusal::unbalanced_bidi;
    if (field == LocaleField::keywords && !keyword_list_valid(value)) return LocaleRefusal::invalid_keywords;
    if (field == LocaleField::access_key && !access_key_valid(value)) return LocaleRefusal::invalid_access_key;
    return std::nullopt;
}

// --- LocaleLayer ----------------------------------------------------------------

std::size_t LocaleLayer::lower_bound(std::string_view stable_id) const noexcept {
    std::size_t low = 0U;
    std::size_t high = records_.size();
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2U;
        if (std::string_view(records_[middle].stable_id) < stable_id) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    return low;
}

const LocaleRecord* LocaleLayer::find(std::string_view stable_id) const& noexcept {
    const std::size_t position = lower_bound(stable_id);
    if (position < records_.size() && records_[position].stable_id == stable_id) return &records_[position];
    return nullptr;
}

void LocaleLayer::set(std::string_view stable_id, LocaleField field, std::string value) {
    if (!valid_stable_id(stable_id)) throw std::invalid_argument("GUI.Forms text record ID must be a stable ID");
    const std::optional<LocaleRefusal> refusal = check_locale_value(field, value);
    if (refusal.has_value()) {
        throw std::invalid_argument("GUI.Forms text record value refused: " +
                                    std::string(locale_refusal_name(*refusal)));
    }
    const std::size_t position = lower_bound(stable_id);
    if (position < records_.size() && records_[position].stable_id == stable_id) {
        locale_record_field(records_[position], field) = std::move(value);
        return;
    }
    LocaleRecord record{};
    record.stable_id = std::string(stable_id);
    locale_record_field(record, field) = std::move(value);
    records_.insert(records_.begin() + static_cast<std::ptrdiff_t>(position), std::move(record));
}

bool LocaleLayer::reset(std::string_view stable_id, LocaleField field) {
    const std::size_t position = lower_bound(stable_id);
    if (position >= records_.size() || records_[position].stable_id != stable_id) return false;
    LocaleRecord& record = records_[position];
    std::optional<std::string>& value = locale_record_field(record, field);
    if (!value.has_value()) return false;
    value.reset();
    if (locale_record_empty(record)) records_.erase(records_.begin() + static_cast<std::ptrdiff_t>(position));
    return true;
}

void LocaleLayer::put(LocaleRecord record) {
    if (!valid_stable_id(record.stable_id)) {
        throw std::invalid_argument("GUI.Forms text record ID must be a stable ID");
    }
    const std::size_t position = lower_bound(record.stable_id);
    const bool present = position < records_.size() && records_[position].stable_id == record.stable_id;
    if (locale_record_empty(record)) {
        if (present) records_.erase(records_.begin() + static_cast<std::ptrdiff_t>(position));
        return;
    }
    if (present) {
        records_[position] = std::move(record);
        return;
    }
    records_.insert(records_.begin() + static_cast<std::ptrdiff_t>(position), std::move(record));
}

void LocaleLayer::assign_sorted(std::vector<LocaleRecord> records) {
    for (std::size_t index = 0U; index < records.size(); ++index) {
        if (!valid_stable_id(records[index].stable_id)) {
            throw std::invalid_argument("GUI.Forms text record ID must be a stable ID");
        }
        if (index > 0U && !(records[index - 1U].stable_id < records[index].stable_id)) {
            throw std::invalid_argument("GUI.Forms text records must be sorted by stable ID, each once");
        }
    }
    const std::vector<LocaleRecord>::iterator kept =
        std::remove_if(records.begin(), records.end(), &locale_record_empty);
    records.erase(kept, records.end());
    records_ = std::move(records);
}

void LocaleLayer::merge_over(const LocaleLayer& over) {
    if (over.records_.empty()) return;
    std::vector<LocaleRecord> merged;
    merged.reserve(records_.size() + over.records_.size());
    std::size_t mine = 0U;
    std::size_t theirs = 0U;
    while (mine < records_.size() || theirs < over.records_.size()) {
        if (theirs == over.records_.size() ||
            (mine < records_.size() && records_[mine].stable_id < over.records_[theirs].stable_id)) {
            merged.push_back(std::move(records_[mine]));
            ++mine;
            continue;
        }
        const LocaleRecord& incoming = over.records_[theirs];
        if (mine == records_.size() || incoming.stable_id < records_[mine].stable_id) {
            merged.push_back(incoming);
            ++theirs;
            continue;
        }
        LocaleRecord combined = std::move(records_[mine]);
        for (std::size_t index = 0U; index < locale_field_count; ++index) {
            if (incoming.fields[index].has_value()) combined.fields[index] = incoming.fields[index];
        }
        merged.push_back(std::move(combined));
        ++mine;
        ++theirs;
    }
    records_ = std::move(merged);
}

// --- Files ------------------------------------------------------------------------

bool valid_language_tag(std::string_view tag) noexcept {
    if (tag.empty()) return false;
    std::size_t begin = 0U;
    bool first = true;
    while (begin <= tag.size()) {
        std::size_t end = tag.find('-', begin);
        if (end == std::string_view::npos) end = tag.size();
        const std::string_view subtag = tag.substr(begin, end - begin);
        const std::size_t minimum = first ? 2U : 1U;
        if (subtag.size() < minimum || subtag.size() > 8U) return false;
        for (const char character : subtag) {
            const bool letter = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
            const bool digit = character >= '0' && character <= '9';
            if (!letter && !(digit && !first)) return false;
        }
        if (end == tag.size()) break;
        first = false;
        begin = end + 1U;
    }
    return true;
}

std::string_view locale_file_error_name(LocaleFileError error) noexcept {
    switch (error) {
    case LocaleFileError::none:
        return "none";
    case LocaleFileError::xml:
        return "xml";
    case LocaleFileError::wrong_root:
        return "wrong_root";
    case LocaleFileError::no_language:
        return "no_language";
    case LocaleFileError::invalid_language:
        return "invalid_language";
    case LocaleFileError::invalid_direction:
        return "invalid_direction";
    case LocaleFileError::too_many_records:
        return "too_many_records";
    case LocaleFileError::language_mismatch:
        return "language_mismatch";
    }
    return {};
}

LocaleReadResult read_locale(std::string_view utf8, const LocaleLimits& limits, const XmlLimits& xml_limits) {
    LocaleReadResult result{};
    const XmlReadResult document = read_xml(utf8, xml_limits);
    if (!document) {
        result.error = LocaleFileError::xml;
        result.xml_error = document.error;
        result.position = document.position;
        return result;
    }
    const XmlElement& root = document.root;
    if (root.name != "locale") {
        result.error = LocaleFileError::wrong_root;
        return result;
    }
    const XmlAttribute* language = root.find_attribute("xml:lang");
    if (language == nullptr || (*language).value.empty()) {
        result.error = LocaleFileError::no_language;
        return result;
    }
    if (!valid_language_tag((*language).value)) {
        result.error = LocaleFileError::invalid_language;
        return result;
    }
    LocaleFileInfo info{};
    info.language = (*language).value;
    if (const XmlAttribute* name = root.find_attribute("name")) info.name = (*name).value;
    if (const XmlAttribute* version = root.find_attribute("version")) info.version = (*version).value;
    if (const XmlAttribute* direction = root.find_attribute("direction")) {
        if ((*direction).value == "rtl") {
            info.direction = LocaleDirection::right_to_left;
        } else if ((*direction).value != "ltr") {
            result.error = LocaleFileError::invalid_direction;
            return result;
        }
    }
    std::vector<LocaleRejection> rejections;
    std::vector<std::string> ignored;
    LocaleWalk walk{};
    walk.limits = &limits;
    walk.maximum_depth = xml_limits.maximum_depth;
    walk.rejections = &rejections;
    walk.ignored = &ignored;
    for (const XmlAttribute& attribute : root.attributes) {
        const bool known = attribute.name == "xml:lang" || attribute.name == "name" ||
                           attribute.name == "version" || attribute.name == "direction";
        if (!known) note_ignored(walk, attribute.name);
    }
    for (const XmlElement& section : root.children) {
        if (section.name == "controls") {
            read_controls(walk, section, 2U);
        } else if (section.name != "info" && section.name != "conventions" && section.name != "terms") {
            note_ignored(walk, section.name);
        }
        if (walk.too_many) break;
    }
    if (walk.too_many) {
        LocaleReadResult refused{};
        refused.error = LocaleFileError::too_many_records;
        return refused;
    }
    // Sorted with document order kept among equal IDs, so the first of a
    // repeated record stays and the later ones are rejected.
    std::stable_sort(walk.found.begin(), walk.found.end(), StableIdOrder{});
    std::vector<LocaleRecord> unique;
    unique.reserve(walk.found.size());
    for (LocaleRecord& record : walk.found) {
        if (!unique.empty() && unique.back().stable_id == record.stable_id) {
            LocaleRejection rejection{};
            rejection.stable_id = record.stable_id;
            rejection.field = LocaleField::text;
            rejection.reason = LocaleRefusal::duplicate_record;
            rejections.push_back(std::move(rejection));
            continue;
        }
        unique.push_back(std::move(record));
    }
    result.records.assign_sorted(std::move(unique));
    result.info = std::move(info);
    result.rejections = std::move(rejections);
    result.ignored = std::move(ignored);
    return result;
}

XmlWriteResult write_locale(const LocaleLayer& records, const LocaleFileInfo& info, const XmlLimits& xml_limits) {
    XmlElement root{};
    root.name = "locale";
    root.attributes.push_back(XmlAttribute{"xml:lang", info.language});
    if (!info.name.empty()) root.attributes.push_back(XmlAttribute{"name", info.name});
    if (!info.version.empty()) root.attributes.push_back(XmlAttribute{"version", info.version});
    if (info.direction == LocaleDirection::right_to_left) {
        root.attributes.push_back(XmlAttribute{"direction", "rtl"});
    }
    XmlElement controls{};
    controls.name = "controls";
    root.children.push_back(std::move(controls));

    // Segment order puts every record before its descendants and all of
    // them before its next sibling, so each new element is appended to the
    // element on the current path and a path only ever grows at its end.
    const std::span<const LocaleRecord> entries = records.entries();
    std::vector<const LocaleRecord*> ordered;
    ordered.reserve(entries.size());
    for (const LocaleRecord& record : entries) ordered.push_back(&record);
    std::sort(ordered.begin(), ordered.end(), SegmentOrder{});

    std::vector<std::string_view> segments;
    std::vector<std::string_view> open_segments;
    // Child positions from root: the first is <controls> (0).
    std::vector<std::size_t> open_indices;
    for (const LocaleRecord* record : ordered) {
        split_segments((*record).stable_id, segments);
        std::size_t shared = 0U;
        while (shared < segments.size() && shared < open_segments.size() &&
               segments[shared] == open_segments[shared]) {
            ++shared;
        }
        open_segments.resize(shared);
        open_indices.resize(shared);
        for (std::size_t level = shared; level < segments.size(); ++level) {
            std::vector<std::size_t> parent_path;
            parent_path.reserve(open_indices.size() + 1U);
            parent_path.push_back(0U);
            parent_path.insert(parent_path.end(), open_indices.begin(), open_indices.end());
            XmlElement& parent = element_at(root, parent_path);
            XmlElement made{};
            made.name = "control";
            made.attributes.push_back(XmlAttribute{"name", std::string(segments[level])});
            parent.children.push_back(std::move(made));
            open_segments.push_back(segments[level]);
            open_indices.push_back(parent.children.size() - 1U);
        }
        std::vector<std::size_t> target_path;
        target_path.reserve(open_indices.size() + 1U);
        target_path.push_back(0U);
        target_path.insert(target_path.end(), open_indices.begin(), open_indices.end());
        XmlElement& target = element_at(root, target_path);
        for (const LocaleField field : attribute_fields) {
            const std::optional<std::string>& value = locale_record_field(*record, field);
            if (value.has_value()) {
                target.attributes.push_back(XmlAttribute{std::string(locale_field_name(field)), *value});
            }
        }
        const std::optional<std::string>& body = locale_record_field(*record, LocaleField::help);
        const std::optional<std::string>& keywords = locale_record_field(*record, LocaleField::keywords);
        const bool has_body = body.has_value() && !(*body).empty();
        if (has_body || keywords.has_value()) {
            // The record's element is new, so the help element is its first
            // child and every nested control follows it.
            XmlElement help{};
            help.name = "help";
            if (keywords.has_value()) help.attributes.push_back(XmlAttribute{"keywords", *keywords});
            if (has_body) help.text = *body;
            target.children.push_back(std::move(help));
        }
    }
    const XmlWriteResult written = write_xml(root, XmlWriteOptions{}, xml_limits);
    return written;
}

// --- LocaleCatalog ----------------------------------------------------------------

LocaleCatalog::LocaleCatalog(std::string built_in_language) : built_in_language_(std::move(built_in_language)) {
    if (!valid_language_tag(built_in_language_)) {
        throw std::invalid_argument("GUI.Forms built-in language must be a language tag");
    }
}

const LocaleLayer& LocaleCatalog::layer(LocaleLayerKind which) const& noexcept {
    switch (which) {
    case LocaleLayerKind::built_in:
        return built_in_;
    case LocaleLayerKind::program:
        return program_;
    case LocaleLayerKind::translation:
        return translation_;
    case LocaleLayerKind::user:
        return user_;
    }
    return built_in_;
}

const std::string& LocaleCatalog::current_language() const noexcept {
    if (!translation_info_.language.empty()) return translation_info_.language;
    return built_in_language_;
}

LocaleReadResult LocaleCatalog::load_built_in(std::string_view utf8) {
    LocaleReadResult result = read_locale(utf8);
    if (!result) return result;
    if (result.info.language != built_in_language_) {
        LocaleReadResult refused{};
        refused.error = LocaleFileError::language_mismatch;
        refused.info = std::move(result.info);
        return refused;
    }
    built_in_.merge_over(result.records);
    result.records.clear();
    ++revision_;
    return result;
}

bool LocaleCatalog::set_program_values(LocaleLayer records) {
    if (records == program_) return false;
    program_ = std::move(records);
    ++revision_;
    return true;
}

LocaleReadResult LocaleCatalog::load_translation(std::string_view utf8) {
    LocaleReadResult result = read_locale(utf8);
    if (!result) return result;
    if (result.info.language != translation_info_.language) user_.clear();
    translation_ = std::move(result.records);
    result.records = LocaleLayer{};
    translation_info_ = result.info;
    ++revision_;
    return result;
}

void LocaleCatalog::clear_translation() {
    if (translation_info_.language.empty() && translation_.empty()) return;
    translation_.clear();
    translation_info_ = LocaleFileInfo{};
    user_.clear();
    ++revision_;
}

LocaleReadResult LocaleCatalog::load_user_overrides(std::string_view utf8) {
    LocaleReadResult result = read_locale(utf8);
    if (!result) return result;
    if (result.info.language != current_language()) {
        LocaleReadResult refused{};
        refused.error = LocaleFileError::language_mismatch;
        refused.info = std::move(result.info);
        return refused;
    }
    user_ = std::move(result.records);
    result.records = LocaleLayer{};
    ++revision_;
    return result;
}

void LocaleCatalog::clear_user_overrides() {
    if (user_.empty()) return;
    user_.clear();
    ++revision_;
}

void LocaleCatalog::set_built_in_only(bool built_in_only) noexcept {
    if (built_in_only_ == built_in_only) return;
    built_in_only_ = built_in_only;
    ++revision_;
}

LocaleResolvedField LocaleCatalog::resolve(std::string_view stable_id, LocaleField field) const {
    LocaleResolvedField resolved{};
    if (!built_in_only_) {
        if (const LocaleRecord* record = user_.find(stable_id)) {
            const std::optional<std::string>& value = locale_record_field(*record, field);
            if (value.has_value()) {
                resolved.text = *value;
                resolved.origin = LocaleLayerKind::user;
                return resolved;
            }
        }
        if (const LocaleRecord* record = translation_.find(stable_id)) {
            const std::optional<std::string>& value = locale_record_field(*record, field);
            if (value.has_value()) {
                resolved.text = *value;
                resolved.origin = LocaleLayerKind::translation;
                return resolved;
            }
        }
    }
    const bool translated = !built_in_only_ && !translation_info_.language.empty();
    if (const LocaleRecord* record = program_.find(stable_id)) {
        const std::optional<std::string>& value = locale_record_field(*record, field);
        if (value.has_value()) {
            resolved.text = *value;
            resolved.origin = LocaleLayerKind::program;
            resolved.fallback = translated;
            return resolved;
        }
    }
    if (const LocaleRecord* record = built_in_.find(stable_id)) {
        const std::optional<std::string>& value = locale_record_field(*record, field);
        if (value.has_value()) {
            resolved.text = *value;
            resolved.origin = LocaleLayerKind::built_in;
            resolved.fallback = translated;
        }
    }
    return resolved;
}

void LocaleCatalog::collect_ids(std::vector<std::string>& ids) const {
    ids.clear();
    for (const LocaleRecord& record : built_in_.entries()) ids.push_back(record.stable_id);
    for (const LocaleRecord& record : program_.entries()) ids.push_back(record.stable_id);
    if (!built_in_only_) {
        for (const LocaleRecord& record : translation_.entries()) ids.push_back(record.stable_id);
        for (const LocaleRecord& record : user_.entries()) ids.push_back(record.stable_id);
    }
    std::sort(ids.begin(), ids.end());
    const std::vector<std::string>::iterator last = std::unique(ids.begin(), ids.end());
    ids.erase(last, ids.end());
}

void locale_template_records(const LocaleCatalog& catalog, std::string_view language, LocaleLayer& records) {
    records = catalog.layer(LocaleLayerKind::built_in);
    records.merge_over(catalog.layer(LocaleLayerKind::program));
    const LocaleFileInfo& translation = catalog.translation_info();
    if (!language.empty() && !translation.language.empty() && language == translation.language) {
        records.merge_over(catalog.layer(LocaleLayerKind::translation));
    }
}

XmlWriteResult export_locale_template(const LocaleCatalog& catalog, std::string_view language,
                                      const XmlLimits& xml_limits) {
    LocaleLayer records;
    locale_template_records(catalog, language, records);
    LocaleFileInfo info{};
    info.language = language.empty() ? catalog.built_in_language() : std::string(language);
    const LocaleFileInfo& translation = catalog.translation_info();
    if (info.language == translation.language) {
        info.name = translation.name;
        info.direction = translation.direction;
    }
    const XmlWriteResult written = write_locale(records, info, xml_limits);
    return written;
}

bool locale_rescue_requested(std::span<const std::string_view> arguments, std::string_view environment_value) noexcept {
    if (environment_value == "builtin") return true;
    for (std::size_t index = 0U; index < arguments.size(); ++index) {
        if (arguments[index] == "--gui-forms-locale=builtin") return true;
        const bool split = arguments[index] == "--gui-forms-locale" && index + 1U < arguments.size() &&
                           arguments[index + 1U] == "builtin";
        if (split) return true;
    }
    return false;
}

} // namespace gui_forms
