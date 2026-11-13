#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/borrow/borrowed_view/borrowed_view.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/host/types/host_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// How a TextBox (or an editable ComboBox) completes what the user types, as
// Windows Forms' AutoCompleteMode.
enum class AutoCompleteMode : std::uint8_t {
    none,            // no completion
    suggest,         // a list of matching candidates under the field
    append,          // the rest of the first match, appended and selected
    suggest_append,  // both
};

// Where the candidates come from, as Windows Forms' AutoCompleteSource.
// custom_source and list_items belong to the control; the others are host
// services (HostServices::query_auto_complete), which no host supplies yet:
// they report HostServiceError::unsupported and offer no candidates.
enum class AutoCompleteSource : std::uint8_t {
    none,
    custom_source,            // the control's AutoCompleteStringCollection
    list_items,               // a ComboBox's items; a plain TextBox refuses it
    file_system,              // host: paths
    file_system_directories,  // host: directory paths
    history_list,             // host: the browsing history
    recently_used_list,       // host: recently used documents
    all_url,                  // host: history and recently used URLs
    all_system_sources,       // host: every host source
};

// The host source an AutoCompleteSource names, or none for a source the
// control owns (none, custom_source, list_items).
[[nodiscard]] GUI_FORMS_API std::optional<HostAutoCompleteSource> host_auto_complete_source(
    AutoCompleteSource source) noexcept;

// The most candidates a suggestion list shows; later matches are dropped.
inline constexpr std::size_t maximum_auto_complete_suggestions = 256U;

// How a typed pattern matches a candidate.
enum class TextMatchMode : std::uint8_t {
    prefix,     // the candidate starts with the pattern
    substring,  // the pattern occurs anywhere in the candidate
};

// The part of a text that matched a pattern: `length` bytes from byte
// `offset`, both on scalar boundaries of the text. A zero length is no
// match. Suggestion and drop-down lists highlight it.
struct GUI_FORMS_API TextMatchRange final {
    std::size_t offset{};
    std::size_t length{};
};

// The byte offset in `candidate` of the first match of `pattern`, or none.
// Bytes compare exactly, except that ASCII letters compare without case
// unless `case_sensitive`; other characters (accented and non-Latin letters
// among them) compare exactly. Valid UTF-8 inputs therefore match at a scalar
// boundary and the match is pattern.size() bytes long. An empty pattern
// matches nothing.
[[nodiscard]] GUI_FORMS_API std::optional<std::size_t> find_text_match(
    std::string_view candidate, std::string_view pattern, TextMatchMode mode,
    bool case_sensitive) noexcept;

// An ordered list of completion candidates, as Windows Forms'
// AutoCompleteStringCollection. Each string is valid UTF-8; duplicates are
// kept (a suggestion list shows each text once). A control holds its
// collection through a std::shared_ptr: a new TextBox owns an empty one,
// and assigning one collection to several controls shares it, so it lives
// while any of them (or the program) holds it. Use it on the UI thread.
class GUI_FORMS_API AutoCompleteStringCollection final {
public:
    AutoCompleteStringCollection() = default;
    AutoCompleteStringCollection(const AutoCompleteStringCollection&) = delete;
    AutoCompleteStringCollection& operator=(const AutoCompleteStringCollection&) = delete;

    [[nodiscard]] BorrowedSpan<std::string> items() const& noexcept { return detail::lend_view(items_); }
    BorrowedSpan<std::string> items() const&& = delete;
    [[nodiscard]] std::size_t size() const noexcept { return items_.size(); }
    [[nodiscard]] bool empty() const noexcept { return items_.empty(); }
    // Throws std::out_of_range for an index past the end.
    [[nodiscard]] const std::string& at(std::size_t index) const;
    [[nodiscard]] bool contains(std::string_view value) const noexcept;
    // The first index holding `value`, or none.
    [[nodiscard]] std::optional<std::size_t> index_of(std::string_view value) const noexcept;

    // Each mutator validates before changing anything: text that is not
    // valid UTF-8 throws std::invalid_argument and an index past the end
    // std::out_of_range. Each change raises `changed` once.
    // Appends and returns the new item's index.
    std::size_t add(std::string value);
    void add_range(std::vector<std::string> values);
    void insert(std::size_t index, std::string value);
    void set(std::size_t index, std::string value);
    // Removes the first item equal to `value`; returns false when none is.
    bool remove(std::string_view value);
    void remove_at(std::size_t index);
    // Removes every item; raises `changed` only when there were some.
    void clear();

    [[nodiscard]] Event<>& changed() noexcept { return changed_; }

private:
    detail::LendingVector<std::string> items_;
    Event<> changed_;
};

} // namespace gui_forms
