#include "gui_forms/controls/auto_complete.hpp"

#include "gui_forms/text.hpp"

#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

unsigned char folded(unsigned char byte) noexcept {
    const bool upper = byte >= 'A' && byte <= 'Z';
    const unsigned char result = upper ? static_cast<unsigned char>(byte - 'A' + 'a') : byte;
    return result;
}

// Whether `pattern` occurs in `candidate` at byte `offset`; the caller has
// checked that it fits. The comparison is chosen once: exact bytes, or
// bytes with ASCII letters folded.
bool matches_at(std::string_view candidate, std::size_t offset, std::string_view pattern,
                bool case_sensitive) noexcept {
    const std::string_view window = candidate.substr(offset, pattern.size());
    if (case_sensitive) {
        const bool result = window == pattern;
        return result;
    }
    for (std::size_t index = 0U; index < pattern.size(); ++index) {
        const unsigned char left = static_cast<unsigned char>(window[index]);
        const unsigned char right = static_cast<unsigned char>(pattern[index]);
        if (folded(left) != folded(right)) return false;
    }
    return true;
}

void require_utf8(std::string_view value) {
    if (!validate_utf8(value).valid()) {
        throw std::invalid_argument("AutoCompleteStringCollection items must be valid UTF-8");
    }
}

} // namespace

std::optional<HostAutoCompleteSource> host_auto_complete_source(AutoCompleteSource source) noexcept {
    switch (source) {
    case AutoCompleteSource::none:
    case AutoCompleteSource::custom_source:
    case AutoCompleteSource::list_items:
        return std::nullopt;
    case AutoCompleteSource::file_system:
        return HostAutoCompleteSource::file_system;
    case AutoCompleteSource::file_system_directories:
        return HostAutoCompleteSource::file_system_directories;
    case AutoCompleteSource::history_list:
        return HostAutoCompleteSource::history_list;
    case AutoCompleteSource::recently_used_list:
        return HostAutoCompleteSource::recently_used_list;
    case AutoCompleteSource::all_url:
        return HostAutoCompleteSource::all_url;
    case AutoCompleteSource::all_system_sources:
        return HostAutoCompleteSource::all_system_sources;
    }
    return std::nullopt;
}

std::optional<std::size_t> find_text_match(std::string_view candidate, std::string_view pattern,
                                           TextMatchMode mode, bool case_sensitive) noexcept {
    if (pattern.empty() || pattern.size() > candidate.size()) return std::nullopt;
    if (mode == TextMatchMode::prefix) {
        if (matches_at(candidate, 0U, pattern, case_sensitive)) return std::size_t{0U};
        return std::nullopt;
    }
    // The last offset at which the whole pattern still fits.
    const std::size_t last = candidate.size() - pattern.size();
    for (std::size_t offset = 0U; offset <= last; ++offset) {
        if (matches_at(candidate, offset, pattern, case_sensitive)) return offset;
    }
    return std::nullopt;
}

const std::string& AutoCompleteStringCollection::at(std::size_t index) const {
    if (index >= items_.size()) {
        throw std::out_of_range("AutoCompleteStringCollection index is past the end");
    }
    return items_[index];
}

bool AutoCompleteStringCollection::contains(std::string_view value) const noexcept {
    const bool result = index_of(value).has_value();
    return result;
}

std::optional<std::size_t> AutoCompleteStringCollection::index_of(std::string_view value) const noexcept {
    for (std::size_t index = 0U; index < items_.size(); ++index) {
        if (items_[index] == value) return index;
    }
    return std::nullopt;
}

std::size_t AutoCompleteStringCollection::add(std::string value) {
    require_utf8(value);
    items_.push_back(std::move(value));
    const std::size_t index = items_.size() - 1U;
    changed_.emit();
    return index;
}

void AutoCompleteStringCollection::add_range(std::vector<std::string> values) {
    for (const std::string& value : values) require_utf8(value);
    if (values.empty()) return;
    items_.reserve(items_.size() + values.size());
    for (std::string& value : values) items_.push_back(std::move(value));
    changed_.emit();
}

void AutoCompleteStringCollection::insert(std::size_t index, std::string value) {
    if (index > items_.size()) {
        throw std::out_of_range("AutoCompleteStringCollection insertion index is past the end");
    }
    require_utf8(value);
    items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(index), std::move(value));
    changed_.emit();
}

void AutoCompleteStringCollection::set(std::size_t index, std::string value) {
    if (index >= items_.size()) {
        throw std::out_of_range("AutoCompleteStringCollection index is past the end");
    }
    require_utf8(value);
    items_[index] = std::move(value);
    changed_.emit();
}

bool AutoCompleteStringCollection::remove(std::string_view value) {
    const std::optional<std::size_t> found = index_of(value);
    if (!found) return false;
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(*found));
    changed_.emit();
    return true;
}

void AutoCompleteStringCollection::remove_at(std::size_t index) {
    if (index >= items_.size()) {
        throw std::out_of_range("AutoCompleteStringCollection index is past the end");
    }
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
    changed_.emit();
}

void AutoCompleteStringCollection::clear() {
    if (items_.empty()) return;
    items_.clear();
    changed_.emit();
}

} // namespace gui_forms
