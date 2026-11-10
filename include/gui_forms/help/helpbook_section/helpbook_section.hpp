#pragma once

#include "gui_forms/help/help_contents/help_contents.hpp"
#include "gui_forms/help/help_records/help_records.hpp"
#include "gui_forms/text/markdown/markdown.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// The helpbook section (help system study, section 5): the help portion of
// one compiled language, built by the language-file compiler's help pass.
// No file carries the `.helpbook` extension; the section travels inside the
// program's text (built in for the built-in language, in the language packs
// for the others) and is rebuilt in memory for a user's locale file. It
// holds the records with their help fields, the generated contents, the
// keyword index, the search term table and the link table.

inline constexpr std::uint16_t helpbook_format_major = 1U;
inline constexpr std::uint16_t helpbook_format_minor = 0U;

// One record of the section: its resolved fields, the help body's plain
// text folded for phrase search, and where the fields came from.
struct HelpBookRecord final {
    std::string stable_id{};
    std::string title{};
    std::string help_line{};
    std::string help{};
    std::string keywords{};
    // markdown_plain_text of the body, case-folded (fold_help_text).
    std::string folded_text{};
    // The record's node in the contents, or help_no_parent.
    std::uint32_t contents_node{help_no_parent};
    // A field came from the built-in layer while a translation is in
    // force (the page marks the fallback).
    bool fallback{};
};

// One keyword of the index: folded for ordering and lookup, as written for
// showing, and the record it opens.
struct HelpKeywordEntry final {
    std::string folded{};
    std::string keyword{};
    std::string subkeyword{};
    std::uint32_t record{};
};

enum class HelpLinkKind : std::uint8_t {
    // A stable ID: opens the topic in the viewer.
    topic,
    // A stable ID with the title "popup": opens the topic in the paper popup.
    popup,
    // Any other destination: offered to the application as an event.
    external,
    // An image's source: a PNG resource of the program.
    image,
};

[[nodiscard]] std::string_view help_link_kind_name(HelpLinkKind kind) noexcept;

// One link of a body, in record and source order.
struct HelpLinkEntry final {
    std::uint32_t record{};
    HelpLinkKind kind{HelpLinkKind::external};
    // The stable ID (topic, popup), the destination as written (external)
    // or the image name.
    std::string target{};
    // After '#' in a topic link: the heading anchor inside the topic.
    std::string fragment{};
};

// One posting of a search term: a record that holds it, how many times,
// and whether its title does.
struct HelpSearchPosting final {
    std::uint32_t record{};
    std::uint32_t count{};
    bool in_title{};
};

// One search term: folded, with its postings (HelpBookSection::postings from
// first_posting, posting_count of them, in record order). Terms are sorted
// by bytes.
struct HelpSearchTerm final {
    std::string term{};
    std::uint32_t first_posting{};
    std::uint32_t posting_count{};
};

struct HelpBookSection final {
    std::uint16_t format_major{helpbook_format_major};
    std::uint16_t format_minor{helpbook_format_minor};
    std::string language{};
    // The catalog revision the section was compiled from.
    std::uint64_t catalog_revision{};
    // Sorted by stable ID.
    std::vector<HelpBookRecord> records{};
    HelpContentsTree contents{};
    // Sorted by folded keyword, then subkeyword, then record.
    std::vector<HelpKeywordEntry> keywords{};
    std::vector<HelpSearchTerm> terms{};
    std::vector<HelpSearchPosting> postings{};
    std::vector<HelpLinkEntry> links{};

    // The record holding `stable_id`, or none.
    [[nodiscard]] std::optional<std::size_t> find(std::string_view stable_id) const noexcept;
    // The first keyword entry not before `folded_prefix`: the index tab's
    // type-ahead position.
    [[nodiscard]] std::size_t keyword_position(std::string_view folded_prefix) const noexcept;
};

// Case-folds help text for indexing and search: ASCII letters to lower
// case; other bytes are kept (Unicode case folding is the text engine's and
// comes with the language-file implementation). `folded` is replaced.
void fold_help_text(std::string_view text, std::string& folded);

// Splits folded text into search words: maximal runs of ASCII letters and
// digits and of non-ASCII bytes. Each word is a view into `text`; `words`
// is cleared first.
void split_help_words(std::string_view text, std::vector<std::string_view>& words);

// What a link destination is in a help body (study section 4): a valid
// stable ID, optionally with '#' and an anchor, is a topic link (a popup
// when `title` is "popup"); anything else is external.
struct HelpLinkTarget final {
    HelpLinkKind kind{HelpLinkKind::external};
    std::string target{};
    std::string fragment{};
};

[[nodiscard]] HelpLinkTarget classify_help_link(std::string_view destination, std::string_view title);

struct HelpBookCompileOptions final {
    // The PNG resources an image may name, by name ("name.png"). Any order.
    std::span<const std::string> image_names{};
    MarkdownLimits markdown{};
};

struct HelpBookCompileResult final {
    HelpBookSection section{};
    // Every field the pass rejected: a body that does not parse, links to
    // records that do not exist, images that are not resources. A rejected
    // body is left out of the section; the record's other fields stay.
    std::vector<HelpEntryRejection> rejections{};

    // The build-time pass fails on any rejection; a run-time pass over a
    // user's file keeps what passed.
    [[nodiscard]] bool succeeded() const noexcept { return rejections.empty(); }
};

// The help pass over one language's catalog: validates every help body
// (markdown, topic links, images), resolves every record, generates the
// contents from `skeleton` and `topics`, and builds the keyword index, the
// search term table and the link table. A topic link may name any record in
// the catalog or any node of the skeleton.
[[nodiscard]] HelpBookCompileResult compile_helpbook_section(const HelpCatalog& catalog,
                                                             std::span<const HelpSkeletonNode> skeleton,
                                                             std::span<const HelpTopicPlacement> topics,
                                                             const HelpBookCompileOptions& options = {});

// The section's bytes (little-endian): the magic "GFHB", the format's major
// and minor version, then sub-sections, each a tag, its own version and its
// byte length. `bytes` is replaced.
void write_helpbook_section(const HelpBookSection& section, std::vector<std::byte>& bytes);

enum class HelpBookReadError : std::uint8_t {
    none,
    // The bytes do not start with the magic.
    not_a_section,
    // The section's major version is later than this reader's: the viewer
    // rebuilds the section from the records instead.
    later_major_version,
    // A length, count or index is out of range, a string is not UTF-8, or a
    // required sub-section is missing.
    malformed,
    // A count passes HelpBookReadLimits.
    limit_exceeded,
};

[[nodiscard]] std::string_view helpbook_read_error_name(HelpBookReadError error) noexcept;

struct HelpBookReadLimits final {
    std::size_t maximum_bytes{64U * 1024U * 1024U};
    std::size_t maximum_records{100000U};
    std::size_t maximum_entries{4U * 1024U * 1024U};
};

struct HelpBookReadResult final {
    HelpBookReadError error{HelpBookReadError::none};
    // The section on success; empty on failure.
    HelpBookSection section{};

    [[nodiscard]] explicit operator bool() const noexcept { return error == HelpBookReadError::none; }
};

// Reads bytes write_helpbook_section wrote. Every length, count and index is
// checked before use; unknown sub-sections are skipped; a later major
// version is refused with its own error. Expected faults never throw.
[[nodiscard]] HelpBookReadResult read_helpbook_section(std::span<const std::byte> bytes,
                                                       const HelpBookReadLimits& limits = {});

} // namespace gui_forms
