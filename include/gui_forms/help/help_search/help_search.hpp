#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/help/helpbook_section/helpbook_section.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// Search v1 (help system study, section 7): simple and deterministic. Words
// are folded (fold_help_text) and split (split_help_words); every word of the
// query must be found (AND); a quoted phrase must appear as consecutive
// words of the body or title; the last word, while the user types, matches
// as a prefix. There is no stemming, no NEAR and no similar words.

// A query split into what must be found. Every word, the phrases' words
// included, is a required term; `phrases` keep each phrase's words in
// order for the consecutive check.
struct GUI_FORMS_API HelpSearchQuery final {
    std::vector<std::string> words{};
    std::vector<std::vector<std::string>> phrases{};
    // The last word was typed without a following space or quote: it
    // matches every term it is a prefix of.
    bool last_is_prefix{};
};

// Fills `query` (replaced) from what the user typed: words, and phrases in
// double quotes (an unclosed quote runs to the end).
GUI_FORMS_API void parse_help_query(std::string_view text, HelpSearchQuery& query);

struct GUI_FORMS_API HelpSearchOptions final {
    // Only titles are searched; phrases are checked against the title.
    bool titles_only{};
    // The most postings and phrase words one search visits before it stops
    // with the hits so far (status partial): the per-keystroke budget on
    // the UI thread.
    std::size_t budget{200000U};
    // The most hits kept, best first.
    std::size_t maximum_hits{500U};
};

enum class HelpSearchStatus : std::uint8_t {
    complete,
    // The budget ran out; `hits` holds what was found and ranked so far.
    partial,
    // The query has no word.
    empty_query,
};

// One record that holds every required term. Ranked by title hit, then
// frequency (the required terms' counts, summed), then contents order.
struct GUI_FORMS_API HelpSearchHit final {
    std::uint32_t record{};
    bool title_hit{};
    std::uint32_t frequency{};
};

// Searches `section`. `hits` is cleared first and its storage reused.
[[nodiscard]] GUI_FORMS_API HelpSearchStatus search_help(const HelpBookSection& section, const HelpSearchQuery& query,
                                           const HelpSearchOptions& options, std::vector<HelpSearchHit>& hits);

} // namespace gui_forms
