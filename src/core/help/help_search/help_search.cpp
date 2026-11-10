#include "gui_forms/help/help_search/help_search.hpp"

#include <algorithm>
#include <utility>

namespace gui_forms {
namespace {

// One record still in the running, with its summed counts.
struct Candidate final {
    std::uint32_t record{};
    std::uint32_t frequency{};
    bool title_hit{};
};

struct PostingRecordOrder final {
    bool operator()(const HelpSearchPosting& left, const HelpSearchPosting& right) const noexcept {
        return left.record < right.record;
    }
};

// Ranks hits: title hits first, then frequency, then contents order, then
// record order (which is stable-ID order).
struct HitOrder final {
    const HelpBookSection* section{};

    bool operator()(const HelpSearchHit& left, const HelpSearchHit& right) const noexcept {
        if (left.title_hit != right.title_hit) return left.title_hit;
        if (left.frequency != right.frequency) return left.frequency > right.frequency;
        const std::uint32_t left_order = (*section).records[left.record].contents_node;
        const std::uint32_t right_order = (*section).records[right.record].contents_node;
        if (left_order != right_order) return left_order < right_order;
        return left.record < right.record;
    }
};

// The first term not before `word`.
[[nodiscard]] std::size_t term_position(const HelpBookSection& section, std::string_view word) noexcept {
    std::size_t low = 0U;
    std::size_t high = section.terms.size();
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2U;
        if (std::string_view(section.terms[middle].term) < word) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    return low;
}

// Fills `postings` (cleared) with the postings of `word`: its own term, or
// every term it begins when `prefix`; merged by record, sorted by record.
// Counts each posting visited against `spent`.
void gather_postings(const HelpBookSection& section, std::string_view word, bool prefix, bool titles_only,
                     std::vector<HelpSearchPosting>& postings, std::size_t& spent) {
    postings.clear();
    std::size_t position = term_position(section, word);
    while (position < section.terms.size()) {
        const HelpSearchTerm& term = section.terms[position];
        const bool exact = term.term == word;
        const bool begins = prefix && std::string_view(term.term).substr(0U, word.size()) == word;
        if (!exact && !begins) break;
        for (std::uint32_t index = 0U; index < term.posting_count; ++index) {
            const HelpSearchPosting& posting = section.postings[term.first_posting + index];
            ++spent;
            if (titles_only && !posting.in_title) continue;
            postings.push_back(posting);
        }
        if (!prefix) break;
        ++position;
    }
    if (!prefix) return;
    std::sort(postings.begin(), postings.end(), PostingRecordOrder{});
    std::size_t written = 0U;
    for (std::size_t index = 0U; index < postings.size(); ++index) {
        if (written > 0U && postings[written - 1U].record == postings[index].record) {
            postings[written - 1U].count += postings[index].count;
            postings[written - 1U].in_title = postings[written - 1U].in_title || postings[index].in_title;
            continue;
        }
        postings[written] = postings[index];
        ++written;
    }
    postings.resize(written);
}

// Whether `phrase` appears as consecutive words of `words`.
[[nodiscard]] bool holds_phrase(const std::vector<std::string_view>& words, const std::vector<std::string>& phrase,
                                std::size_t& spent) noexcept {
    if (phrase.empty()) return true;
    if (words.size() < phrase.size()) return false;
    for (std::size_t start = 0U; start + phrase.size() <= words.size(); ++start) {
        ++spent;
        std::size_t matched = 0U;
        while (matched < phrase.size() && words[start + matched] == phrase[matched]) ++matched;
        if (matched == phrase.size()) return true;
    }
    return false;
}

} // namespace

void parse_help_query(std::string_view text, HelpSearchQuery& query) {
    query.words.clear();
    query.phrases.clear();
    query.last_is_prefix = false;
    std::string folded;
    fold_help_text(text, folded);
    const std::string_view source(folded);
    std::vector<std::string_view> words;
    std::size_t index = 0U;
    while (index < source.size()) {
        const std::size_t quote = source.find('"', index);
        const std::string_view plain =
            source.substr(index, quote == std::string_view::npos ? std::string_view::npos : quote - index);
        split_help_words(plain, words);
        for (const std::string_view word : words) query.words.push_back(std::string(word));
        if (quote == std::string_view::npos) {
            // The last word is still being typed when nothing follows it.
            const bool ends_in_word = !plain.empty() && !words.empty() &&
                                      plain.data() + plain.size() == words.back().data() + words.back().size();
            query.last_is_prefix = ends_in_word;
            return;
        }
        const std::size_t close = source.find('"', quote + 1U);
        const std::string_view quoted =
            source.substr(quote + 1U, close == std::string_view::npos ? std::string_view::npos : close - quote - 1U);
        split_help_words(quoted, words);
        std::vector<std::string> phrase;
        for (const std::string_view word : words) {
            phrase.push_back(std::string(word));
            query.words.push_back(std::string(word));
        }
        if (!phrase.empty()) query.phrases.push_back(std::move(phrase));
        if (close == std::string_view::npos) return;
        index = close + 1U;
    }
}

HelpSearchStatus search_help(const HelpBookSection& section, const HelpSearchQuery& query,
                             const HelpSearchOptions& options, std::vector<HelpSearchHit>& hits) {
    hits.clear();
    if (query.words.empty()) return HelpSearchStatus::empty_query;
    std::size_t spent = 0U;
    bool partial = false;
    std::vector<Candidate> candidates;
    std::vector<Candidate> kept;
    std::vector<HelpSearchPosting> postings;
    for (std::size_t word_index = 0U; word_index < query.words.size(); ++word_index) {
        const bool prefix = query.last_is_prefix && word_index + 1U == query.words.size();
        gather_postings(section, query.words[word_index], prefix, options.titles_only, postings, spent);
        if (word_index == 0U) {
            for (const HelpSearchPosting& posting : postings) {
                candidates.push_back(Candidate{posting.record, posting.count, posting.in_title});
            }
        } else {
            // Both lists are in record order: one merge intersects them.
            kept.clear();
            std::size_t left = 0U;
            std::size_t right = 0U;
            while (left < candidates.size() && right < postings.size()) {
                if (candidates[left].record < postings[right].record) {
                    ++left;
                } else if (postings[right].record < candidates[left].record) {
                    ++right;
                } else {
                    Candidate merged = candidates[left];
                    merged.frequency += postings[right].count;
                    merged.title_hit = merged.title_hit || postings[right].in_title;
                    kept.push_back(merged);
                    ++left;
                    ++right;
                }
            }
            std::swap(candidates, kept);
        }
        if (candidates.empty()) return HelpSearchStatus::complete;
        if (spent > options.budget) {
            partial = true;
            break;
        }
    }
    std::vector<std::string_view> words;
    for (const Candidate& candidate : candidates) {
        if (!query.phrases.empty()) {
            const HelpBookRecord& record = section.records[candidate.record];
            std::string folded_title;
            fold_help_text(record.title, folded_title);
            split_help_words(options.titles_only ? std::string_view(folded_title)
                                                 : std::string_view(record.folded_text),
                             words);
            bool all = true;
            for (const std::vector<std::string>& phrase : query.phrases) {
                if (!holds_phrase(words, phrase, spent)) {
                    all = false;
                    break;
                }
            }
            if (!all && !options.titles_only) {
                // A phrase may also stand in the title.
                split_help_words(folded_title, words);
                all = true;
                for (const std::vector<std::string>& phrase : query.phrases) {
                    if (!holds_phrase(words, phrase, spent)) {
                        all = false;
                        break;
                    }
                }
            }
            if (!all) continue;
        }
        hits.push_back(HelpSearchHit{candidate.record, candidate.title_hit, candidate.frequency});
        if (spent > options.budget) {
            partial = true;
            break;
        }
    }
    std::sort(hits.begin(), hits.end(), HitOrder{&section});
    if (hits.size() > options.maximum_hits) hits.resize(options.maximum_hits);
    return partial ? HelpSearchStatus::partial : HelpSearchStatus::complete;
}

} // namespace gui_forms
