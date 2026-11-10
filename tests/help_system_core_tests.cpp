// The help system's headless core (help system study, revision 1): the
// help fields in the locale XML format and their layers, the generated
// contents, stand-alone topics, the helpbook section compile (broken links,
// missing images, markdown faults), its binary round trip and version
// refusal, search v1 (with a 2,000-record measurement) and the page model.

#include "gui_forms/gui_forms.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

constexpr std::string_view built_in_locale =
    "<locale xml:lang=\"en\">\n"
    "  <controls>\n"
    "    <control name=\"main\" text=\"Main window\" help-line=\"The program's window.\">\n"
    "      <help keywords=\"window; start\">\n"
    "        The main window holds the document.\n"
    "\n"
    "        See [Paste](main.paste) and [the guide](guide.start).\n"
    "      </help>\n"
    "      <control name=\"paste\" text=\"Paste\" help-line=\"Pastes the clipboard.\"/>\n"
    "      <control name=\"copy\" text=\"Copy\" help-line=\"Copies the selection.\">\n"
    "        <help keywords=\"clipboard, copy; selection\">Copies the **selection** to the clipboard.</help>\n"
    "      </control>\n"
    "    </control>\n"
    "  </controls>\n"
    "</locale>\n";

constexpr std::string_view french_locale =
    "<locale xml:lang=\"fr\">\n"
    "  <controls>\n"
    "    <control name=\"main\">\n"
    "      <control name=\"copy\" text=\"Copier\"/>\n"
    "    </control>\n"
    "  </controls>\n"
    "</locale>\n";

// Declared topics join the contents wherever they say.
const HelpTopic guide_topic(StableId(StableIdLiteral("guide.start")), "Getting started",
                            "Start here.\n\n{callout=tip}\nPress F1 anywhere.\n", HelpPlace::top(), "",
                            "start; guide");

struct Program final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<Button> paste;
    std::shared_ptr<Button> copy;
    std::unique_ptr<Window> window;
};

Program make_program() {
    Program program;
    program.root = make_control<Panel>(StableId("main"));
    program.paste = make_control<Button>(StableId("main.paste"), "Paste");
    program.copy = make_control<Button>(StableId("main.copy"), "Copy");
    (*program.root).add_child(program.paste);
    (*program.root).add_child(program.copy);
    program.window = std::make_unique<Window>(program.root, Size{640.0, 480.0});
    (*program.window).perform_layout();
    return program;
}

void records_and_layers() {
    const HelpLocaleReadResult read = read_help_locale(built_in_locale);
    require(static_cast<bool>(read) && read.language == "en" && read.rejections.empty(), "the built-in file reads");
    require(read.records.size() == 3U, "three records: main, main.paste, main.copy");
    const HelpRecordEntry* copy = read.records.find("main.copy");
    require(copy != nullptr && (*copy).title == std::optional<std::string>("Copy") &&
                (*copy).help.has_value() && (*(*copy).help).find("**selection**") != std::string::npos,
            "the copy record keeps its title and body");
    const HelpRecordEntry* main_record = read.records.find("main");
    require(main_record != nullptr && (*main_record).help.has_value() &&
                (*(*main_record).help).rfind("The main window", 0) == 0,
            "a help body is dedented");
    HelpCatalog catalog;
    catalog.built_in().merge_over(read.records);
    const HelpLocaleReadResult french = read_help_locale(french_locale);
    require(static_cast<bool>(french), "the translation reads");
    catalog.set_translation(french.records, french.language);
    const HelpResolvedRecord resolved = catalog.resolve("main.copy");
    require(resolved.found && resolved.title.text == "Copier" && resolved.title.origin == HelpLayer::translation,
            "the translation's title wins");
    require(resolved.help_line.text == "Copies the selection." && resolved.help_line.fallback,
            "an untranslated field falls back to the built-in text, marked");
    catalog.set_built_in_only(true);
    require(catalog.resolve("main.copy").title.text == "Copy", "the way back shows the built-in language");
    catalog.set_built_in_only(false);
    // The template export reads back to the same fields.
    const XmlWriteResult exported = write_help_locale(catalog.layer(HelpLayer::built_in), "en");
    require(static_cast<bool>(exported), "the template exports");
    const HelpLocaleReadResult reread = read_help_locale(exported.text);
    require(static_cast<bool>(reread) && reread.records.size() == 3U, "the template reads back");
    const HelpRecordEntry* reread_copy = reread.records.find("main.copy");
    require(reread_copy != nullptr && (*reread_copy).help == (*copy).help &&
                (*reread_copy).keywords == (*copy).keywords,
            "the round trip keeps bodies and keywords");
    // A bad stable ID segment is rejected alone.
    const HelpLocaleReadResult bad = read_help_locale(
        "<locale xml:lang=\"en\"><controls><control name=\"Bad Name\" text=\"x\"/>"
        "<control name=\"good\" text=\"Good\"/></controls></locale>");
    require(static_cast<bool>(bad) && bad.records.size() == 1U && !bad.rejections.empty() &&
                bad.rejections.front().reason == HelpEntryRefusal::invalid_stable_id,
            "an invalid segment is rejected and the rest kept");
    std::vector<HelpKeyword> keywords;
    parse_help_keywords("clipboard, copy; selection", keywords);
    require(keywords.size() == 2U && keywords[0].keyword == "clipboard" && keywords[0].subkeyword == "copy" &&
                keywords[1].keyword == "selection",
            "keywords split on ';' and a subkeyword after ','");
}

HelpCatalog make_catalog() {
    HelpCatalog catalog;
    const HelpLocaleReadResult read = read_help_locale(built_in_locale);
    catalog.built_in().merge_over(read.records);
    put_declared_topics(catalog.built_in());
    return catalog;
}

void contents_and_compile() {
    Program program = make_program();
    std::vector<HelpSkeletonNode> skeleton;
    append_help_skeleton(*program.window, skeleton);
    require(skeleton.size() >= 3U && skeleton.front().stable_id == "main", "the skeleton walks the window");
    std::vector<HelpTopicPlacement> topics;
    collect_declared_topics(topics);
    require(!topics.empty(), "the declared topic is collected");
    const HelpCatalog catalog = make_catalog();
    const HelpBookCompileResult compiled = compile_helpbook_section(catalog, skeleton, topics);
    require(compiled.succeeded(), "the section compiles");
    const HelpBookSection& section = compiled.section;
    // Contents: the topic at the top, then main with copy under it; paste
    // has no body, so it is a line on main's page.
    require(section.contents.nodes.size() == 3U, "three pages: the guide, main, main.copy");
    require(section.contents.nodes[0].stable_id == "guide.start" && section.contents.nodes[0].topic,
            "a top topic comes first");
    // HelpContentsTree::find answers help_no_parent for an absent node.
    const std::uint32_t main_node = section.contents.find("main");
    const std::uint32_t copy_node = section.contents.find("main.copy");
    require(main_node != help_no_parent && copy_node != help_no_parent &&
                section.contents.nodes[copy_node].parent == main_node,
            "copy is under main");
    require(section.contents.find("main.paste") == help_no_parent, "a control without a body has no page");
    bool paste_line = false;
    for (const HelpContentsLine& line : section.contents.lines) {
        if (line.stable_id == "main.paste" && line.page == main_node && line.help_line == "Pastes the clipboard.")
            paste_line = true;
    }
    require(paste_line, "its help line is listed on the parent's page");
    // Keywords: sorted, folded, with subkeywords.
    require(!section.keywords.empty(), "the keyword index is built");
    for (std::size_t index = 1U; index < section.keywords.size(); ++index) {
        require(section.keywords[index - 1U].folded <= section.keywords[index].folded, "keywords are sorted");
    }
    // Round trip through the binary section.
    std::vector<std::byte> bytes;
    write_helpbook_section(section, bytes);
    const HelpBookReadResult read = read_helpbook_section(bytes);
    require(static_cast<bool>(read), "the section reads back");
    require(read.section.records.size() == section.records.size() &&
                read.section.contents.nodes.size() == section.contents.nodes.size() &&
                read.section.keywords.size() == section.keywords.size() &&
                read.section.terms.size() == section.terms.size() &&
                read.section.postings.size() == section.postings.size(),
            "every table survives the round trip");
    for (std::size_t index = 0U; index < section.records.size(); ++index) {
        require(read.section.records[index].stable_id == section.records[index].stable_id &&
                    read.section.records[index].help == section.records[index].help,
                "records survive the round trip");
    }
    std::vector<std::byte> later = bytes;
    later[4] = std::byte{2};
    later[5] = std::byte{0};
    require(read_helpbook_section(later).error == HelpBookReadError::later_major_version,
            "a later major version is refused");
    std::vector<std::byte> cut(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(bytes.size() / 2U));
    require(!read_helpbook_section(cut), "a truncated section is refused");
    require(read_helpbook_section(std::span<const std::byte>{}).error == HelpBookReadError::not_a_section,
            "empty bytes are not a section");
}

void broken_links() {
    HelpCatalog catalog;
    HelpRecordEntry entry{};
    entry.stable_id = "page";
    entry.title = std::string("Page");
    entry.help = std::string("See [missing](no.such.page) and ![logo](logo.png).");
    catalog.built_in().put(entry);
    HelpRecordEntry fine{};
    fine.stable_id = "fine";
    fine.title = std::string("Fine");
    fine.help = std::string("Visit [the site](https://example.org).");
    catalog.built_in().put(fine);
    const HelpBookCompileResult compiled = compile_helpbook_section(catalog, {}, {});
    bool broken = false;
    bool missing = false;
    for (const HelpEntryRejection& rejection : compiled.rejections) {
        if (rejection.stable_id == "page" && rejection.reason == HelpEntryRefusal::broken_link) broken = true;
        if (rejection.stable_id == "page" && rejection.reason == HelpEntryRefusal::missing_image) missing = true;
    }
    require(broken && missing, "a broken topic link and a missing image are reported");
    require(!compiled.succeeded(), "a section with rejections does not succeed");
    require(compiled.section.find("fine").has_value(), "the good record compiles anyway");
    bool external = false;
    for (const HelpLinkEntry& link : compiled.section.links) {
        if (link.kind == HelpLinkKind::external && link.target == "https://example.org") external = true;
    }
    require(external, "an external link is recorded as external");
    const std::vector<std::string> images{"logo.png"};
    HelpBookCompileOptions options{};
    options.image_names = images;
    const HelpBookCompileResult with_image = compile_helpbook_section(catalog, {}, {}, options);
    for (const HelpEntryRejection& rejection : with_image.rejections) {
        require(rejection.reason != HelpEntryRefusal::missing_image, "a named PNG resource is found");
    }
}

void search() {
    const HelpCatalog catalog = make_catalog();
    const HelpBookCompileResult compiled = compile_helpbook_section(catalog, {}, {});
    const HelpBookSection& section = compiled.section;
    HelpSearchQuery query;
    std::vector<HelpSearchHit> hits;
    parse_help_query("clipboard", query);
    require(search_help(section, query, {}, hits) == HelpSearchStatus::complete && hits.size() == 1U &&
                section.records[hits[0].record].stable_id == "main.copy",
            "a word finds its record");
    parse_help_query("CLIP", query);
    require(search_help(section, query, {}, hits) == HelpSearchStatus::complete && hits.size() == 1U,
            "the last word is a case-folded prefix");
    parse_help_query("\"main window\"", query);
    require(search_help(section, query, {}, hits) == HelpSearchStatus::complete && !hits.empty() &&
                section.records[hits[0].record].stable_id == "main",
            "a phrase matches consecutive words");
    parse_help_query("copy", query);
    HelpSearchOptions titles{};
    titles.titles_only = true;
    require(search_help(section, query, titles, hits) == HelpSearchStatus::complete && hits.size() == 1U &&
                hits[0].title_hit,
            "titles only searches titles");
    parse_help_query("   ", query);
    require(search_help(section, query, {}, hits) == HelpSearchStatus::empty_query, "no word, no search");

    // The measurement: 2,000 records of 60 words each.
    HelpCatalog large;
    std::string body;
    for (std::size_t index = 0U; index < 2000U; ++index) {
        HelpRecordEntry entry{};
        entry.stable_id = "topic" + std::to_string(index);
        entry.title = "Topic " + std::to_string(index);
        body.clear();
        for (std::size_t word = 0U; word < 60U; ++word) {
            body.append("word");
            body.append(std::to_string((index * 7U + word * 13U) % 997U));
            body.push_back(' ');
        }
        entry.help = body;
        large.built_in().put(std::move(entry));
    }
    const std::chrono::steady_clock::time_point compile_start = std::chrono::steady_clock::now();
    const HelpBookCompileResult large_compiled = compile_helpbook_section(large, {}, {});
    const std::chrono::steady_clock::time_point compile_end = std::chrono::steady_clock::now();
    require(large_compiled.succeeded() && large_compiled.section.records.size() == 2000U,
            "2,000 records compile");
    parse_help_query("word13 word2", query);
    const std::chrono::steady_clock::time_point search_start = std::chrono::steady_clock::now();
    const std::size_t rounds = 100U;
    HelpSearchStatus status = HelpSearchStatus::empty_query;
    for (std::size_t round = 0U; round < rounds; ++round) status = search_help(large_compiled.section, query, {}, hits);
    const std::chrono::steady_clock::time_point search_end = std::chrono::steady_clock::now();
    require(status == HelpSearchStatus::complete, "the large search completes within its budget");
    const double compile_ms = std::chrono::duration<double, std::milli>(compile_end - compile_start).count();
    const double search_us =
        std::chrono::duration<double, std::micro>(search_end - search_start).count() / static_cast<double>(rounds);
    std::cout << "help search measurement: 2000 records x 60 words; compile " << compile_ms << " ms; "
              << "two-word query (prefix last) " << search_us << " us mean over " << rounds << " runs; "
              << hits.size() << " hits\n";
}

void page_model() {
    HelpPageModel page;
    const MarkdownParseResult parsed = build_help_page(
        "p", "Title",
        "Intro with [a link](other.page \"popup\").\n\n"
        "{callout=caution}\n"
        "Careful.\n\n"
        "{collapsible}\n"
        "## Details\n\n"
        "Hidden text.\n\n"
        "## Open part\n\n"
        "![Logo](logo.png)\n",
        page);
    require(parsed.succeeded(), "the page parses");
    require(page.links.size() == 1U && page.links[0].kind == HelpLinkKind::popup &&
                page.links[0].target == "other.page",
            "a link titled popup is a popup link");
    bool caution = false;
    for (const HelpPageBlock& block : page.blocks) {
        if (block.callout == HelpCalloutKind::caution) caution = true;
    }
    require(caution, "the callout attribute marks the next block");
    require(page.sections.size() == 1U && !page.sections[0].initially_open, "the collapsible section starts closed");
    const std::optional<std::size_t> details = page.find_anchor("details");
    require(details.has_value() && page.sections[0].heading == *details, "a heading's anchor is its folded text");
    require(page.blocks[*details + 1U].section == std::optional<std::size_t>(0U), "the body is in the section");
    const std::optional<std::size_t> open_part = page.find_anchor("open-part");
    require(open_part.has_value() && !page.blocks[*open_part].section.has_value(),
            "a heading of the same level ends the section");
    require(page.blocks.back().kind == HelpPageBlockKind::image && page.images.size() == 1U &&
                page.images[0].source == "logo.png" && page.images[0].alternative == "Logo",
            "an image alone in a paragraph is an image block");
    std::string anchor;
    help_heading_anchor("  Hello, World 2! ", anchor);
    require(anchor == "hello-world-2", "anchors fold and join with dashes");
}

} // namespace

int main() {
    records_and_layers();
    contents_and_compile();
    broken_links();
    search();
    page_model();
    std::cout << "help system core tests passed\n";
    return 0;
}
