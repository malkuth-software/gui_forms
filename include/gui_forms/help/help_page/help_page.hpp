#pragma once

#include "gui_forms/help/helpbook_section/helpbook_section.hpp"
#include "gui_forms/text/markdown/markdown.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// One help page as the paper pane shows and prints it (help system study,
// section 3): the record's title and its body read as markdown, with the
// help dialect's block attribute lines.
//
// - `{callout=note}`, `{callout=tip}` and `{callout=caution}` alone on a line
//   make the next block a callout on darker paper.
// - `{collapsible}` alone on a line before a heading folds the heading's
//   section (up to the next heading of the same or a higher level) under a
//   topic bar, closed; `{collapsible open}` starts it open.
//
// Other block attribute lines are the markdown engine's ({align=...}).

enum class HelpCalloutKind : std::uint8_t { none, note, tip, caution };

[[nodiscard]] std::string_view help_callout_name(HelpCalloutKind kind) noexcept;

enum class HelpPageBlockKind : std::uint8_t { paragraph, heading, list_item, code, rule, image };

// A piece of a block's text in one style. `link` indexes the page's links.
struct HelpPageRun final {
    std::string text{};
    bool bold{};
    bool italic{};
    bool code{};
    bool underline{};
    bool strikethrough{};
    bool line_break{};
    std::optional<std::size_t> link{};
};

// One block. `level` is a heading's level (1 to 6) or a list item's
// nesting. `section` is the innermost collapsible section the block lies in
// (a section's heading is not in its own section). `anchor` is a heading's
// fragment name (help_heading_anchor), or a listed control's stable ID for a
// help-line entry, so a link or F1 can scroll to it. `image` indexes the
// page's images for an image block.
struct HelpPageBlock final {
    HelpPageBlockKind kind{HelpPageBlockKind::paragraph};
    std::size_t level{};
    std::size_t quote_depth{};
    MarkdownListMarker marker{MarkdownListMarker::bullet};
    std::uint32_t ordinal{};
    MarkdownAlignment alignment{MarkdownAlignment::left};
    HelpCalloutKind callout{HelpCalloutKind::none};
    std::optional<std::size_t> section{};
    std::string anchor{};
    std::optional<std::size_t> image{};
    // The block's runs: HelpPageModel::runs from first_run, run_count of them.
    std::size_t first_run{};
    std::size_t run_count{};
};

// A collapsible section: its heading block, the first block after it, and
// whether it starts open.
struct HelpPageSection final {
    std::size_t heading{};
    std::size_t end{};
    bool initially_open{};
};

struct HelpPageLink final {
    HelpLinkKind kind{HelpLinkKind::external};
    std::string target{};
    std::string fragment{};
    // A topic or popup link's target title, when the page was built from a
    // section: printing shows it in brackets after the link.
    std::string target_title{};
};

struct HelpPageImage final {
    std::string source{};
    std::string alternative{};
};

struct HelpPageModel final {
    std::string stable_id{};
    std::string title{};
    // A field came from the built-in language while a translation is in
    // force: the page marks it.
    bool fallback{};
    std::vector<HelpPageBlock> blocks{};
    std::vector<HelpPageRun> runs{};
    std::vector<HelpPageLink> links{};
    std::vector<HelpPageImage> images{};
    std::vector<HelpPageSection> sections{};

    // The block whose anchor is `anchor`, or none.
    [[nodiscard]] std::optional<std::size_t> find_anchor(std::string_view anchor) const noexcept;
    // Ends every element's lifetime; capacity remains.
    void clear() noexcept;
};

// A heading's fragment name: ASCII letters folded to lower case, digits
// and non-ASCII bytes kept, every other run of bytes one '-', with no '-'
// at either end. `anchor` is replaced.
void help_heading_anchor(std::string_view heading, std::string& anchor);

// Builds `page` from a record's title and body. On a markdown failure the
// page holds the title only and the result says why. `page` is replaced.
[[nodiscard]] MarkdownParseResult build_help_page(std::string_view stable_id, std::string_view title,
                                                  std::string_view body, HelpPageModel& page,
                                                  const MarkdownLimits& limits = {});

// Builds the page of record `record` of `section`: its body, then one list
// entry per control listed on the page (HelpContentsLine), with the
// control's title in bold and its help line, anchored at its stable ID.
[[nodiscard]] MarkdownParseResult build_help_page(const HelpBookSection& section, std::size_t record,
                                                  HelpPageModel& page, const MarkdownLimits& limits = {});

} // namespace gui_forms
