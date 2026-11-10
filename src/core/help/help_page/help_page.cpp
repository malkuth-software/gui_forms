#include "gui_forms/help/help_page/help_page.hpp"

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

// Copies `body` into `prepared` with each help attribute line blanked to
// spaces (so every source offset stays where it was and the line reads as
// blank), recording the lines in `attributes`.
void prepare_body(std::string_view body, std::string& prepared, std::vector<HelpAttributeLine>& attributes) {
    prepared.assign(body);
    attributes.clear();
    std::size_t begin = 0U;
    while (begin < body.size()) {
        std::size_t end = body.find('\n', begin);
        if (end == std::string_view::npos) end = body.size();
        HelpAttributeLine attribute{};
        if (read_attribute_line(body.substr(begin, end - begin), attribute)) {
            for (std::size_t index = begin; index < end; ++index) {
                if (prepared[index] != '\r') prepared[index] = ' ';
            }
            attribute.line_end = end;
            attributes.push_back(attribute);
        }
        begin = end + 1U;
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

std::optional<std::size_t> HelpPageModel::find_anchor(std::string_view anchor) const noexcept {
    if (anchor.empty()) return std::nullopt;
    for (std::size_t index = 0U; index < blocks.size(); ++index) {
        if (blocks[index].anchor == anchor) return index;
    }
    return std::nullopt;
}

void HelpPageModel::clear() noexcept {
    stable_id.clear();
    title.clear();
    fallback = false;
    blocks.clear();
    runs.clear();
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
    prepare_body(body, prepared, attributes);
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
    std::vector<OpenSection> open_sections;
    std::string heading_text;
    for (const MarkdownBlock& source_block : document.blocks) {
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
        if (!open_sections.empty()) block.section = open_sections.back().section;
        // The runs, and whether the block is one image and nothing else.
        block.first_run = page.runs.size();
        std::optional<std::size_t> only_image{};
        bool image_only = source_block.span_count > 0U;
        heading_text.clear();
        for (std::size_t index = 0U; index < source_block.span_count; ++index) {
            const MarkdownSpan& span = document.spans[source_block.first_span + index];
            if (span.image.has_value()) {
                page.images[*span.image].alternative.append(span.text);
                if (only_image.has_value() && *only_image != *span.image) image_only = false;
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
        if (image_only && only_image.has_value() && block.kind == HelpPageBlockKind::paragraph) {
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
        const std::optional<std::size_t> target = section.find(link.target);
        if (target.has_value()) link.target_title = section.records[*target].title;
    }
    append_line_entries(section, record, page);
    return parsed;
}

} // namespace gui_forms
