#include "gui_forms/help/help_records/help_records.hpp"

#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/text/types/text_types.hpp"

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

[[nodiscard]] std::string_view trim_spaces(std::string_view text) noexcept {
    std::size_t begin = 0U;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
    while (end > begin && (text[end - 1U] == ' ' || text[end - 1U] == '\t')) --end;
    return text.substr(begin, end - begin);
}

// The language-file field a help field is.
[[nodiscard]] LocaleField locale_field_of(HelpField field) noexcept {
    switch (field) {
    case HelpField::title:
        return LocaleField::text;
    case HelpField::help_line:
        return LocaleField::help_line;
    case HelpField::help:
        return LocaleField::help;
    case HelpField::keywords:
        return LocaleField::keywords;
    }
    return LocaleField::text;
}

// The help field a language-file field is, or nothing for the others.
[[nodiscard]] std::optional<HelpField> help_field_of(LocaleField field) noexcept {
    switch (field) {
    case LocaleField::text:
        return HelpField::title;
    case LocaleField::help_line:
        return HelpField::help_line;
    case LocaleField::help:
        return HelpField::help;
    case LocaleField::keywords:
        return HelpField::keywords;
    case LocaleField::tool_tip:
    case LocaleField::access_key:
    case LocaleField::title:
    case LocaleField::placeholder:
    case LocaleField::header:
    case LocaleField::accessible_name:
    case LocaleField::accessible_description:
        return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] HelpEntryRefusal help_refusal_of(LocaleRefusal refusal) noexcept {
    switch (refusal) {
    case LocaleRefusal::invalid_stable_id:
        return HelpEntryRefusal::invalid_stable_id;
    case LocaleRefusal::too_long:
        return HelpEntryRefusal::too_long;
    case LocaleRefusal::invalid_text:
        return HelpEntryRefusal::invalid_text;
    case LocaleRefusal::control_character:
        return HelpEntryRefusal::control_character;
    case LocaleRefusal::unbalanced_bidi:
        return HelpEntryRefusal::unbalanced_bidi;
    case LocaleRefusal::invalid_keywords:
        return HelpEntryRefusal::invalid_keywords;
    case LocaleRefusal::duplicate_record:
        return HelpEntryRefusal::duplicate_record;
    case LocaleRefusal::invalid_access_key:
        return HelpEntryRefusal::invalid_text;
    }
    return HelpEntryRefusal::invalid_text;
}

[[nodiscard]] HelpLocaleError help_error_of(LocaleFileError error) noexcept {
    switch (error) {
    case LocaleFileError::none:
        return HelpLocaleError::none;
    case LocaleFileError::xml:
        return HelpLocaleError::xml;
    case LocaleFileError::wrong_root:
        return HelpLocaleError::wrong_root;
    case LocaleFileError::no_language:
        return HelpLocaleError::no_language;
    case LocaleFileError::invalid_language:
        return HelpLocaleError::invalid_language;
    case LocaleFileError::invalid_direction:
        return HelpLocaleError::invalid_direction;
    case LocaleFileError::too_many_records:
        return HelpLocaleError::too_many_records;
    case LocaleFileError::language_mismatch:
        return HelpLocaleError::no_language;
    }
    return HelpLocaleError::xml;
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
    case HelpEntryRefusal::control_character:
        return "control_character";
    case HelpEntryRefusal::unbalanced_bidi:
        return "unbalanced_bidi";
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
    case HelpLocaleError::invalid_language:
        return "invalid_language";
    case HelpLocaleError::invalid_direction:
        return "invalid_direction";
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

void normalize_help_body(std::string_view raw, std::string& body) { normalize_locale_help_body(raw, body); }

// --- Locale files ---------------------------------------------------------------

void project_help_fields(const LocaleLayer& source, HelpRecordLayer& target) {
    target.clear();
    for (const LocaleRecord& record : source.entries()) {
        HelpRecordEntry entry{};
        entry.stable_id = record.stable_id;
        entry.title = locale_record_field(record, LocaleField::text);
        entry.help_line = locale_record_field(record, LocaleField::help_line);
        entry.help = locale_record_field(record, LocaleField::help);
        entry.keywords = locale_record_field(record, LocaleField::keywords);
        const bool any = entry.title.has_value() || entry.help_line.has_value() || entry.help.has_value() ||
                         entry.keywords.has_value();
        // The source is sorted with each ID once, so each put appends.
        if (any) target.put(std::move(entry));
    }
}

HelpLocaleReadResult read_help_locale(std::string_view utf8, const HelpFieldLimits& limits,
                                      const XmlLimits& xml_limits) {
    LocaleLimits locale_limits{};
    locale_limits.maximum_field_bytes[static_cast<std::size_t>(LocaleField::text)] = limits.maximum_title_bytes;
    locale_limits.maximum_field_bytes[static_cast<std::size_t>(LocaleField::help_line)] =
        limits.maximum_help_line_bytes;
    locale_limits.maximum_field_bytes[static_cast<std::size_t>(LocaleField::help)] = limits.maximum_help_bytes;
    locale_limits.maximum_field_bytes[static_cast<std::size_t>(LocaleField::keywords)] =
        limits.maximum_keywords_bytes;
    locale_limits.maximum_records = limits.maximum_records;
    const LocaleReadResult read = read_locale(utf8, locale_limits, xml_limits);
    HelpLocaleReadResult result{};
    if (!read) {
        result.error = help_error_of(read.error);
        result.xml_error = read.xml_error;
        result.position = read.position;
        return result;
    }
    result.language = read.info.language;
    project_help_fields(read.records, result.records);
    for (const LocaleRejection& rejection : read.rejections) {
        const std::optional<HelpField> field = help_field_of(rejection.field);
        if (!field.has_value()) continue;
        HelpEntryRejection kept{};
        kept.stable_id = rejection.stable_id;
        kept.field = *field;
        kept.reason = help_refusal_of(rejection.reason);
        kept.detail = rejection.detail;
        result.rejections.push_back(std::move(kept));
    }
    return result;
}

XmlWriteResult write_help_locale(const HelpRecordLayer& records, std::string_view language,
                                 const XmlLimits& xml_limits) {
    std::vector<LocaleRecord> converted;
    converted.reserve(records.size());
    for (const HelpRecordEntry& entry : records.entries()) {
        LocaleRecord record{};
        record.stable_id = entry.stable_id;
        locale_record_field(record, locale_field_of(HelpField::title)) = entry.title;
        locale_record_field(record, locale_field_of(HelpField::help_line)) = entry.help_line;
        locale_record_field(record, locale_field_of(HelpField::help)) = entry.help;
        locale_record_field(record, locale_field_of(HelpField::keywords)) = entry.keywords;
        converted.push_back(std::move(record));
    }
    LocaleLayer layer;
    layer.assign_sorted(std::move(converted));
    LocaleFileInfo info{};
    info.language = std::string(language);
    const XmlWriteResult written = write_locale(layer, info, xml_limits);
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

void HelpCatalog::adopt_locale(const LocaleCatalog& locale) {
    project_help_fields(locale.layer(LocaleLayerKind::built_in), built_in_);
    HelpRecordLayer program;
    project_help_fields(locale.layer(LocaleLayerKind::program), program);
    built_in_.merge_over(program);
    project_help_fields(locale.layer(LocaleLayerKind::translation), translation_);
    project_help_fields(locale.layer(LocaleLayerKind::user), user_);
    language_ = locale.translation_info().language;
    built_in_only_ = locale.built_in_only();
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
