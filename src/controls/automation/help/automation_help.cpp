#include "automation_help.hpp"

#include "../../../core/json/json_reader/json_reader.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::automation {
namespace {

// docs/user/talk-to-a-program.md, embedded at build time
// (cmake/embed_text_file.cmake).
namespace talk_document {
#include "talk_to_a_program.inc"
} // namespace talk_document

// docs/user/index.md, the user guide's table of contents.
namespace guide_index {
#include "user_guide_index.inc"
} // namespace guide_index

// Where the user guide is published; a page `name` is at
// <published_guide><name>.html.
constexpr std::string_view published_guide = "https://gui-forms.readthedocs.io/en/latest/user/";

std::string_view embedded_text(const unsigned char* bytes, std::size_t size) noexcept {
    // The array ends with a zero the text does not include.
    const std::string_view text(reinterpret_cast<const char*>(bytes), size - 1U);
    return text;
}

std::string_view talk_text() noexcept {
    return embedded_text(talk_document::embedded_source_bytes, sizeof(talk_document::embedded_source_bytes));
}

std::string_view index_text() noexcept {
    return embedded_text(guide_index::embedded_source_bytes, sizeof(guide_index::embedded_source_bytes));
}

// A topic answered by one section of talk-to-a-program.md, by its heading.
struct SectionTopic final {
    std::string_view topic;
    std::string_view heading;
};

constexpr SectionTopic section_topics[] = {
    {"socket", "Find the socket"},
    {"console", "The console"},
    {"folder", "The command folder"},
    {"json", "MCP"},
    {"stdio", "A client that wants a stdio server"},
    {"connections", "Connections"},
    {"control", "Control"},
    {"disconnect", "Disconnect"},
    {"help", "Help"},
};

std::string_view trimmed(std::string_view text) noexcept {
    std::size_t first = 0U;
    while (first < text.size() && (text[first] == ' ' || text[first] == '\t' || text[first] == '\r')) ++first;
    std::size_t last = text.size();
    while (last > first && (text[last - 1U] == ' ' || text[last - 1U] == '\t' || text[last - 1U] == '\r')) --last;
    return text.substr(first, last - first);
}

std::string lowered(std::string_view text) {
    std::string result(text);
    for (char& character : result) {
        if (character >= 'A' && character <= 'Z') character = static_cast<char>(character - 'A' + 'a');
    }
    return result;
}

// A link's address as the help shows it: a page of the guide by its
// published address, anything else as written.
std::string link_address(std::string_view target) {
    const std::size_t hash = target.find('#');
    const std::string_view page = target.substr(0U, hash);
    const bool guide_page = page.size() > 3U && page.substr(page.size() - 3U) == ".md" &&
                            page.find('/') == std::string_view::npos;
    if (!guide_page) return std::string(target);
    std::string address(published_guide);
    address += page.substr(0U, page.size() - 3U);
    address += ".html";
    if (hash != std::string_view::npos) address += target.substr(hash);
    return address;
}

// One line of Markdown as plain text: a link [text](target) becomes
// "text <address>".
std::string plain_line(std::string_view line) {
    std::string result{};
    std::size_t index = 0U;
    while (index < line.size()) {
        if (line[index] == '[') {
            const std::size_t close = line.find("](", index);
            const std::size_t end = close == std::string_view::npos ? close : line.find(')', close);
            if (close != std::string_view::npos && end != std::string_view::npos) {
                result += line.substr(index + 1U, close - index - 1U);
                result += " <" + link_address(line.substr(close + 2U, end - close - 2U)) + ">";
                index = end + 1U;
                continue;
            }
        }
        result += line[index];
        ++index;
    }
    return result;
}

// Markdown as plain text for a console: headings stand on their own line,
// code blocks are indented and table rules dropped.
std::string plain_text(std::string_view markdown) {
    std::string result{};
    bool in_code = false;
    std::size_t start = 0U;
    while (start <= markdown.size()) {
        std::size_t end = markdown.find('\n', start);
        if (end == std::string_view::npos) end = markdown.size();
        const std::string_view line = markdown.substr(start, end - start);
        if (line.substr(0U, 3U) == "```") {
            in_code = !in_code;
        } else if (in_code) {
            result += "    ";
            result += line;
            result += '\n';
        } else if (line.substr(0U, 3U) == "|--" || line.substr(0U, 4U) == "|---") {
            // A table's rule.
        } else if (line.substr(0U, 3U) == "## ") {
            result += "-- " + plain_line(line.substr(3U)) + " --\n";
        } else if (line.substr(0U, 2U) == "# ") {
            result += plain_line(line.substr(2U)) + '\n';
        } else {
            result += plain_line(line);
            result += '\n';
        }
        if (end == markdown.size()) break;
        start = end + 1U;
    }
    while (!result.empty() && result.back() == '\n') result.pop_back();
    return result;
}

// The section of `markdown` under the level-two heading `heading`, the
// heading included; empty when there is none.
std::string_view section_of(std::string_view markdown, std::string_view heading) {
    const std::string wanted = "\n## " + std::string(heading) + "\n";
    const std::size_t found = markdown.find(wanted);
    if (found == std::string_view::npos) return std::string_view{};
    const std::size_t begin = found + 1U;
    std::size_t next = markdown.find("\n## ", begin + 3U);
    if (next == std::string_view::npos) next = markdown.size();
    return markdown.substr(begin, next - begin);
}

// The guide's pages, from the index's table-of-contents blocks.
std::string guide_pages() {
    const std::string_view index = index_text();
    std::string result = "GUI.Forms, the user guide: " + std::string(published_guide) + "index.html\n";
    bool in_toc = false;
    std::size_t start = 0U;
    while (start <= index.size()) {
        std::size_t end = index.find('\n', start);
        if (end == std::string_view::npos) end = index.size();
        const std::string_view line = trimmed(index.substr(start, end - start));
        if (line.substr(0U, 3U) == "```") {
            in_toc = line.find("toctree") != std::string_view::npos;
        } else if (in_toc && line.substr(0U, 10U) == ":caption: ") {
            result += "\n" + std::string(line.substr(10U)) + ":\n";
        } else if (in_toc && !line.empty() && line[0] != ':') {
            result += "  " + std::string(line) + "  " + std::string(published_guide) + std::string(line) + ".html\n";
        }
        if (end == index.size()) break;
        start = end + 1U;
    }
    result += "\nThis program's own use of GUI.Forms: `help mcp`.";
    return result;
}

// A schema property's type as the help names it.
std::string type_name(const detail::JsonValue& property) {
    const detail::JsonValue* const options = property.find("enum");
    if (options != nullptr && (*options).kind == detail::JsonKind::array) {
        std::string names = "one of ";
        for (std::size_t index = 0U; index < (*options).items.size(); ++index) {
            if (index != 0U) names += ", ";
            names += (*options).items[index].text;
        }
        return names;
    }
    const detail::JsonValue* const type = property.find("type");
    if (type == nullptr) return std::string("any");
    if ((*type).kind == detail::JsonKind::string) {
        if ((*type).text != "array") return (*type).text;
        const detail::JsonValue* const items = property.find("items");
        if (items == nullptr) return std::string("array");
        return "array of " + type_name(*items);
    }
    std::string names{};
    for (std::size_t index = 0U; index < (*type).items.size(); ++index) {
        if (index != 0U) names += " or ";
        names += (*type).items[index].text;
    }
    return names;
}

// A property's range ("1 to 100"), when its schema bounds it.
std::string range_of(const detail::JsonValue& property) {
    const detail::JsonValue* const low = property.find("minimum");
    const detail::JsonValue* const high = property.find("maximum");
    if (low != nullptr && high != nullptr) return (*low).text + " to " + (*high).text;
    if (low != nullptr) return "at least " + (*low).text;
    if (high != nullptr) return "at most " + (*high).text;
    return std::string{};
}

bool has_point_members(const detail::JsonValue& property) {
    const detail::JsonValue* const members = property.find("properties");
    const bool point = members != nullptr && (*members).find("x") != nullptr && (*members).find("y") != nullptr;
    return point;
}

// A sample value for an example call, from the property's schema.
std::string sample_value(const detail::JsonValue& property) {
    const detail::JsonValue* const options = property.find("enum");
    if (options != nullptr && (*options).kind == detail::JsonKind::array && !(*options).items.empty()) {
        const detail::JsonValue& first = (*options).items.front();
        if (first.kind == detail::JsonKind::string) return "\"" + first.text + "\"";
        return first.text;
    }
    const detail::JsonValue* const type = property.find("type");
    std::string kind = "string";
    if (type != nullptr && (*type).kind == detail::JsonKind::string) kind = (*type).text;
    if (type != nullptr && (*type).kind == detail::JsonKind::array && !(*type).items.empty()) {
        kind = (*type).items.front().text;
    }
    if (kind == "integer" || kind == "number") {
        const detail::JsonValue* const low = property.find("minimum");
        return low != nullptr ? (*low).text : std::string("0");
    }
    if (kind == "boolean") return std::string("true");
    if (kind == "object") return has_point_members(property) ? std::string("{\"x\":10,\"y\":10}") : std::string("{}");
    if (kind == "array") {
        const detail::JsonValue* const items = property.find("items");
        if (items == nullptr) return std::string("[]");
        const detail::JsonValue* const fewest = property.find("minItems");
        const bool two = fewest != nullptr && (*fewest).text != "0" && (*fewest).text != "1";
        if (has_point_members(*items)) {
            // Points make a real shape, never one point twice (round 5:
            // plan_paint_path's example repeated its point): a triangle,
            // or a line where one or two points do.
            if (!two) return std::string("[{\"x\":10,\"y\":10}]");
            const std::string_view triangle = "[{\"x\":10,\"y\":10},{\"x\":120,\"y\":40},{\"x\":40,\"y\":110}]";
            const detail::JsonValue* const most = property.find("maxItems");
            if (most != nullptr && (*most).text == "2") return std::string("[{\"x\":10,\"y\":10},{\"x\":120,\"y\":40}]");
            return std::string(triangle);
        }
        const std::string item = sample_value(*items);
        return two ? "[" + item + "," + item + "]" : "[" + item + "]";
    }
    if (kind == "null") return std::string("null");
    return std::string("\"text\"");
}

// `help <tool>`: the description, the arguments and an example call.
std::string tool_help(const ToolSpec& spec) {
    std::string text = spec.name;
    if (!spec.title.empty()) text += " (" + spec.title + ")";
    text += "\n";
    if (spec.description.empty()) {
        text += "This tool is not documented: its program declared no description.\n";
    } else {
        text += spec.description + "\n";
    }
    text += spec.access == ToolAccess::acting ? "An act: it needs control.\n" : "A read: it needs no control.\n";
    if (spec.destructive) text += "Destructive: the first such act of a launch asks the user.\n";
    const detail::JsonReadResult read = detail::read_json(spec.input_schema.empty() ? "{}" : spec.input_schema);
    const detail::JsonValue* const properties = read ? read.root.find("properties") : nullptr;
    const detail::JsonValue* const required = read ? read.root.find("required") : nullptr;
    std::string example = "call " + spec.name;
    if (properties == nullptr || (*properties).items.empty()) {
        text += "No arguments.\n";
    } else {
        text += "Arguments:\n";
        std::string members{};
        for (std::size_t index = 0U; index < (*properties).items.size(); ++index) {
            const std::string& name = (*properties).names[index];
            const detail::JsonValue& property = (*properties).items[index];
            bool needed = false;
            if (required != nullptr) {
                for (const detail::JsonValue& item : (*required).items) needed = needed || item.text == name;
            }
            text += "  " + name + "  " + type_name(property);
            const std::string range = range_of(property);
            if (!range.empty()) text += ", " + range;
            text += needed ? ", required" : ", optional";
            const detail::JsonValue* const description = property.find("description");
            if (description != nullptr && !(*description).text.empty()) text += ": " + (*description).text;
            text += "\n";
            if (needed) {
                if (!members.empty()) members += ",";
                members += "\"" + name + "\":" + sample_value(property);
            }
        }
        if (!members.empty()) example += " {" + members + "}";
    }
    text += "Example:\n  " + example;
    return text;
}

// The first sentence of a description, for a one-line list.
std::string first_sentence(std::string_view text) {
    std::size_t end = text.find(". ");
    if (end == std::string_view::npos) end = text.find('\n');
    if (end == std::string_view::npos) return std::string(text);
    std::string sentence(text.substr(0U, end));
    if (sentence.empty()) return std::string(text);
    if (sentence.back() != '.') sentence += '.';
    return sentence;
}

std::string tool_list(const std::vector<ToolSpec>& tools) {
    std::size_t width = 0U;
    for (const ToolSpec& spec : tools) width = std::max(width, spec.name.size());
    std::string text{};
    for (const ToolSpec& spec : tools) {
        text += spec.name + std::string(width - spec.name.size() + 2U, ' ');
        text += spec.description.empty() ? std::string("(not documented)") : first_sentence(spec.description);
        text += "\n";
    }
    text += std::to_string(tools.size()) + " tools; `help <tool>` explains one.";
    return text;
}

// A tool's group: the shortest prefix ending before a '_' that another tool
// shares, extended segment by segment while every tool with it shares the
// next; empty when the tool shares none (a built-in such as act).
std::string tool_group(std::string_view name, const std::vector<ToolSpec>& tools) {
    std::size_t cut = name.find('_');
    while (cut != std::string_view::npos) {
        const std::string_view prefix = name.substr(0U, cut + 1U);
        std::size_t sharing = 0U;
        for (const ToolSpec& spec : tools) {
            if (spec.name.size() > prefix.size() && std::string_view(spec.name).substr(0U, prefix.size()) == prefix) {
                ++sharing;
            }
        }
        if (sharing >= 2U) break;
        cut = name.find('_', cut + 1U);
    }
    if (cut == std::string_view::npos) return std::string{};
    // Extended while every tool of the group shares the next segment
    // (plan_ becomes plan_paint_).
    for (;;) {
        const std::size_t next = name.find('_', cut + 1U);
        if (next == std::string_view::npos) break;
        const std::string_view longer = name.substr(0U, next + 1U);
        const std::string_view shorter = name.substr(0U, cut + 1U);
        bool all = true;
        for (const ToolSpec& spec : tools) {
            const std::string_view other(spec.name);
            if (other.size() <= shorter.size() || other.substr(0U, shorter.size()) != shorter) continue;
            all = all && other.size() > longer.size() && other.substr(0U, longer.size()) == longer;
        }
        if (!all) break;
        cut = next;
    }
    return std::string(name.substr(0U, cut));
}

// A tool's dotted path: group.leaf, or the name alone.
std::string tool_path(const ToolSpec& spec, const std::vector<ToolSpec>& tools) {
    const std::string group = tool_group(spec.name, tools);
    if (group.empty()) return spec.name;
    return group + "." + spec.name.substr(group.size() + 1U);
}

// The groups, in the tools' order.
std::vector<std::string> tool_groups(const std::vector<ToolSpec>& tools) {
    std::vector<std::string> groups{};
    for (const ToolSpec& spec : tools) {
        const std::string group = tool_group(spec.name, tools);
        if (group.empty() || std::find(groups.begin(), groups.end(), group) != groups.end()) continue;
        groups.push_back(group);
    }
    return groups;
}

// `help <group>`: its tools as dotted paths, one line each.
std::string group_list(std::string_view group, const std::vector<ToolSpec>& tools) {
    std::vector<const ToolSpec*> members{};
    std::size_t width = 0U;
    for (const ToolSpec& spec : tools) {
        if (tool_group(spec.name, tools) != group) continue;
        members.push_back(&spec);
        width = std::max(width, tool_path(spec, tools).size());
    }
    std::string text{};
    for (const ToolSpec* const spec : members) {
        const std::string path = tool_path(*spec, tools);
        text += path + std::string(width - path.size() + 2U, ' ');
        text += (*spec).description.empty() ? std::string("(not documented)") : first_sentence((*spec).description);
        text += "\n";
    }
    text += std::to_string(members.size()) + " tools; `help " + std::string(group) + ".<name>` explains one.";
    return text;
}

std::string overview(const std::vector<ToolSpec>& tools, const HelpContext& context) {
    std::string text{};
    if (!context.access.empty()) text += context.access + "\n";
    text += "Chain commands with && (stop on error) or ; (continue): one line, one reply, each part numbered.\n\n";
    if (context.console) {
        text += "Commands, one a line (on one connection or many: each acts on the program as the last did):\n"
                "  help [path]           this tree, a group, one tool or a topic, by dotted path\n"
                "  call <tool> [<json object> | key=value ...]\n"
                "                        calls a tool; the reply is the tool's text, as MCP gives it\n"
                "  transcript            this program's console record\n"
                "  quit                  closes this connection\n"
                "A first line that is a JSON object makes the connection MCP instead.\n\n";
    }
    const std::vector<std::string> groups = tool_groups(tools);
    if (!groups.empty()) {
        text += "Groups:\n";
        for (const std::string& group : groups) {
            std::size_t count = 0U;
            for (const ToolSpec& spec : tools) count += tool_group(spec.name, tools) == group ? 1U : 0U;
            text += "  " + group + "  (" + std::to_string(count) + " tools: `help " + group + "`)\n";
        }
    }
    std::size_t width = 0U;
    for (const ToolSpec& spec : tools) {
        if (tool_group(spec.name, tools).empty()) width = std::max(width, spec.name.size());
    }
    text += "Tools:\n";
    for (const ToolSpec& spec : tools) {
        if (!tool_group(spec.name, tools).empty()) continue;
        text += "  " + spec.name + std::string(width - spec.name.size() + 2U, ' ');
        text += spec.description.empty() ? std::string("(not documented)") : first_sentence(spec.description);
        text += "\n";
    }
    text += "Topics: `help mcp`";
    for (const SectionTopic& entry : section_topics) {
        if (!section_of(talk_text(), entry.heading).empty()) text += ", `help mcp." + std::string(entry.topic) + "`";
    }
    text += ", `help gui.forms`; `help tools` lists every tool.\n";
    text += std::to_string(tools.size()) + " tools serve this program.";
    if (!context.note.empty()) text += "\n\nHow this program works (its own note, also gui://app/note):\n" + context.note;
    return text;
}

} // namespace

std::string session_instructions(std::string_view base, const HelpContext& context) {
    std::string text(base);
    if (!context.access.empty()) text += "\n" + context.access;
    text += "\n" + std::string(mcp_help_pointer);
    if (!context.note.empty()) {
        text += "\n\nHow this program works (its own note, also the resource gui://app/note):\n" + context.note;
    }
    return text;
}

std::string help_tree_text(const std::vector<ToolSpec>& tools, const HelpContext& context) {
    std::string text = overview(tools, context);
    for (const std::string& group : tool_groups(tools)) {
        text += "\n\n== help " + group + "\n" + group_list(group, tools);
    }
    for (const ToolSpec& spec : tools) {
        text += "\n\n== help " + tool_path(spec, tools) + "\n" + tool_help(spec);
    }
    text += "\n";
    return text;
}

std::string tool_for_path(std::string_view path, const std::vector<ToolSpec>& tools) {
    const std::string wanted = lowered(trimmed(path));
    if (wanted.empty()) return std::string{};
    for (const ToolSpec& spec : tools) {
        if (lowered(spec.name) == wanted || lowered(tool_path(spec, tools)) == wanted) return spec.name;
    }
    if (wanted == "release") {
        for (const ToolSpec& spec : tools) {
            if (spec.name == "release_control") return spec.name;
        }
    }
    return std::string{};
}

std::vector<std::string> help_topics() {
    std::vector<std::string> topics{"mcp"};
    for (const SectionTopic& entry : section_topics) {
        if (!section_of(talk_text(), entry.heading).empty()) topics.emplace_back(entry.topic);
    }
    return topics;
}

std::vector<std::string> undocumented_tools(const std::vector<ToolSpec>& tools) {
    std::vector<std::string> names{};
    for (const ToolSpec& spec : tools) {
        if (spec.description.empty()) names.push_back(spec.name);
    }
    return names;
}

std::size_t help_distance(std::string_view left, std::string_view right) {
    std::vector<std::size_t> row(right.size() + 1U);
    for (std::size_t column = 0U; column <= right.size(); ++column) row[column] = column;
    for (std::size_t line = 1U; line <= left.size(); ++line) {
        std::size_t diagonal = row[0];
        row[0] = line;
        for (std::size_t column = 1U; column <= right.size(); ++column) {
            const std::size_t above = row[column];
            const std::size_t substitution = diagonal + (left[line - 1U] == right[column - 1U] ? 0U : 1U);
            row[column] = std::min({above + 1U, row[column - 1U] + 1U, substitution});
            diagonal = above;
        }
    }
    return row[right.size()];
}

std::string help_text(std::string_view topic, const std::vector<ToolSpec>& tools, const HelpContext& context) {
    const std::string wanted = lowered(trimmed(topic));
    if (wanted.empty()) return overview(tools, context);
    if (wanted == "tools") return tool_list(tools);
    if (wanted == "mcp") return plain_text(talk_text());
    if (wanted == "gui.forms" || wanted == "guiforms" || wanted == "docs") return guide_pages();
    const std::string section_name = wanted.rfind("mcp.", 0U) == 0U ? wanted.substr(4U) : wanted;
    for (const SectionTopic& entry : section_topics) {
        if (section_name != entry.topic) continue;
        const std::string_view section = section_of(talk_text(), entry.heading);
        if (!section.empty()) return plain_text(section);
    }
    for (const std::string& group : tool_groups(tools)) {
        if (lowered(group) == wanted) return group_list(group, tools);
    }
    for (const ToolSpec& spec : tools) {
        if (lowered(spec.name) == wanted || lowered(tool_path(spec, tools)) == wanted) {
            const std::string path = tool_path(spec, tools);
            std::string text = tool_help(spec);
            if (path != spec.name) text = path + " = " + text;
            return text;
        }
    }
    // The nearest path the help knows.
    std::vector<std::string> known{"mcp", "tools", "gui.forms"};
    for (const SectionTopic& entry : section_topics) known.push_back("mcp." + std::string(entry.topic));
    for (const std::string& group : tool_groups(tools)) known.push_back(group);
    for (const ToolSpec& spec : tools) known.push_back(tool_path(spec, tools));
    std::string nearest{};
    std::size_t best = 0U;
    for (const std::string& name : known) {
        const std::size_t distance = help_distance(wanted, lowered(name));
        if (nearest.empty() || distance < best) {
            nearest = name;
            best = distance;
        }
    }
    std::string text = "no help on \"" + std::string(trimmed(topic)) + "\"";
    if (!nearest.empty()) text += "; the nearest is `help " + nearest + "`";
    text += ". `help` lists the tree, `help tools` every tool.";
    return text;
}

} // namespace gui_forms::automation
