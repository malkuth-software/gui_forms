#include "gui_forms/help/help_page/help_page.hpp"

#include <stxlib/optional.hpp>
#include <algorithm>
#include <utility>

namespace gui_forms {
namespace {

enum class AttributeKind : std::uint8_t { callout, collapsible };

// One help attribute line found by the pre-pass: where its line ends in the
// source, and what it says. It applies to the first block that starts at or
// after `line_end`.
struct HelpAttributeLine final {
    std::size_t line_end{};
    AttributeKind kind{AttributeKind::callout};
    HelpCalloutKind callout{HelpCalloutKind::none};
    bool open{};
};

[[nodiscard]] std::string_view trim_line(std::string_view line) noexcept {
    std::size_t begin = 0U;
    std::size_t end = line.size();
    while (begin < end && (line[begin] == ' ' || line[begin] == '\t')) ++begin;
    while (end > begin && (line[end - 1U] == ' ' || line[end - 1U] == '\t' || line[end - 1U] == '\r')) --end;
    return line.substr(begin, end - begin);
}

// Whether `line` is one of the help dialect's attribute lines, filling
// `attribute` when it is.
[[nodiscard]] bool read_attribute_line(std::string_view line, HelpAttributeLine& attribute) noexcept {
    const std::string_view trimmed = trim_line(line);
    if (trimmed == "{callout=note}") {
        attribute.kind = AttributeKind::callout;
        attribute.callout = HelpCalloutKind::note;
        return true;
    }
    if (trimmed == "{callout=tip}") {
        attribute.kind = AttributeKind::callout;
        attribute.callout = HelpCalloutKind::tip;
        return true;
    }
    if (trimmed == "{callout=caution}") {
        attribute.kind = AttributeKind::callout;
        attribute.callout = HelpCalloutKind::caution;
        return true;
    }
    if (trimmed == "{collapsible}" || trimmed == "{collapsible open}") {
        attribute.kind = AttributeKind::collapsible;
        attribute.open = trimmed == "{collapsible open}";
        return true;
    }
    return false;
}

// A pipe table found by the pre-pass: where its first line starts in the
// source, and its rows' cell texts, the header row first. Its rows come
// before the first block that starts after `line_start`.
struct HelpTableLines final {
    std::size_t line_start{};
    std::vector<std::vector<std::string>> rows{};
};

// The lines of `body` the pre-pass reads: each line's start and end
// (without its line feed).
struct SourceLine final {
    std::size_t begin{};
    std::size_t end{};
};

// Whether `line` opens or closes a fenced code block.
[[nodiscard]] bool fence_line(std::string_view line) noexcept {
    const std::string_view trimmed = trim_line(line);
    return trimmed.substr(0U, 3U) == "```" || trimmed.substr(0U, 3U) == "~~~";
}

// Whether `line` is indented as code (four or more leading spaces).
[[nodiscard]] bool indented_code_line(std::string_view line) noexcept {
    std::size_t spaces = 0U;
    while (spaces < line.size() && line[spaces] == ' ') ++spaces;
    return spaces >= 4U || (!line.empty() && line[0] == '\t');
}

// Whether `line` is a pipe table's delimiter row: pipes, dashes, colons and
// spaces only, with a dash and a pipe.
[[nodiscard]] bool table_delimiter_line(std::string_view line) noexcept {
    const std::string_view trimmed = trim_line(line);
    bool dash = false;
    bool pipe = false;
    for (const char character : trimmed) {
        if (character == '-') {
            dash = true;
        } else if (character == '|') {
            pipe = true;
        } else if (character != ':' && character != ' ') {
            return false;
        }
    }
    return dash && pipe;
}

// Whether `line` holds a cell separator: a '|' outside a code span and not
// escaped.
[[nodiscard]] bool table_row_line(std::string_view line) noexcept {
    bool code = false;
    for (std::size_t index = 0U; index < line.size(); ++index) {
        if (line[index] == '\\') {
            ++index;
        } else if (line[index] == '`') {
            code = !code;
        } else if (line[index] == '|' && !code) {
            return true;
        }
    }
    return false;
}

// Splits a table row into its cells' texts, trimmed, the outer pipes
// dropped and each escaped pipe read as a pipe. `cells` is replaced.
void split_table_row(std::string_view line, std::vector<std::string>& cells) {
    cells.clear();
    std::string_view trimmed = trim_line(line);
    if (!trimmed.empty() && trimmed.front() == '|') trimmed.remove_prefix(1U);
    if (!trimmed.empty() && trimmed.back() == '|' && (trimmed.size() < 2U || trimmed[trimmed.size() - 2U] != '\\')) {
        trimmed.remove_suffix(1U);
    }
    std::string cell;
    bool code = false;
    for (std::size_t index = 0U; index < trimmed.size(); ++index) {
        const char character = trimmed[index];
        if (character == '\\' && index + 1U < trimmed.size() && trimmed[index + 1U] == '|') {
            cell.push_back('|');
            ++index;
            continue;
        }
        if (character == '`') code = !code;
        if (character == '|' && !code) {
            cells.push_back(std::string(trim_line(cell)));
            cell.clear();
            continue;
        }
        cell.push_back(character);
    }
    cells.push_back(std::string(trim_line(cell)));
}

// Copies `body` into `prepared` with each help attribute line and each pipe
// table's lines blanked to spaces (so every source offset stays where it
// was and the line reads as blank), recording the attribute lines in
// `attributes` and the tables in `tables`. Lines inside fenced or indented
// code are left alone.
void prepare_body(std::string_view body, std::string& prepared, std::vector<HelpAttributeLine>& attributes,
                  std::vector<HelpTableLines>& tables) {
    prepared.assign(body);
    attributes.clear();
    tables.clear();
    std::vector<SourceLine> lines;
    std::size_t begin = 0U;
    while (begin < body.size()) {
        std::size_t end = body.find('\n', begin);
        if (end == std::string_view::npos) end = body.size();
        lines.push_back(SourceLine{begin, end});
        begin = end + 1U;
    }
    bool fenced = false;
    std::vector<std::string> cells;
    std::size_t index = 0U;
    while (index < lines.size()) {
        const SourceLine line = lines[index];
        const std::string_view text = body.substr(line.begin, line.end - line.begin);
        if (fence_line(text)) fenced = !fenced;
        if (fenced || indented_code_line(text)) {
            ++index;
            continue;
        }
        HelpAttributeLine attribute{};
        if (read_attribute_line(text, attribute)) {
            for (std::size_t at = line.begin; at < line.end; ++at) {
                if (prepared[at] != '\r') prepared[at] = ' ';
            }
            attribute.line_end = line.end;
            attributes.push_back(attribute);
            ++index;
            continue;
        }
        const bool table_starts = table_row_line(text) && index + 1U < lines.size() &&
                                  table_delimiter_line(body.substr(lines[index + 1U].begin,
                                                                   lines[index + 1U].end - lines[index + 1U].begin));
        if (!table_starts) {
            ++index;
            continue;
        }
        HelpTableLines table{};
        table.line_start = line.begin;
        split_table_row(text, cells);
        table.rows.push_back(cells);
        std::size_t last = index + 1U;
        while (last + 1U < lines.size()) {
            const SourceLine row = lines[last + 1U];
            const std::string_view row_text = body.substr(row.begin, row.end - row.begin);
            if (trim_line(row_text).empty() || !table_row_line(row_text)) break;
            split_table_row(row_text, cells);
            table.rows.push_back(cells);
            ++last;
        }
        for (std::size_t at = line.begin; at < lines[last].end; ++at) {
            if (prepared[at] != '\r' && prepared[at] != '\n') prepared[at] = ' ';
        }
        tables.push_back(std::move(table));
        index = last + 1U;
    }
}

// Appends the runs of `text`, read as inline markdown, to `page`, and its
// links to the page's links. An unreadable cell is kept as plain text.
void append_inline_runs(std::string_view text, HelpPageModel& page, const MarkdownLimits& limits) {
    if (text.empty()) return;
    MarkdownDocument document{};
    const MarkdownParseResult parsed = parse_markdown(text, document, limits);
    if (!parsed.succeeded()) {
        HelpPageRun run{};
        run.text = std::string(text);
        page.runs.push_back(std::move(run));
        return;
    }
    const std::size_t first_link = page.links.size();
    for (const MarkdownLink& link : document.links) {
        const HelpLinkTarget target = classify_help_link(link.target, link.title);
        page.links.push_back(HelpPageLink{target.kind, target.target, target.fragment, {}});
    }
    for (const MarkdownBlock& block : document.blocks) {
        for (std::size_t index = 0U; index < block.span_count; ++index) {
            const MarkdownSpan& span = document.spans[block.first_span + index];
            HelpPageRun run{};
            run.text = span.text;
            run.bold = span.style.bold;
            run.italic = span.style.italic;
            run.code = span.style.code;
            run.underline = span.style.underline;
            run.strikethrough = span.style.strikethrough;
            run.line_break = span.kind == MarkdownSpanKind::line_break;
            const std::size_t* const link = span.link.value();
            if (link != nullptr) run.link.set(first_link + *link);
            page.runs.push_back(std::move(run));
        }
    }
}

// Appends `table`'s rows to `page` as table rows in `section`, the first a
// header row.
void append_table(const HelpTableLines& table, stxlib::Optional<std::size_t> section, HelpPageModel& page,
                  const MarkdownLimits& limits) {
    for (std::size_t row = 0U; row < table.rows.size(); ++row) {
        HelpPageBlock block{};
        block.kind = HelpPageBlockKind::table_row;
        block.header = row == 0U;
        block.section = section;
        block.first_run = page.runs.size();
        block.first_cell = page.cells.size();
        for (const std::string& text : table.rows[row]) {
            HelpPageCell cell{};
            cell.first_run = page.runs.size();
            append_inline_runs(text, page, limits);
            cell.run_count = page.runs.size() - cell.first_run;
            page.cells.push_back(cell);
        }
        block.cell_count = page.cells.size() - block.first_cell;
        page.blocks.push_back(std::move(block));
    }
}

[[nodiscard]] HelpPageBlockKind page_kind(MarkdownBlockKind kind) noexcept {
    switch (kind) {
    case MarkdownBlockKind::paragraph:
        return HelpPageBlockKind::paragraph;
    case MarkdownBlockKind::heading:
        return HelpPageBlockKind::heading;
    case MarkdownBlockKind::list_item:
        return HelpPageBlockKind::list_item;
    case MarkdownBlockKind::code_block:
        return HelpPageBlockKind::code;
    case MarkdownBlockKind::rule:
        return HelpPageBlockKind::rule;
    }
    return HelpPageBlockKind::paragraph;
}

// An open collapsible section while the blocks are read.
struct OpenSection final {
    std::size_t section{};
    std::size_t level{};
};

void append_line_entries(const HelpBookSection& section, std::size_t record, HelpPageModel& page) {
    const std::uint32_t node = section.records[record].contents_node;
    if (node == help_no_parent) return;
    for (const HelpContentsLine& line : section.contents.lines) {
        if (line.page != node) continue;
        HelpPageBlock block{};
        block.kind = HelpPageBlockKind::list_item;
        block.anchor = line.stable_id;
        block.first_run = page.runs.size();
        HelpPageRun title{};
        title.text = line.title;
        title.bold = true;
        page.runs.push_back(std::move(title));
        HelpPageRun text{};
        text.text = ": ";
        text.text.append(line.help_line);
        page.runs.push_back(std::move(text));
        block.run_count = page.runs.size() - block.first_run;
        page.blocks.push_back(std::move(block));
    }
}

} // namespace

std::string_view help_callout_name(HelpCalloutKind kind) noexcept {
    switch (kind) {
    case HelpCalloutKind::none:
        return "none";
    case HelpCalloutKind::note:
        return "note";
    case HelpCalloutKind::tip:
        return "tip";
    case HelpCalloutKind::caution:
        return "caution";
    }
    return {};
}

stxlib::Optional<std::size_t> HelpPageModel::find_anchor(std::string_view anchor) const noexcept {
    if (anchor.empty()) return stxlib::Optional<std::size_t>::none();
    for (std::size_t index = 0U; index < blocks.size(); ++index) {
        if (blocks[index].anchor == anchor) return stxlib::Optional<std::size_t>::of(index);
    }
    return stxlib::Optional<std::size_t>::none();
}

void HelpPageModel::clear() noexcept {
    stable_id.clear();
    title.clear();
    fallback = false;
    blocks.clear();
    runs.clear();
    cells.clear();
    links.clear();
    images.clear();
    sections.clear();
}

void help_heading_anchor(std::string_view heading, std::string& anchor) {
    anchor.clear();
    bool pending_dash = false;
    for (const char character : heading) {
        const unsigned char byte = static_cast<unsigned char>(character);
        const bool letter = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z');
        const bool kept = letter || (byte >= '0' && byte <= '9') || byte >= 0x80U;
        if (!kept) {
            pending_dash = !anchor.empty();
            continue;
        }
        if (pending_dash) anchor.push_back('-');
        pending_dash = false;
        if (byte >= 'A' && byte <= 'Z') {
            anchor.push_back(static_cast<char>(byte - 'A' + 'a'));
        } else {
            anchor.push_back(character);
        }
    }
}

MarkdownParseResult build_help_page(std::string_view stable_id, std::string_view title, std::string_view body,
                                    HelpPageModel& page, const MarkdownLimits& limits) {
    page.clear();
    page.stable_id = std::string(stable_id);
    page.title = std::string(title);
    std::string prepared;
    std::vector<HelpAttributeLine> attributes;
    std::vector<HelpTableLines> tables;
    prepare_body(body, prepared, attributes, tables);
    MarkdownDocument document{};
    const MarkdownParseResult parsed = parse_markdown(prepared, document, limits);
    if (!parsed.succeeded()) return parsed;

    page.links.reserve(document.links.size());
    for (const MarkdownLink& link : document.links) {
        const HelpLinkTarget target = classify_help_link(link.target, link.title);
        page.links.push_back(HelpPageLink{target.kind, target.target, target.fragment, {}});
    }
    page.images.reserve(document.images.size());
    for (const MarkdownImage& image : document.images) page.images.push_back(HelpPageImage{image.source, {}});

    std::size_t next_attribute = 0U;
    std::size_t next_table = 0U;
    std::vector<OpenSection> open_sections;
    std::string heading_text;
    for (const MarkdownBlock& source_block : document.blocks) {
        // The tables that stand before this block.
        while (next_table < tables.size() && tables[next_table].line_start < source_block.source.start.value()) {
            const stxlib::Optional<std::size_t> section = open_sections.empty()
                                                              ? stxlib::Optional<std::size_t>::none()
                                                              : stxlib::Optional<std::size_t>::of(open_sections.back().section);
            append_table(tables[next_table], section, page, limits);
            ++next_table;
        }
        HelpPageBlock block{};
        block.kind = page_kind(source_block.kind);
        block.level = source_block.level;
        block.quote_depth = source_block.quote_depth;
        block.marker = source_block.marker;
        block.ordinal = source_block.ordinal;
        block.alignment = source_block.alignment;
        HelpCalloutKind callout = HelpCalloutKind::none;
        std::optional<bool> collapsible{};
        while (next_attribute < attributes.size() &&
               attributes[next_attribute].line_end <= source_block.source.start.value()) {
            const HelpAttributeLine& attribute = attributes[next_attribute];
            if (attribute.kind == AttributeKind::callout) {
                callout = attribute.callout;
            } else {
                collapsible = attribute.open;
            }
            ++next_attribute;
        }
        block.callout = callout;
        const std::size_t block_index = page.blocks.size();
        if (block.kind == HelpPageBlockKind::heading) {
            while (!open_sections.empty() && open_sections.back().level >= block.level) {
                page.sections[open_sections.back().section].end = block_index;
                open_sections.pop_back();
            }
        }
        if (!open_sections.empty()) block.section.set(open_sections.back().section);
        // The runs, and whether the block is one image and nothing else.
        block.first_run = page.runs.size();
        stxlib::Optional<std::size_t> only_image{};
        bool image_only = source_block.span_count > 0U;
        heading_text.clear();
        for (std::size_t index = 0U; index < source_block.span_count; ++index) {
            const MarkdownSpan& span = document.spans[source_block.first_span + index];
            if (span.image.present()) {
                page.images[(*span.image.value())].alternative.append(span.text);
                if (only_image.present() && (*only_image.value()) != (*span.image.value())) image_only = false;
                only_image = span.image;
            } else if (span.kind != MarkdownSpanKind::text || !trim_line(span.text).empty()) {
                image_only = false;
            }
            HelpPageRun run{};
            run.text = span.text;
            run.bold = span.style.bold;
            run.italic = span.style.italic;
            run.code = span.style.code;
            run.underline = span.style.underline;
            run.strikethrough = span.style.strikethrough;
            run.line_break = span.kind == MarkdownSpanKind::line_break;
            run.link = span.link;
            heading_text.append(span.text);
            page.runs.push_back(std::move(run));
        }
        block.run_count = page.runs.size() - block.first_run;
        if (image_only && only_image.present() && block.kind == HelpPageBlockKind::paragraph) {
            block.kind = HelpPageBlockKind::image;
            block.image = only_image;
        }
        if (block.kind == HelpPageBlockKind::heading) {
            help_heading_anchor(heading_text, block.anchor);
            if (collapsible.has_value()) {
                page.sections.push_back(HelpPageSection{block_index, block_index + 1U, *collapsible});
                open_sections.push_back(OpenSection{page.sections.size() - 1U, block.level});
            }
        }
        page.blocks.push_back(std::move(block));
    }
    while (next_table < tables.size()) {
        const stxlib::Optional<std::size_t> section = open_sections.empty()
                                                          ? stxlib::Optional<std::size_t>::none()
                                                          : stxlib::Optional<std::size_t>::of(open_sections.back().section);
        append_table(tables[next_table], section, page, limits);
        ++next_table;
    }
    for (const OpenSection& open : open_sections) page.sections[open.section].end = page.blocks.size();
    return parsed;
}

MarkdownParseResult build_help_page(const HelpBookSection& section, std::size_t record, HelpPageModel& page,
                                    const MarkdownLimits& limits) {
    const HelpBookRecord& source = section.records[record];
    const MarkdownParseResult parsed = build_help_page(source.stable_id, source.title, source.help, page, limits);
    page.fallback = source.fallback;
    if (!parsed.succeeded()) return parsed;
    for (HelpPageLink& link : page.links) {
        if (link.kind != HelpLinkKind::topic && link.kind != HelpLinkKind::popup) continue;
        const stxlib::Optional<std::size_t> target = section.find(link.target);
        if (target.present()) link.target_title = section.records[(*target.value())].title;
    }
    append_line_entries(section, record, page);
    return parsed;
}

} // namespace gui_forms
