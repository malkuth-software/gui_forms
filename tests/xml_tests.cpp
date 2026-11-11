#include "gui_forms/xml.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Counts what the reader asks the heap for while the probe is enabled. The
// total requested bytes during one read is an upper bound on the read's
// peak live heap use, which is how the fuzz-style test measures memory.
namespace allocation_probe {
bool enabled = false;
std::size_t calls = 0U;
std::size_t requested_bytes = 0U;

void* allocate(const std::size_t bytes) {
    if (enabled) {
        ++calls;
        requested_bytes += bytes;
    }
    const std::size_t requested = bytes == 0U ? 1U : bytes;
    void* result = std::malloc(requested);
    if (result == nullptr) throw std::bad_alloc();
    return result;
}
} // namespace allocation_probe

void* operator new(const std::size_t bytes) {
    void* result = allocation_probe::allocate(bytes);
    return result;
}
void* operator new[](const std::size_t bytes) {
    void* result = allocation_probe::allocate(bytes);
    return result;
}
void operator delete(void* address) noexcept { std::free(address); }
void operator delete[](void* address) noexcept { std::free(address); }
void operator delete(void* address, std::size_t) noexcept { std::free(address); }
void operator delete[](void* address, std::size_t) noexcept { std::free(address); }

namespace {

using gui_forms::XmlAttribute;
using gui_forms::XmlElement;
using gui_forms::XmlError;
using gui_forms::XmlLimits;
using gui_forms::XmlReadResult;
using gui_forms::XmlWriteOptions;
using gui_forms::XmlWriteResult;
using gui_forms::read_xml;
using gui_forms::write_xml;
using gui_forms::xml_error_name;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

XmlElement read_ok(std::string_view text, std::string_view what) {
    XmlReadResult result = read_xml(text);
    if (!result) {
        std::cerr << "FAIL: " << what << ": " << xml_error_name(result.error) << " at "
                  << result.position.line << ':' << result.position.column << '\n';
        ++failures;
    }
    return std::move(result.root);
}

void require_error(std::string_view text, XmlError expected, std::string_view what,
                   const XmlLimits& limits = {}) {
    const XmlReadResult result = read_xml(text, limits);
    if (result.error != expected) {
        std::cerr << "FAIL: " << what << ": expected " << xml_error_name(expected) << ", got "
                  << xml_error_name(result.error) << '\n';
        ++failures;
        return;
    }
    require(result.root == XmlElement{}, "a failed read returns an empty root");
}

std::string attribute(const XmlElement& element, std::string_view name) {
    const XmlAttribute* found = element.find_attribute(name);
    if (found == nullptr) return "<absent>";
    std::string value = (*found).value;
    return value;
}

void test_conformance() {
    const XmlElement minimal = read_ok("<a/>", "minimal");
    require(minimal.name == "a" && minimal.attributes.empty() && minimal.children.empty() &&
                minimal.text.empty(), "minimal element");

    const XmlElement declared = read_ok(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<root/>", "declaration");
    require(declared.name == "root", "declaration then root");
    const XmlElement single = read_ok("<?xml version='1.0' encoding='utf-8' standalone='yes' ?><r/>",
                                      "single-quoted declaration");
    require(single.name == "r", "single-quoted declaration with standalone");
    const XmlElement bom = read_ok("\xEF\xBB\xBF<?xml version=\"1.0\"?><r/>", "byte order mark");
    require(bom.name == "r", "UTF-8 byte order mark skipped");

    const XmlElement attributes = read_ok("<e a=\"1\" b='two' c = \"x&amp;y\" d=\"&lt;&gt;&apos;&quot;\"/>",
                                          "attributes");
    require(attributes.attributes.size() == 4U, "four attributes in order");
    require(attributes.attributes[0].name == "a" && attributes.attributes[3].name == "d",
            "attribute order kept");
    require(attribute(attributes, "b") == "two", "single-quoted attribute");
    require(attribute(attributes, "c") == "x&y", "entity in attribute");
    require(attribute(attributes, "d") == "<>'\"", "the five predefined entities");
    require(attribute(attributes, "z") == "<absent>", "find_attribute misses");

    const XmlElement references = read_ok("<e v=\"&#65;&#x42;&#x00043;&#x1F600;&#233;\">&#x3C;b&#62;</e>",
                                          "character references");
    require(attribute(references, "v") == "ABC\xF0\x9F\x98\x80\xC3\xA9", "decimal and hex references");
    require(references.text == "<b>", "references in text");

    const XmlElement normalized = read_ok("<e v=\"a\tb\nc\r\nd\rf&#10;g&#9;h\">x\r\ny\rz</e>",
                                          "normalization");
    require(attribute(normalized, "v") == "a b c d f\ng\th", "attribute value normalization");
    require(normalized.text == "x\ny\nz", "line ends in text become LF");

    const XmlElement nested = read_ok(
        "<root>\n  <!-- a comment -->\n  <child id=\"1\">text one</child>\n"
        "  <child id=\"2\"><leaf/></child>\n  <space>  </space>\n</root>\n<!-- trailing -->\n",
        "nested");
    require(nested.children.size() == 3U, "three children");
    require(nested.text.empty(), "indentation between children dropped");
    require(nested.children[0].text == "text one", "leaf text kept");
    require(nested.children[1].children.size() == 1U && nested.children[1].children[0].name == "leaf",
            "grandchild");
    require(nested.children[2].text == "  ", "whitespace-only leaf text kept");

    const XmlElement names = read_ok("<dock:layout xmlns:dock=\"urn:x\" _a.b-c=\"1\" \xC3\xA9t\xC3\xA9=\"2\"/>",
                                     "names");
    require(names.name == "dock:layout", "colon is a name character");
    require(attribute(names, "xmlns:dock") == "urn:x", "namespace declarations are ordinary attributes");
    require(attribute(names, "\xC3\xA9t\xC3\xA9") == "2", "non-ASCII names");

    const XmlElement comment_text = read_ok("<e>a<!-- x -->b</e>", "comment inside text");
    require(comment_text.text == "ab", "comments are dropped from text");
    const XmlElement empty_comment = read_ok("<e><!----></e>", "empty comment");
    require(empty_comment.text.empty(), "empty comment");
    const XmlElement end_tag_space = read_ok("<e  a=\"1\" ></e  >", "spaces in tags");
    require(end_tag_space.name == "e" && attribute(end_tag_space, "a") == "1", "spaces in tags");
    const XmlElement gt_in_text = read_ok("<e>a > b ]] c</e>", "greater-than in text");
    require(gt_in_text.text == "a > b ]] c", "a lone > and ]] are character data");
}

void test_malformed() {
    require_error("", XmlError::missing_root_element, "empty input");
    require_error("   \n", XmlError::missing_root_element, "whitespace only");
    require_error("<a>", XmlError::unexpected_end, "unclosed root");
    require_error("<a></b>", XmlError::mismatched_end_tag, "mismatched end tag");
    require_error("<a><b></a></b>", XmlError::mismatched_end_tag, "crossed tags");
    require_error("<a/><b/>", XmlError::content_after_root, "two roots");
    require_error("<a/>text", XmlError::content_after_root, "text after root");
    require_error("text<a/>", XmlError::syntax_error, "text before root");
    require_error("<a>x<b/></a>", XmlError::mixed_content, "text before a child");
    require_error("<a><b/>x</a>", XmlError::mixed_content, "text after a child");
    require_error("<a b=\"1\" b=\"2\"/>", XmlError::duplicate_attribute, "duplicate attribute");
    require_error("<a b=\"1\"c=\"2\"/>", XmlError::syntax_error, "attributes without space");
    require_error("<a b=1/>", XmlError::syntax_error, "unquoted attribute");
    require_error("<a b=\"<\"/>", XmlError::syntax_error, "< in attribute");
    require_error("<a b/>", XmlError::syntax_error, "attribute without value");
    require_error("<1a/>", XmlError::invalid_name, "digit cannot start a name");
    require_error("< a/>", XmlError::invalid_name, "space before the name");
    require_error("<a>]]></a>", XmlError::syntax_error, "]]> in text");
    require_error("<a>&foo;</a>", XmlError::unknown_entity, "unknown entity");
    require_error("<a>&nbsp;</a>", XmlError::unknown_entity, "HTML entity");
    require_error("<a>&amp</a>", XmlError::unknown_entity, "entity without semicolon");
    require_error("<a>& b</a>", XmlError::syntax_error, "bare ampersand");
    require_error("<a>&#0;</a>", XmlError::invalid_character_reference, "reference to NUL");
    require_error("<a>&#xD800;</a>", XmlError::invalid_character_reference, "reference to a surrogate");
    require_error("<a>&#x110000;</a>", XmlError::invalid_character_reference, "reference above U+10FFFF");
    require_error("<a>&#99999999999999999999;</a>", XmlError::invalid_character_reference,
                  "overlong reference");
    require_error("<a>&#;</a>", XmlError::invalid_character_reference, "reference without digits");
    require_error("<a>&#x41</a>", XmlError::invalid_character_reference, "reference without semicolon");
    require_error("<a>\xC0\x80</a>", XmlError::invalid_utf8, "overlong NUL");
    require_error("<a>\xE0\x80\xAF</a>", XmlError::invalid_utf8, "overlong slash");
    require_error("<a>\xED\xA0\x80</a>", XmlError::invalid_utf8, "encoded surrogate");
    require_error("<a>\xF4\x90\x80\x80</a>", XmlError::invalid_utf8, "above U+10FFFF");
    require_error("<a>\xF5\x80\x80\x80</a>", XmlError::invalid_utf8, "invalid lead byte");
    require_error("<a>\x80</a>", XmlError::invalid_utf8, "lone continuation byte");
    require_error("<a>\xE2\x82</a>", XmlError::invalid_utf8, "truncated sequence");
    require_error("<a>\xE9t\xE9</a>", XmlError::invalid_utf8, "Latin-1 text without a byte order mark");
    require_error("<a v=\"\xFF\"/>", XmlError::invalid_utf8, "invalid UTF-8 in an attribute");
    require_error("<\xC3\x28/>", XmlError::invalid_utf8, "invalid UTF-8 in a name");
    require_error("<a>\x01</a>", XmlError::invalid_character, "control character");
    require_error("<a>\xEF\xBF\xBE</a>", XmlError::invalid_character, "U+FFFE");
    require_error("\xFE\xFF\x00<\x00" "a\x00/\x00>", XmlError::unsupported_encoding, "UTF-16 BE mark");
    require_error(std::string_view("<\0a\0/\0>\0", 8), XmlError::unsupported_encoding, "UTF-16 LE text");
    require_error("<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?><a/>", XmlError::unsupported_encoding,
                  "declared Latin-1");
    require_error("<?xml version=\"1.1\"?><a/>", XmlError::invalid_declaration, "XML 1.1");
    require_error("<?xml encoding=\"UTF-8\"?><a/>", XmlError::invalid_declaration, "declaration without version");
    require_error("<?xml version=\"1.0\" standalone=\"maybe\"?><a/>", XmlError::invalid_declaration,
                  "bad standalone");
    require_error("<?xml version=\"1.0\"", XmlError::unexpected_end, "truncated declaration");
    require_error(" <?xml version=\"1.0\"?><a/>", XmlError::processing_instruction_refused,
                  "declaration not at the start");
    require_error("<!DOCTYPE a [<!ENTITY e \"x\">]><a>&e;</a>", XmlError::doctype_refused, "DTD present");
    require_error("<!DOCTYPE a SYSTEM \"http://example.invalid/a.dtd\"><a/>", XmlError::doctype_refused,
                  "external DTD");
    require_error("<a><!DOCTYPE b></a>", XmlError::doctype_refused, "DTD inside content");
    require_error("<?xml-stylesheet href=\"a\"?><a/>", XmlError::processing_instruction_refused,
                  "processing instruction in prolog");
    require_error("<a><?target data?></a>", XmlError::processing_instruction_refused,
                  "processing instruction in content");
    require_error("<a/><?target?>", XmlError::processing_instruction_refused,
                  "processing instruction after root");
    require_error("<a><![CDATA[x]]></a>", XmlError::cdata_refused, "CDATA section");
    require_error("<a><!ELEMENT a ANY></a>", XmlError::syntax_error, "markup declaration in content");
    require_error("<a><!-- a -- b --></a>", XmlError::invalid_comment, "double hyphen in comment");
    require_error("<a><!-- a ---></a>", XmlError::invalid_comment, "comment ending in three hyphens");
    require_error("<a><!-- open", XmlError::unexpected_end, "unterminated comment");
}

// Every proper prefix of a well-formed document that ends inside its root
// element must be refused, and none of them may crash.
void test_truncation() {
    const std::string document =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<layout format=\"2\" name=\"caf\xC3\xA9 &amp; bar\">\n"
        "  <!-- comment -->\n"
        "  <split ratio=\"0.25\"><stack id=\"7\">&#x1F600; text</stack><empty/></split>\n"
        "</layout>\n";
    const XmlElement full = read_ok(document, "truncation seed");
    require(full.name == "layout", "truncation seed reads");
    const std::size_t root_end = document.rfind('>') + 1U;
    for (std::size_t length = 0; length < root_end; ++length) {
        const std::string_view prefix(document.data(), length);
        const XmlReadResult result = read_xml(prefix);
        if (result) {
            std::cerr << "FAIL: prefix of " << length << " bytes was accepted\n";
            ++failures;
        }
    }
    const XmlReadResult tail = read_xml(std::string_view(document.data(), root_end));
    require(static_cast<bool>(tail), "the document without its final newline is complete");
}

void test_positions() {
    const XmlReadResult result = read_xml("<a>\n  <b>\n  caf\xC3\xA9 &bad;</b></a>");
    require(result.error == XmlError::unknown_entity, "position case error");
    require(result.position.line == 3U, "error line");
    require(result.position.column == 8U, "column counts scalar values, not bytes");
    require(result.position.byte_offset == 18U, "error byte offset");
    const XmlReadResult crlf = read_xml("<a>\r\n\r\n<b></c></a>");
    require(crlf.position.line == 3U && crlf.position.column == 4U, "CR LF ends one line");
    const XmlReadResult first = read_xml("x");
    require(first.position.line == 1U && first.position.column == 1U, "first character");
    const XmlReadResult success = read_xml("<a/>");
    require(success.position == gui_forms::XmlSourcePosition{}, "no position on success");
}

std::string repeated(std::string_view text, std::size_t count) {
    std::string result{};
    for (std::size_t index = 0; index < count; ++index) result.append(text);
    return result;
}

void test_limits() {
    XmlLimits input{};
    input.maximum_input_bytes = 8;
    require(static_cast<bool>(read_xml("<abcde/>", input)), "input at its limit");
    require_error("<abcdef/>", XmlError::input_too_large, "input over its limit", input);

    XmlLimits depth{};
    depth.maximum_depth = 3;
    require(static_cast<bool>(read_xml("<a><b><c/></b></a>", depth)), "depth at its limit");
    require_error("<a><b><c><d/></c></b></a>", XmlError::depth_exceeded, "depth over its limit", depth);
    const std::string deep = repeated("<a>", 65) + repeated("</a>", 65);
    require_error(deep, XmlError::depth_exceeded, "default depth limit of 64");

    XmlLimits bad_depth{};
    bad_depth.maximum_depth = 0;
    require_error("<a/>", XmlError::invalid_limits, "zero depth limit", bad_depth);
    bad_depth.maximum_depth = gui_forms::xml_depth_ceiling + 1U;
    require_error("<a/>", XmlError::invalid_limits, "depth limit above the ceiling", bad_depth);

    XmlLimits elements{};
    elements.maximum_elements = 3;
    require(static_cast<bool>(read_xml("<a><b/><c/></a>", elements)), "elements at their limit");
    require_error("<a><b/><c/><d/></a>", XmlError::element_limit_exceeded, "elements over their limit",
                  elements);

    XmlLimits attributes{};
    attributes.maximum_attributes_per_element = 2;
    require(static_cast<bool>(read_xml("<a x=\"1\" y=\"2\"/>", attributes)), "attributes at their limit");
    require_error("<a x=\"1\" y=\"2\" z=\"3\"/>", XmlError::attribute_limit_exceeded,
                  "attributes over their limit", attributes);

    XmlLimits names{};
    names.maximum_name_bytes = 4;
    require(static_cast<bool>(read_xml("<abcd efgh=\"\"/>", names)), "names at their limit");
    require_error("<abcde/>", XmlError::name_too_long, "element name over its limit", names);
    require_error("<a efghi=\"\"/>", XmlError::name_too_long, "attribute name over its limit", names);
    require_error("<abcd></abcde>", XmlError::name_too_long, "end tag name over its limit", names);

    XmlLimits values{};
    values.maximum_value_bytes = 4;
    require(static_cast<bool>(read_xml("<a v=\"&amp;bcd\">wxyz<!--1234--></a>", values)),
            "values at their limit");
    require_error("<a v=\"abcde\"/>", XmlError::value_too_long, "attribute over its limit", values);
    require_error("<a>abcde</a>", XmlError::value_too_long, "text over its limit", values);
    require_error("<a>abcd&amp;</a>", XmlError::value_too_long, "text over its limit by a reference", values);
    require_error("<a>ab<!---->cde</a>", XmlError::value_too_long, "text across a comment", values);
    require_error("<a><!--12345--></a>", XmlError::value_too_long, "comment over its limit", values);
    require_error("<a>\xF0\x9F\x98\x80" "a</a>", XmlError::value_too_long,
                  "a multi-byte character crossing the limit", values);
}

XmlElement sample_tree() {
    XmlElement root{};
    root.name = "dock_layout";
    root.attributes.push_back(XmlAttribute{.name = "format", .value = "2"});
    root.attributes.push_back(XmlAttribute{.name = "title", .value = "a < b & \"c\" > 'd'\t\n\r"});
    XmlElement split{};
    split.name = "split";
    split.attributes.push_back(XmlAttribute{.name = "ratio", .value = "0.25"});
    XmlElement stack{};
    stack.name = "stack";
    stack.text = "  text with <markup> & ]]> and\r\nline ends  ";
    split.children.push_back(stack);
    XmlElement empty{};
    empty.name = "empty";
    split.children.push_back(empty);
    root.children.push_back(split);
    XmlElement unicode{};
    unicode.name = "\xC3\xA9l\xC3\xA9ment";
    unicode.text = "\xF0\x9F\x98\x80";
    root.children.push_back(unicode);
    return root;
}

void test_writer() {
    const XmlElement tree = sample_tree();
    const XmlWriteResult written = write_xml(tree);
    require(static_cast<bool>(written), "sample writes");
    const std::string expected =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<dock_layout format=\"2\" title=\"a &lt; b &amp; &quot;c&quot; &gt; 'd'&#9;&#10;&#13;\">\n"
        "  <split ratio=\"0.25\">\n"
        "    <stack>  text with &lt;markup&gt; &amp; ]]&gt; and&#13;\nline ends  </stack>\n"
        "    <empty/>\n"
        "  </split>\n"
        "  <\xC3\xA9l\xC3\xA9ment>\xF0\x9F\x98\x80</\xC3\xA9l\xC3\xA9ment>\n"
        "</dock_layout>\n";
    require(written.text == expected, "writer output is exactly the documented form");

    const XmlReadResult reread = read_xml(written.text);
    require(static_cast<bool>(reread), "written document reads");
    require(reread.root == tree, "read(write(tree)) == tree");
    const XmlWriteResult again = write_xml(reread.root);
    require(again.text == written.text, "round trip is byte-stable");

    XmlWriteOptions bare{};
    bare.declaration = false;
    bare.indent_spaces = 0;
    const XmlWriteResult unindented = write_xml(tree, bare);
    require(unindented.text.starts_with("<dock_layout") && unindented.text.find("\n<split") !=
                std::string::npos, "no declaration and no indentation");
    const XmlReadResult unindented_read = read_xml(unindented.text);
    require(unindented_read.root == tree, "unindented form reads back the same tree");

    XmlElement bad_name{};
    bad_name.name = "1bad";
    require(write_xml(bad_name).error == XmlError::invalid_name, "writer refuses a bad name");
    XmlElement empty_name{};
    require(write_xml(empty_name).error == XmlError::invalid_name, "writer refuses an empty name");
    XmlElement mixed = sample_tree();
    mixed.text = "x";
    require(write_xml(mixed).error == XmlError::mixed_content, "writer refuses mixed content");
    XmlElement duplicate{};
    duplicate.name = "a";
    duplicate.attributes.push_back(XmlAttribute{.name = "x", .value = "1"});
    duplicate.attributes.push_back(XmlAttribute{.name = "x", .value = "2"});
    require(write_xml(duplicate).error == XmlError::duplicate_attribute, "writer refuses duplicates");
    XmlElement control{};
    control.name = "a";
    control.text = "\x01";
    require(write_xml(control).error == XmlError::invalid_character, "writer refuses a control character");
    XmlElement broken{};
    broken.name = "a";
    broken.attributes.push_back(XmlAttribute{.name = "v", .value = "\xC3"});
    require(write_xml(broken).error == XmlError::invalid_utf8, "writer refuses invalid UTF-8");

    XmlLimits shallow{};
    shallow.maximum_depth = 2;
    require(write_xml(tree, XmlWriteOptions{}, shallow).error == XmlError::depth_exceeded,
            "writer enforces depth");
    XmlLimits few{};
    few.maximum_elements = 4;
    require(write_xml(tree, XmlWriteOptions{}, few).error == XmlError::element_limit_exceeded,
            "writer enforces the element count");
    XmlLimits small{};
    small.maximum_input_bytes = 64;
    require(write_xml(tree, XmlWriteOptions{}, small).error == XmlError::input_too_large,
            "writer refuses a document the reader would refuse");
    const XmlWriteResult failed = write_xml(mixed);
    require(failed.text.empty(), "a failed write returns no text");
}

// A small deterministic generator (xorshift64), so every run mutates the
// same way and a failure reproduces from its iteration number.
struct MutationRandom final {
    std::uint64_t state{0x9E3779B97F4A7C15ULL};

    std::uint64_t next() noexcept {
        state ^= state << 13U;
        state ^= state >> 7U;
        state ^= state << 17U;
        return state;
    }

    std::size_t below(std::size_t bound) noexcept {
        if (bound == 0U) return 0U;
        const std::size_t value = static_cast<std::size_t>(next() % bound);
        return value;
    }
};

constexpr std::array<std::string_view, 20> mutation_tokens{
    "<", ">", "/", "&", "&amp;", "&#x", ";", "\"", "'", "=", " ", "<!--", "-->", "]]>",
    "<a>", "</a>", "\xC0", "\xED\xA0\x80", "\xF0\x9F\x98\x80", "<!DOCTYPE"};

void mutate(std::string& input, MutationRandom& random) {
    const std::size_t choice = random.below(6);
    const std::size_t offset = random.below(input.size() + 1U);
    if (choice == 0U && !input.empty()) {
        const std::size_t at = random.below(input.size());
        input[at] = static_cast<char>(input[at] ^ static_cast<char>(1U << random.below(8)));
    } else if (choice == 1U && !input.empty()) {
        const std::size_t count = 1U + random.below(8);
        input.erase(random.below(input.size()), count);
    } else if (choice == 2U) {
        const std::string_view token = mutation_tokens[random.below(mutation_tokens.size())];
        input.insert(offset, token);
    } else if (choice == 3U && !input.empty()) {
        // Duplicating a range repeats tags, which builds depth and width.
        const std::size_t start = random.below(input.size());
        const std::size_t count = 1U + random.below(32);
        const std::string copied = input.substr(start, count);
        input.insert(offset, copied);
    } else if (choice == 4U) {
        input.resize(offset);
    } else {
        input.insert(offset, 1U, static_cast<char>(random.below(256)));
    }
}

void test_seeded_mutation() {
    const std::array<std::string_view, 3> seeds{
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<dock_layout format=\"2\" application_version=\"1\">\n"
        "  <main_site>\n"
        "    <split id=\"1\" axis=\"horizontal\" ratio=\"0.25\">\n"
        "      <tool_stack id=\"2\" selected=\"0\"><panel id=\"tools.solution\"/></tool_stack>\n"
        "      <document_area id=\"3\"/>\n"
        "    </split>\n"
        "  </main_site>\n"
        "</dock_layout>\n",
        "<r a='&lt;&#65;&#x1F600;' b=\"\t\">\n <!-- c -->\n <e>caf\xC3\xA9 &amp; t\xC3\xA9</e><f/>\n</r>",
        "\xEF\xBB\xBF<a><b><c><d><e x=\"1\" y=\"2\" z=\"3\">deep</e></d></c></b></a>"};
    MutationRandom random{};
    const std::size_t iterations = 30000;
    std::size_t accepted = 0;
    std::size_t largest_input = 0;
    double worst_ratio = 0.0;
    std::size_t worst_requested = 0;
    std::int64_t slowest_microseconds = 0;
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        std::string input(seeds[iteration % seeds.size()]);
        const std::size_t mutations = 1U + random.below(6);
        for (std::size_t step = 0; step < mutations; ++step) mutate(input, random);
        if (input.size() > largest_input) largest_input = input.size();

        allocation_probe::requested_bytes = 0;
        allocation_probe::calls = 0;
        const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        allocation_probe::enabled = true;
        XmlReadResult result = read_xml(input);
        allocation_probe::enabled = false;
        const std::chrono::steady_clock::time_point finished = std::chrono::steady_clock::now();
        const std::int64_t microseconds =
            std::chrono::duration_cast<std::chrono::microseconds>(finished - started).count();
        if (microseconds > slowest_microseconds) slowest_microseconds = microseconds;

        // Bounded memory: everything the read asked the heap for, in total,
        // stays within 128 bytes per input byte plus 64 KiB.
        const std::size_t bound = 128U * input.size() + 64U * 1024U;
        if (allocation_probe::requested_bytes > bound) {
            std::cerr << "FAIL: iteration " << iteration << " requested "
                      << allocation_probe::requested_bytes << " bytes for " << input.size()
                      << " input bytes\n";
            ++failures;
        }
        if (allocation_probe::requested_bytes > worst_requested) {
            worst_requested = allocation_probe::requested_bytes;
        }
        if (!input.empty()) {
            const double ratio = static_cast<double>(allocation_probe::requested_bytes) /
                                 static_cast<double>(input.size());
            if (ratio > worst_ratio) worst_ratio = ratio;
        }
        if (!result) {
            if (result.position.byte_offset > input.size()) {
                std::cerr << "FAIL: iteration " << iteration << " reported a position past the end\n";
                ++failures;
            }
            continue;
        }
        // Whatever the reader accepts, the writer writes, and the written
        // form reads back to the same tree and writes the same bytes.
        ++accepted;
        const XmlWriteResult written = write_xml(result.root);
        if (!written) {
            std::cerr << "FAIL: iteration " << iteration << " accepted tree does not write: "
                      << xml_error_name(written.error) << '\n';
            ++failures;
            continue;
        }
        const XmlReadResult reread = read_xml(written.text);
        const XmlWriteResult rewritten = write_xml(reread.root);
        if (!(reread.root == result.root) || rewritten.text != written.text) {
            std::cerr << "FAIL: iteration " << iteration << " round trip differs\n";
            ++failures;
        }
    }
    require(accepted > 0U && accepted < iterations, "mutation produced both accepted and refused inputs");

    // The densest documents for the reader's storage: the most elements,
    // the deepest nesting and the most attributes per input byte, at the
    // default limits.
    const std::string wide = "<a>" + repeated("<b/>", 65535) + "</a>";
    const std::string deep = repeated("<a>", 64) + repeated("</a>", 64);
    std::string dense_attributes = "<r>";
    for (std::size_t element = 0; element < 4000; ++element) {
        dense_attributes.append("<e");
        for (char name = 'a'; name < 'a' + 26; ++name) {
            dense_attributes.push_back(' ');
            dense_attributes.push_back(name);
            dense_attributes.append("=''");
        }
        dense_attributes.append("/>");
    }
    dense_attributes.append("</r>");
    const std::array<const std::string*, 3> dense{&wide, &deep, &dense_attributes};
    for (const std::string* document : dense) {
        allocation_probe::requested_bytes = 0;
        allocation_probe::enabled = true;
        const XmlReadResult result = read_xml(*document);
        allocation_probe::enabled = false;
        require(static_cast<bool>(result), "dense document reads");
        const double ratio = static_cast<double>(allocation_probe::requested_bytes) /
                             static_cast<double>((*document).size());
        require(allocation_probe::requested_bytes <= 128U * (*document).size() + 64U * 1024U,
                "dense document within the memory bound");
        std::cout << "dense document of " << (*document).size() << " bytes: "
                  << allocation_probe::requested_bytes << " bytes requested (" << ratio
                  << " per input byte)\n";
    }
    std::cout << "seeded mutation: " << iterations << " inputs (largest " << largest_input
              << " bytes), " << accepted << " accepted; most heap requested by one read "
              << worst_requested << " bytes, at most " << worst_ratio
              << " bytes per input byte; slowest read " << slowest_microseconds << " us\n";
}

} // namespace

// A chain `levels` deep, the root at depth 1, built in code (no limit).
XmlElement element_chain(std::size_t levels) {
    XmlElement current{};
    current.name = "leaf";
    for (std::size_t level = 1U; level < levels; ++level) {
        XmlElement parent{};
        parent.name = "level";
        parent.children.push_back(std::move(current));
        current = std::move(parent);
    }
    return current;
}

// Copying, comparing and destroying do not recurse per level (HOUSE_STYLE
// section 1): a chain far deeper than any reader limit, whose recursive
// teardown would exhaust an 8 MiB stack, is copied, compared and destroyed.
// The writer refuses one level past maximum_depth and writes the limit.
void test_deep_trees() {
    constexpr std::size_t deep = 200000U;
    {
        const XmlElement original = element_chain(deep);
        XmlElement copy = original;
        require(copy == original, "a deep copy compares equal");
        copy.children.front().children.front().text = "changed";
        require(!(copy == original), "a change two levels down compares unequal");
        XmlElement assigned{};
        assigned = original;
        require(assigned == original, "copy assignment copies the whole depth");
    }
    XmlLimits limits{};
    limits.maximum_depth = xml_depth_ceiling;
    const XmlElement at_limit = element_chain(xml_depth_ceiling);
    const XmlWriteResult written = write_xml(at_limit, XmlWriteOptions{}, limits);
    require(static_cast<bool>(written), "a tree at maximum_depth writes");
    const XmlReadResult reread = read_xml(written.text, limits);
    require(static_cast<bool>(reread) && reread.root == at_limit, "a tree at maximum_depth reads back equal");
    const XmlElement past_limit = element_chain(xml_depth_ceiling + 1U);
    const XmlWriteResult refused = write_xml(past_limit, XmlWriteOptions{}, limits);
    require(refused.error == XmlError::depth_exceeded && refused.text.empty(),
            "one level past maximum_depth is refused, writing nothing");
}

int main() {
    test_conformance();
    test_malformed();
    test_truncation();
    test_positions();
    test_limits();
    test_writer();
    test_deep_trees();
    test_seeded_mutation();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "xml tests passed\n";
    return 0;
}
