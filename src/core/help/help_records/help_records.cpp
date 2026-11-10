#include "gui_forms/help/help_records/help_records.hpp"

#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/text/types/text_types.hpp"
#include "gui_forms/xml/xml_reader/xml_reader.hpp"
#include "gui_forms/xml/xml_writer/xml_writer.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

// Valid UTF-8 without NUL: what every help field must be.
[[nodiscard]] bool valid_help_text(std::string_view text) noexcept {
    if (text.find('\0') != std::string_view::npos) return false;
    const Utf8ValidationResult validation = validate_utf8(text);
    return validation.valid();
}

// Whether `segment` is one stable-ID segment (no '.').
[[nodiscard]] bool valid_segment(std::string_view segment) noexcept {
    return segment.find('.') == std::string_view::npos && valid_stable_id(segment);
}

[[nodiscard]] std::string_view trim_spaces(std::string_view text) noexcept {
    std::size_t begin = 0U;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
    while (end > begin && (text[end - 1U] == ' ' || text[end - 1U] == '\t')) --end;
    return text.substr(begin, end - begin);
}

[[nodiscard]] std::size_t field_limit(const HelpFieldLimits& limits, HelpField field) noexcept {
    switch (field) {
    case HelpField::title:
        return limits.maximum_title_bytes;
    case HelpField::help_line:
        return limits.maximum_help_line_bytes;
    case HelpField::help:
        return limits.maximum_help_bytes;
    case HelpField::keywords:
        return limits.maximum_keywords_bytes;
    }
    return 0U;
}

// The reader's working state for one file: the path of segments down to
// the element being read, the records found and the rejections.
struct LocaleWalk final {
    const HelpFieldLimits* limits{};
    std::vector<std::string_view> path{};
    std::string joined{};
    HelpRecordLayer* records{};
    std::vector<HelpEntryRejection>* rejections{};
    std::vector<std::string> seen{};
    bool too_many{};
};

void join_path(LocaleWalk& walk) {
    walk.joined.clear();
    for (std::size_t index = 0U; index < walk.path.size(); ++index) {
        if (index > 0U) walk.joined.push_back('.');
        walk.joined.append(walk.path[index]);
    }
}

void reject(LocaleWalk& walk, HelpField field, HelpEntryRefusal reason, std::string_view detail) {
    HelpEntryRejection rejection{};
    rejection.stable_id = walk.joined;
    rejection.field = field;
    rejection.reason = reason;
    rejection.detail = std::string(detail);
    (*walk.rejections).push_back(std::move(rejection));
}

// Checks one field's value and stores it in `entry`, or rejects it.
void accept_field(LocaleWalk& walk, HelpRecordEntry& entry, HelpField field, std::string value) {
    if (!valid_help_text(value)) {
        reject(walk, field, HelpEntryRefusal::invalid_text, {});
        return;
    }
    if (value.size() > field_limit(*walk.limits, field)) {
        reject(walk, field, HelpEntryRefusal::too_long, {});
        return;
    }
    if (field == HelpField::keywords) {
        std::vector<HelpKeyword> parsed;
        if (!parse_help_keywords(value, parsed)) {
            reject(walk, field, HelpEntryRefusal::invalid_keywords, value);
            return;
        }
    }
    help_entry_field(entry, field) = std::move(value);
}

void read_control(LocaleWalk& walk, const XmlElement& element);

void read_children(LocaleWalk& walk, const XmlElement& element) {
    for (const XmlElement& child : element.children) {
        if (walk.too_many) return;
        if (child.name == "control") read_control(walk, child);
    }
}

void read_control(LocaleWalk& walk, const XmlElement& element) {
    const XmlAttribute* name = element.find_attribute("name");
    const std::string_view segment = name != nullptr ? std::string_view((*name).value) : std::string_view{};
    if (!valid_segment(segment)) {
        join_path(walk);
        reject(walk, HelpField::title, HelpEntryRefusal::invalid_stable_id, segment);
        return;
    }
    walk.path.push_back(segment);
    join_path(walk);
    HelpRecordEntry entry{};
    entry.stable_id = walk.joined;
    if (const XmlAttribute* title = element.find_attribute("text")) {
        accept_field(walk, entry, HelpField::title, (*title).value);
    }
    if (const XmlAttribute* line = element.find_attribute("help-line")) {
        accept_field(walk, entry, HelpField::help_line, (*line).value);
    }
    for (const XmlElement& child : element.children) {
        if (child.name != "help") continue;
        std::string body;
        normalize_help_body(child.text, body);
        accept_field(walk, entry, HelpField::help, std::move(body));
        if (const XmlAttribute* keywords = child.find_attribute("keywords")) {
            accept_field(walk, entry, HelpField::keywords, (*keywords).value);
        }
        break;
    }
    const bool has_field = entry.title.has_value() || entry.help_line.has_value() || entry.help.has_value() ||
                           entry.keywords.has_value();
    if (has_field) {
        if ((*walk.records).find(entry.stable_id) != nullptr) {
            reject(walk, HelpField::title, HelpEntryRefusal::duplicate_record, {});
        } else if ((*walk.records).size() >= (*walk.limits).maximum_records) {
            walk.too_many = true;
        } else {
            (*walk.records).put(std::move(entry));
        }
    }
    read_children(walk, element);
    walk.path.pop_back();
}

// Finds or makes the `<control name="segment">` child of `parent`.
XmlElement& control_child(XmlElement& parent, std::string_view segment) {
    for (XmlElement& child : parent.children) {
        if (child.name != "control") continue;
        const XmlAttribute* name = child.find_attribute("name");
        if (name != nullptr && (*name).value == segment) return child;
    }
    XmlElement made{};
    made.name = "control";
    made.attributes.push_back(XmlAttribute{"name", std::string(segment)});
    parent.children.push_back(std::move(made));
    return parent.children.back();
}

} // namespace

std::string_view help_field_name(HelpField field) noexcept {
    switch (field) {
    case HelpField::title:
        return "text";
    case HelpField::help_line:
        return "help-line";
    case HelpField::help:
        return "help";
    case HelpField::keywords:
        return "keywords";
    }
    return {};
}

std::string_view help_entry_refusal_name(HelpEntryRefusal refusal) noexcept {
    switch (refusal) {
    case HelpEntryRefusal::invalid_stable_id:
        return "invalid_stable_id";
    case HelpEntryRefusal::too_long:
        return "too_long";
    case HelpEntryRefusal::invalid_text:
        return "invalid_text";
    case HelpEntryRefusal::invalid_keywords:
        return "invalid_keywords";
    case HelpEntryRefusal::duplicate_record:
        return "duplicate_record";
    case HelpEntryRefusal::markdown_fault:
        return "markdown_fault";
    case HelpEntryRefusal::broken_link:
        return "broken_link";
    case HelpEntryRefusal::missing_image:
        return "missing_image";
    }
    return {};
}

std::string_view help_locale_error_name(HelpLocaleError error) noexcept {
    switch (error) {
    case HelpLocaleError::none:
        return "none";
    case HelpLocaleError::xml:
        return "xml";
    case HelpLocaleError::wrong_root:
        return "wrong_root";
    case HelpLocaleError::no_language:
        return "no_language";
    case HelpLocaleError::too_many_records:
        return "too_many_records";
    }
    return {};
}

const std::optional<std::string>& help_entry_field(const HelpRecordEntry& entry, HelpField field) noexcept {
    switch (field) {
    case HelpField::title:
        return entry.title;
    case HelpField::help_line:
        return entry.help_line;
    case HelpField::help:
        return entry.help;
    case HelpField::keywords:
        return entry.keywords;
    }
    return entry.title;
}

std::optional<std::string>& help_entry_field(HelpRecordEntry& entry, HelpField field) noexcept {
    switch (field) {
    case HelpField::title:
        return entry.title;
    case HelpField::help_line:
        return entry.help_line;
    case HelpField::help:
        return entry.help;
    case HelpField::keywords:
        return entry.keywords;
    }
    return entry.title;
}

// --- HelpRecordLayer ----------------------------------------------------------

std::size_t HelpRecordLayer::lower_bound(std::string_view stable_id) const noexcept {
    std::size_t low = 0U;
    std::size_t high = entries_.size();
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2U;
        if (std::string_view(entries_[middle].stable_id) < stable_id) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    return low;
}

const HelpRecordEntry* HelpRecordLayer::find(std::string_view stable_id) const& noexcept {
    const std::size_t position = lower_bound(stable_id);
    if (position < entries_.size() && entries_[position].stable_id == stable_id) return &entries_[position];
    return nullptr;
}

void HelpRecordLayer::set(std::string_view stable_id, HelpField field, std::string value) {
    if (!valid_stable_id(stable_id)) throw std::invalid_argument("GUI.Forms help record ID must be a stable ID");
    if (!valid_help_text(value)) throw std::invalid_argument("GUI.Forms help text must be valid UTF-8 without NUL");
    const std::size_t position = lower_bound(stable_id);
    if (position < entries_.size() && entries_[position].stable_id == stable_id) {
        help_entry_field(entries_[position], field) = std::move(value);
        return;
    }
    HelpRecordEntry entry{};
    entry.stable_id = std::string(stable_id);
    help_entry_field(entry, field) = std::move(value);
    entries_.insert(entries_.begin() + static_cast<std::ptrdiff_t>(position), std::move(entry));
}

bool HelpRecordLayer::reset(std::string_view stable_id, HelpField field) {
    const std::size_t position = lower_bound(stable_id);
    if (position >= entries_.size() || entries_[position].stable_id != stable_id) return false;
    HelpRecordEntry& entry = entries_[position];
    std::optional<std::string>& value = help_entry_field(entry, field);
    if (!value.has_value()) return false;
    value.reset();
    const bool empty = !entry.title.has_value() && !entry.help_line.has_value() && !entry.help.has_value() &&
                       !entry.keywords.has_value();
    if (empty) entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(position));
    return true;
}

void HelpRecordLayer::put(HelpRecordEntry entry) {
    if (!valid_stable_id(entry.stable_id)) {
        throw std::invalid_argument("GUI.Forms help record ID must be a stable ID");
    }
    const std::size_t position = lower_bound(entry.stable_id);
    if (position < entries_.size() && entries_[position].stable_id == entry.stable_id) {
        entries_[position] = std::move(entry);
        return;
    }
    entries_.insert(entries_.begin() + static_cast<std::ptrdiff_t>(position), std::move(entry));
}

void HelpRecordLayer::merge_over(const HelpRecordLayer& over) {
    for (const HelpRecordEntry& incoming : over.entries_) {
        const std::size_t position = lower_bound(incoming.stable_id);
        if (position < entries_.size() && entries_[position].stable_id == incoming.stable_id) {
            HelpRecordEntry& existing = entries_[position];
            if (incoming.title.has_value()) existing.title = incoming.title;
            if (incoming.help_line.has_value()) existing.help_line = incoming.help_line;
            if (incoming.help.has_value()) existing.help = incoming.help;
            if (incoming.keywords.has_value()) existing.keywords = incoming.keywords;
            continue;
        }
        entries_.insert(entries_.begin() + static_cast<std::ptrdiff_t>(position), incoming);
    }
}

// --- Fields ---------------------------------------------------------------------

bool parse_help_keywords(std::string_view field, std::vector<HelpKeyword>& keywords) {
    keywords.clear();
    if (trim_spaces(field).empty()) return true;
    std::size_t begin = 0U;
    while (begin <= field.size()) {
        std::size_t end = field.find(';', begin);
        if (end == std::string_view::npos) end = field.size();
        const std::string_view item = trim_spaces(field.substr(begin, end - begin));
        const std::size_t comma = item.find(',');
        if (item.empty() || (comma != std::string_view::npos && item.find(',', comma + 1U) != std::string_view::npos)) {
            keywords.clear();
            return false;
        }
        HelpKeyword keyword{};
        if (comma == std::string_view::npos) {
            keyword.keyword = std::string(item);
        } else {
            keyword.keyword = std::string(trim_spaces(item.substr(0U, comma)));
            keyword.subkeyword = std::string(trim_spaces(item.substr(comma + 1U)));
            if (keyword.keyword.empty() || keyword.subkeyword.empty()) {
                keywords.clear();
                return false;
            }
        }
        keywords.push_back(std::move(keyword));
        if (end == field.size()) break;
        begin = end + 1U;
    }
    return true;
}

void normalize_help_body(std::string_view raw, std::string& body) {
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

// --- Locale files ---------------------------------------------------------------

HelpLocaleReadResult read_help_locale(std::string_view utf8, const HelpFieldLimits& limits,
                                      const XmlLimits& xml_limits) {
    HelpLocaleReadResult result{};
    const XmlReadResult document = read_xml(utf8, xml_limits);
    if (!document) {
        result.error = HelpLocaleError::xml;
        result.xml_error = document.error;
        result.position = document.position;
        return result;
    }
    const XmlElement& root = document.root;
    if (root.name != "locale") {
        result.error = HelpLocaleError::wrong_root;
        return result;
    }
    const XmlAttribute* language = root.find_attribute("xml:lang");
    if (language == nullptr || (*language).value.empty()) {
        result.error = HelpLocaleError::no_language;
        return result;
    }
    LocaleWalk walk{};
    walk.limits = &limits;
    walk.records = &result.records;
    walk.rejections = &result.rejections;
    for (const XmlElement& section : root.children) {
        if (section.name == "controls") read_children(walk, section);
    }
    if (walk.too_many) {
        HelpLocaleReadResult refused{};
        refused.error = HelpLocaleError::too_many_records;
        return refused;
    }
    result.language = (*language).value;
    return result;
}

XmlWriteResult write_help_locale(const HelpRecordLayer& records, std::string_view language,
                                 const XmlLimits& xml_limits) {
    XmlElement root{};
    root.name = "locale";
    root.attributes.push_back(XmlAttribute{"xml:lang", std::string(language)});
    XmlElement controls{};
    controls.name = "controls";
    root.children.push_back(std::move(controls));
    std::vector<std::string_view> segments;
    for (const HelpRecordEntry& entry : records.entries()) {
        segments.clear();
        std::string_view rest(entry.stable_id);
        while (!rest.empty()) {
            const std::size_t dot = rest.find('.');
            if (dot == std::string_view::npos) {
                segments.push_back(rest);
                break;
            }
            segments.push_back(rest.substr(0U, dot));
            rest.remove_prefix(dot + 1U);
        }
        // Each segment's element is found again from the root: a pointer
        // kept across control_child would dangle when a sibling is added.
        XmlElement* element = &root.children.front();
        for (const std::string_view segment : segments) element = &control_child(*element, segment);
        XmlElement& target = *element;
        if (entry.title.has_value()) target.attributes.push_back(XmlAttribute{"text", *entry.title});
        if (entry.help_line.has_value()) target.attributes.push_back(XmlAttribute{"help-line", *entry.help_line});
        if (entry.help.has_value() || entry.keywords.has_value()) {
            XmlElement help{};
            help.name = "help";
            if (entry.keywords.has_value()) help.attributes.push_back(XmlAttribute{"keywords", *entry.keywords});
            if (entry.help.has_value()) help.text = *entry.help;
            // The help element goes before the nested controls.
            target.children.insert(target.children.begin(), std::move(help));
        }
    }
    const XmlWriteResult written = write_xml(root, XmlWriteOptions{}, xml_limits);
    return written;
}

// --- HelpCatalog ----------------------------------------------------------------

const HelpRecordLayer& HelpCatalog::layer(HelpLayer which) const& noexcept {
    switch (which) {
    case HelpLayer::built_in:
        return built_in_;
    case HelpLayer::translation:
        return translation_;
    case HelpLayer::user:
        return user_;
    }
    return built_in_;
}

HelpRecordLayer& HelpCatalog::built_in() & noexcept {
    ++revision_;
    return built_in_;
}

void HelpCatalog::set_translation(HelpRecordLayer records, std::string language) {
    translation_ = std::move(records);
    language_ = std::move(language);
    ++revision_;
}

void HelpCatalog::set_user_overrides(HelpRecordLayer records) {
    user_ = std::move(records);
    ++revision_;
}

void HelpCatalog::set_built_in_only(bool built_in_only) noexcept {
    if (built_in_only_ == built_in_only) return;
    built_in_only_ = built_in_only;
    ++revision_;
}

void HelpCatalog::resolve_field(std::string_view stable_id, HelpField field, HelpResolvedField& resolved) const {
    resolved = HelpResolvedField{};
    if (!built_in_only_) {
        if (const HelpRecordEntry* entry = user_.find(stable_id)) {
            const std::optional<std::string>& value = help_entry_field(*entry, field);
            if (value.has_value()) {
                resolved.text = *value;
                resolved.origin = HelpLayer::user;
                return;
            }
        }
        if (const HelpRecordEntry* entry = translation_.find(stable_id)) {
            const std::optional<std::string>& value = help_entry_field(*entry, field);
            if (value.has_value()) {
                resolved.text = *value;
                resolved.origin = HelpLayer::translation;
                return;
            }
        }
    }
    if (const HelpRecordEntry* entry = built_in_.find(stable_id)) {
        const std::optional<std::string>& value = help_entry_field(*entry, field);
        if (value.has_value()) {
            resolved.text = *value;
            resolved.origin = HelpLayer::built_in;
            resolved.fallback = !built_in_only_ && !translation_.empty();
        }
    }
}

HelpResolvedRecord HelpCatalog::resolve(std::string_view stable_id) const {
    HelpResolvedRecord record{};
    record.stable_id = std::string(stable_id);
    resolve_field(stable_id, HelpField::title, record.title);
    resolve_field(stable_id, HelpField::help_line, record.help_line);
    resolve_field(stable_id, HelpField::help, record.help);
    resolve_field(stable_id, HelpField::keywords, record.keywords);
    record.found = record.title.origin.has_value() || record.help_line.origin.has_value() ||
                   record.help.origin.has_value() || record.keywords.origin.has_value();
    return record;
}

void HelpCatalog::collect_ids(std::vector<std::string>& ids) const {
    ids.clear();
    for (const HelpRecordEntry& entry : built_in_.entries()) ids.push_back(entry.stable_id);
    if (!built_in_only_) {
        for (const HelpRecordEntry& entry : translation_.entries()) ids.push_back(entry.stable_id);
        for (const HelpRecordEntry& entry : user_.entries()) ids.push_back(entry.stable_id);
    }
    std::sort(ids.begin(), ids.end());
    const std::vector<std::string>::iterator last = std::unique(ids.begin(), ids.end());
    ids.erase(last, ids.end());
}

} // namespace gui_forms
