// The localizable-literal plugin, dogfooded: this unit is compiled with the
// plugin (GUI_FORMS_LOCALIZABLE_TEST_PLUGIN), next to a unit compiled
// without it, a static library and a shared library compiled with it. The
// test reads the tables back with localizable_literals() and checks the
// records exactly: which literals are found, which are not, and where.
// Without the plugin it checks only the table reader. The plugin also
// searches lambda bodies; the house style keeps lambdas out of this
// repository, so no test here passes a literal in one.
#include "gui_forms/gui_forms.hpp"
#include "gui_forms/text/localizable/localizable.hpp"

#include "localizable_plugin_units.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace localizable_dogfood {

using namespace gui_forms;
using namespace std::string_literals;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

// --- The program under the plugin -----------------------------------------

constexpr const char* settings_title = "Settings";
constexpr std::string_view search_hint = "Search files";
const std::string greeting = "Welcome";

// A helper of the developer's own, marked: literals passed to it are found.
void set_caption(Label& label, GUI_FORMS_LOCALIZABLE(text) std::string_view caption) {
    label.set_text(std::string(caption));
}

// The same helper unmarked: the literal reaches set_text only through the
// parameter, so it is not found.
void set_caption_unmarked(Label& label, std::string_view caption) {
    label.set_text(std::string(caption));
}

// u8 literals reach a char8_t parameter of a marked helper directly.
std::size_t set_u8_caption(GUI_FORMS_LOCALIZABLE(text) std::u8string_view caption) {
    return caption.size();
}

// Wide literals are not recorded.
std::size_t set_wide_caption(GUI_FORMS_LOCALIZABLE(text) std::wstring_view caption) {
    return caption.size();
}

template <typename ControlType>
void set_from_template(ControlType& control) {
    control.set_text("From a template");
}

struct Controls final {
    Control::Ptr root = make_control<Control>(StableId("localizable.root"));
    std::shared_ptr<Label> label = make_control<Label>(StableId("localizable.label"));
    std::shared_ptr<GroupBox> group = make_control<GroupBox>(StableId("localizable.group"));
    std::shared_ptr<Button> button = make_control<Button>(StableId("localizable.button"));
    std::shared_ptr<TextBox> box = make_control<TextBox>(StableId("localizable.box"));
    std::shared_ptr<ComboBox> combo = make_control<ComboBox>(StableId("localizable.combo"));
    std::shared_ptr<ListBox> list = make_control<ListBox>(StableId("localizable.list"));
};

void exercise(Controls& controls, Window& window, bool never, int mode) {
    Label& label = *controls.label;
    label.set_text("Ready");
    if (never) {
        label.set_text("Never shown");
    }
    switch (mode) {
    case 1:
        label.set_text("Mode one");
        break;
    default:
        // Every other mode keeps "Ready": the switch exists so the plugin
        // sees a literal in a case that the test never runs.
        break;
    }
    label.set_text(never ? "On" : "Off");
    label.set_text("Ready");
    label.set_text("Save " "all");
    label.set_text(R"(Path "C:\temp")");
    label.set_text(reinterpret_cast<const char*>(u8"Caf\u00e9"));
    label.set_text("Tab\there \xE2\x9C\x93 \u65e5\u672c");
    label.set_text("日本語のテキスト");
    label.set_text(settings_title);
    label.set_text(greeting);
    label.set_text("Literal operator"s);
    static_cast<void>(set_u8_caption(u8"Grüße"));
    static_cast<void>(set_wide_caption(L"Wide caption"));

    ToolTip tips(window);
    tips.set_tool_tip(controls.button, std::string(search_hint));
    (*controls.box).set_placeholder_text(std::string{search_hint});
    (*controls.button).accessible_name = "Close window";
    (*controls.button).set_text("OK");
    (*controls.combo).set_items({"Small", "Medium"});
    (*controls.combo).add_item("Large");
    (*controls.list).add_item(std::string("Extra"));

    // Not localizable: a text box's text is the user's.
    (*controls.box).set_text("User content");
    set_caption(label, "Helper caption");
    set_caption_unmarked(label, "Unmarked helper");
    label.set_text("Built at " + std::to_string(mode));

    set_from_template(label);
    set_from_template(*controls.group);

    MenuItemSpec open_item("file.open", MenuItemKind::command, {}, "Open");
    ToolStripItemSpec save{.stable_id = "tool.save", .text = "Save"};
    save.text = "Save as";
    const ListViewColumn name_column{"name", "Name"};
    // The shortcut is the key binding's text and stays unmarked.
    const RibbonToolTipContent paste_tip{.title = "Paste", .description = "Insert the clipboard",
                                         .shortcut = "Ctrl+V", .disabled_reason = "The clipboard is empty"};
    static_cast<void>(open_item);
    static_cast<void>(name_column);
    static_cast<void>(paste_tip);
}

// --- Expectations ---------------------------------------------------------

struct Expected final {
    std::string_view property;
    std::string_view text;
    std::string_view function;
    std::string_view file;
    std::size_t count;
};

constexpr std::string_view self = "tests/localizable_plugin_tests.cpp";
constexpr std::string_view exercise_name = "localizable_dogfood::exercise";

// Every record the program's tables hold, with how many sites hold it.
const std::vector<Expected>& expected_records() {
    static const std::vector<Expected> records{
        {"text", "Ready", exercise_name, self, 2U},
        {"text", "Never shown", exercise_name, self, 1U},
        {"text", "Mode one", exercise_name, self, 1U},
        {"text", "On", exercise_name, self, 1U},
        {"text", "Off", exercise_name, self, 1U},
        {"text", "Save all", exercise_name, self, 1U},
        {"text", "Path \"C:\\temp\"", exercise_name, self, 1U},
        {"text", "Caf\xC3\xA9", exercise_name, self, 1U},
        {"text", "Tab\there \xE2\x9C\x93 \xE6\x97\xA5\xE6\x9C\xAC", exercise_name, self, 1U},
        {"text", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE3\x81\xAE\xE3\x83\x86\xE3\x82\xAD\xE3\x82\xB9\xE3\x83\x88",
         exercise_name, self, 1U},
        {"text", "Settings", exercise_name, self, 1U},
        {"text", "Welcome", exercise_name, self, 1U},
        {"text", "Literal operator", exercise_name, self, 1U},
        {"text", "Gr\xC3\xBC\xC3\x9F" "e", exercise_name, self, 1U},
        {"tool_tip", "Search files", exercise_name, self, 1U},
        {"placeholder", "Search files", exercise_name, self, 1U},
        {"accessible_name", "Close window", exercise_name, self, 1U},
        {"text", "OK", exercise_name, self, 1U},
        {"items", "Small", exercise_name, self, 1U},
        {"items", "Medium", exercise_name, self, 1U},
        {"items", "Large", exercise_name, self, 1U},
        {"items", "Extra", exercise_name, self, 1U},
        {"text", "Helper caption", exercise_name, self, 1U},
        {"text", "From a template", "localizable_dogfood::set_from_template", self, 1U},
        {"text", "Open", exercise_name, self, 1U},
        {"text", "Save", exercise_name, self, 1U},
        {"text", "Save as", exercise_name, self, 1U},
        {"header", "Name", exercise_name, self, 1U},
        {"tool_tip", "Paste", exercise_name, self, 1U},
        {"tool_tip", "Insert the clipboard", exercise_name, self, 1U},
        {"tool_tip", "The clipboard is empty", exercise_name, self, 1U},
        {"text", "Static library text", "localizable_units::static_unit", "tests/localizable_plugin_static.cpp", 1U},
        {"title", "Shared library title", "localizable_units::shared_unit", "tests/localizable_plugin_shared.cpp",
         1U},
    };
    return records;
}

// Literals that must not be recorded anywhere.
constexpr std::string_view not_recorded[] = {
    "Plain unit text", "Wide caption", "User content", "Unmarked helper", "Built at ", "Ctrl+V",
};

std::string variant_name(std::string_view text) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const char byte : text) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 0x100000001b3ULL;
    }
    constexpr std::string_view digits = "0123456789abcdef";
    std::string name = "v-";
    for (int shift = 44; shift >= 0; shift -= 4) {
        name.push_back(digits[(hash >> static_cast<unsigned>(shift)) & 0xfU]);
    }
    return name;
}

bool is_test_file(std::string_view file) {
    return file.starts_with("tests/localizable_plugin");
}

// The literal's first token starts at the recorded line and column (1-based,
// in bytes) of the recorded file.
void require_location(const LocalizableLiteral& literal) {
    std::ifstream source(std::string(GUI_FORMS_LOCALIZABLE_TEST_SOURCE_DIR) + "/" + literal.file);
    require(source.good(), "cannot open the recorded file " + literal.file);
    std::string line;
    for (std::uint32_t number = 1U; number <= literal.line; ++number) {
        require(static_cast<bool>(std::getline(source, line)), "no line " + std::to_string(literal.line));
    }
    require(literal.column >= 1U && literal.column <= line.size(), "column out of range for " + literal.text);
    const std::string_view token = std::string_view(line).substr(literal.column - 1U);
    const bool starts_literal = token.starts_with("\"") || token.starts_with("R\"") || token.starts_with("u8\"") ||
                                token.starts_with("u8R\"");
    require(starts_literal, "the record of \"" + literal.text + "\" points at " + std::string(token.substr(0U, 20U)));
}

void check_program_tables() {
    const std::vector<LocalizableLiteral> literals = localizable_literals();
    for (const std::string_view text : not_recorded) {
        for (const LocalizableLiteral& literal : literals) {
            require(!literal.text.starts_with(text), "recorded \"" + literal.text + "\", which must not be");
        }
    }
#if defined(GUI_FORMS_LOCALIZABLE_TEST_PLUGIN)
    std::size_t matched = 0U;
    for (const Expected& expected : expected_records()) {
        std::size_t count = 0U;
        for (const LocalizableLiteral& literal : literals) {
            if (literal.property == expected.property && literal.text == expected.text &&
                literal.function == expected.function && literal.file == expected.file) {
                require(literal.name == variant_name(literal.text), "variant name of " + literal.text);
                require_location(literal);
                ++count;
            }
        }
        require(count == expected.count, "expected " + std::to_string(expected.count) + " record(s) of \"" +
                                             std::string(expected.text) + "\" (" + std::string(expected.property) +
                                             "), found " + std::to_string(count));
        matched += count;
    }
    std::size_t in_tests = 0U;
    for (const LocalizableLiteral& literal : literals) {
        if (is_test_file(literal.file)) {
            ++in_tests;
        } else {
            std::cout << "outside the test: " << literal.file << ':' << literal.line << ' ' << literal.text << '\n';
        }
    }
    require(in_tests == matched, "unexpected records: " + std::to_string(in_tests - matched));
    require(in_tests == literals.size(), "records outside the test's files");
    std::cout << "localizable records: " << literals.size() << '\n';
#else
    for (const LocalizableLiteral& literal : literals) {
        require(!is_test_file(literal.file), "a record without the plugin: " + literal.text);
    }
    std::cout << "built without the plugin: table reader checks only\n";
#endif
}

// --- The table reader -----------------------------------------------------

void put_u32(std::vector<std::byte>& out, std::uint32_t value) {
    for (unsigned shift = 0U; shift < 32U; shift += 8U) {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
    }
}

void put_text(std::vector<std::byte>& out, std::string_view text) {
    put_u32(out, static_cast<std::uint32_t>(text.size()));
    for (const char byte : text) out.push_back(static_cast<std::byte>(byte));
}

std::vector<std::byte> table(std::string_view text) {
    std::vector<std::byte> description;
    put_u32(description, 1U);
    put_text(description, "text");
    put_text(description, variant_name(text));
    put_text(description, "a.cpp");
    put_u32(description, 3U);
    put_u32(description, 7U);
    put_text(description, "f");
    put_text(description, text);
    std::vector<std::byte> blob;
    put_u32(blob, 9U);
    put_u32(blob, static_cast<std::uint32_t>(description.size()));
    put_u32(blob, 0x47464c01U);
    for (const char byte : std::string_view("GUIForms\0\0\0\0", 12U)) blob.push_back(static_cast<std::byte>(byte));
    blob.insert(blob.end(), description.begin(), description.end());
    while (blob.size() % 4U != 0U) blob.push_back(std::byte{0});
    return blob;
}

bool all_zero(std::span<const std::byte> bytes) {
    for (const std::byte byte : bytes) {
        if (byte != std::byte{0}) return false;
    }
    return true;
}

void check_reader() {
    std::vector<LocalizableLiteral> out;
    require(parse_localizable_tables({}, out) && out.empty(), "an empty section");

    std::vector<std::byte> section = table("Ready");
    section.insert(section.end(), 8U, std::byte{0});
    const std::vector<std::byte> second = table("Caf\xC3\xA9");
    section.insert(section.end(), second.begin(), second.end());
    section.insert(section.end(), 2U, std::byte{0});
    require(parse_localizable_tables(section, out), "two tables with zero padding");
    require(out.size() == 2U && out[0].text == "Ready" && out[1].text == "Caf\xC3\xA9", "the two records");
    require(out[0].property == "text" && out[0].file == "a.cpp" && out[0].line == 3U && out[0].column == 7U &&
                out[0].function == "f" && out[0].name == variant_name("Ready"),
            "the first record's fields");

    // The table is complete once its description ends (the header, the
    // 12-byte padded name, then the description size the header records);
    // only the trailing alignment padding follows.
    // The test table is far below 64 KiB, so the two low bytes of the
    // little-endian size are the whole of it.
    const std::size_t size_low = std::to_integer<unsigned char>(second[4]);
    const std::size_t size_high = std::to_integer<unsigned char>(second[5]);
    const std::size_t description_size = size_low + size_high * 256U;
    const std::size_t complete_size = 24U + description_size;
    require(complete_size <= second.size(), "the test table's recorded size");
    for (std::size_t size = 1U; size < complete_size; ++size) {
        const std::span<const std::byte> cut(second.data(), size);
        if (all_zero(cut)) continue;
        require(!parse_localizable_tables(cut, out), "a table cut at " + std::to_string(size));
        require(out.size() == 2U, "a refused section leaves the output");
    }
    // A complete table whose section ends before (or inside) its padding is
    // read: a section need not end on the alignment.
    for (std::size_t size = complete_size; size < second.size(); ++size) {
        const std::span<const std::byte> unpadded(second.data(), size);
        require(parse_localizable_tables(unpadded, out), "a complete table of " + std::to_string(size) + " bytes");
        require(out.size() == 3U && out[2].text == "Caf\xC3\xA9", "the complete table's record");
        out.pop_back();
    }
    std::vector<std::byte> foreign = table("Ready");
    // The section is shared with the effect verification records (type
    // 0x47464c02): a GUIForms note of another type is skipped, not refused.
    foreign[8] = std::byte{0x02};
    require(parse_localizable_tables(foreign, out) && out.size() == 2U, "a note of another type is skipped");
    std::vector<std::byte> huge = table("Ready");
    // The description (the record count first) follows the 12-byte header
    // and the 12-byte padded name.
    huge[24] = std::byte{0xff};
    huge[25] = std::byte{0xff};
    require(!parse_localizable_tables(huge, out), "a record count past the table");
    require(out.size() == 2U, "refused sections leave the output");
}

void run() {
    Controls controls;
    (*controls.root).add_child(controls.button);
    Window window(controls.root, {320.0, 200.0});
    exercise(controls, window, false, 0);
    localizable_units::plain_unit(*controls.label);
    localizable_units::static_unit(*controls.label);
    require(localizable_units::shared_unit() == "Shared library title", "the shared library's caption");
    check_reader();
    check_program_tables();
}

} // namespace localizable_dogfood

int main() {
    try {
        localizable_dogfood::run();
    } catch (const std::exception& error) {
        std::cerr << "localizable_plugin_tests: " << error.what() << '\n';
        return 1;
    }
    std::cout << "localizable_plugin_tests: passed\n";
    return 0;
}
