// The language files (translation design, revision B and its settled
// decisions; help system study, section 4; roadmap row 2.13): every field
// of the text record read and written, per-field rejections (control
// characters, unbalanced bidirectional controls, keywords, access keys,
// bounds, repeated records, bad segments), whole-file refusals, the depth
// bound enforced by the XML reader before the walk, segment-ordered writing,
// the four layers and their precedence, the language rule for user files,
// the way back, the template export and the rescue switch.

#include "gui_forms/gui_forms.hpp"
#include "gui_forms/text/locale_file/locale_file.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

[[nodiscard]] bool field_is(const LocaleRecord* record, LocaleField field, std::string_view expected) {
    if (record == nullptr) return false;
    const std::optional<std::string>& value = locale_record_field(*record, field);
    const bool same = value.has_value() && *value == expected;
    return same;
}

[[nodiscard]] bool rejected(const LocaleReadResult& read, std::string_view stable_id, LocaleField field,
                            LocaleRefusal reason) {
    for (const LocaleRejection& rejection : read.rejections) {
        if (rejection.stable_id == stable_id && rejection.field == field && rejection.reason == reason) return true;
    }
    return false;
}

constexpr std::string_view built_in_file =
    "<locale xml:lang=\"en\" name=\"English\" version=\"7\">\n"
    "  <info><translator name=\"The program\"/></info>\n"
    "  <terms><term name=\"saved-file\">Saved {name}</term></terms>\n"
    "  <controls>\n"
    "    <control name=\"main\" text=\"Sketch\" title=\"Sketch window\" help-line=\"The drawing window.\">\n"
    "      <help keywords=\"window; drawing, canvas\">\n"
    "        Sketch draws on a canvas.\n"
    "\n"
    "            indented code\n"
    "      </help>\n"
    "      <control name=\"paste\" text=\"Paste\" tool-tip=\"Pastes the clipboard\" access-key=\"P\"\n"
    "               accessible-name=\"Paste button\" accessible-description=\"Pastes what you copied\"\n"
    "               where=\"Edit menu\" mystery=\"x\"/>\n"
    "      <control name=\"find\" placeholder=\"Search\" header=\"Find\"/>\n"
    "      <variant name=\"v-0123456789ab\" text=\"Ready\"/>\n"
    "    </control>\n"
    "  </controls>\n"
    "</locale>\n";

void reads_every_field() {
    const LocaleReadResult read = read_locale(built_in_file);
    require(static_cast<bool>(read) && read.rejections.empty(), "the built-in file reads");
    require(read.info.language == "en" && read.info.name == "English" && read.info.version == "7" &&
                read.info.direction == LocaleDirection::left_to_right,
            "the locale element's attributes are the file's info");
    require(read.records.size() == 3U, "three records: main, main.paste, main.find");
    const LocaleRecord* main_record = read.records.find("main");
    require(field_is(main_record, LocaleField::text, "Sketch") && field_is(main_record, LocaleField::title, "Sketch window") &&
                field_is(main_record, LocaleField::help_line, "The drawing window.") &&
                field_is(main_record, LocaleField::keywords, "window; drawing, canvas") &&
                field_is(main_record, LocaleField::help, "Sketch draws on a canvas.\n\n    indented code"),
            "a record's attributes and its help child are its fields; the body is dedented");
    const LocaleRecord* paste = read.records.find("main.paste");
    require(field_is(paste, LocaleField::tool_tip, "Pastes the clipboard") && field_is(paste, LocaleField::access_key, "P") &&
                field_is(paste, LocaleField::accessible_name, "Paste button") &&
                field_is(paste, LocaleField::accessible_description, "Pastes what you copied") &&
                !locale_record_field(*paste, LocaleField::help).has_value(),
            "every attribute field is read; a missing field stays absent");
    const LocaleRecord* find = read.records.find("main.find");
    require(field_is(find, LocaleField::placeholder, "Search") && field_is(find, LocaleField::header, "Find"),
            "placeholder and header are fields");
    require(read.ignored.size() == 1U && read.ignored[0] == "main.paste@mystery",
            "an unknown attribute is reported once; skeleton metadata, terms, info and variants are not");
    for (std::size_t index = 0U; index < locale_field_count; ++index) {
        const LocaleField field = static_cast<LocaleField>(index);
        require(find_locale_field(locale_field_name(field)) == field, "every field's name finds the field");
    }
}

void rejects_fields_one_by_one() {
    LocaleLimits limits{};
    limits.maximum_field_bytes[static_cast<std::size_t>(LocaleField::title)] = 8U;
    const LocaleReadResult read = read_locale(
        "<locale xml:lang=\"de\"><controls>"
        "<control name=\"a\" text=\"Delete&#127;\" tool-tip=\"Fine\"/>"
        "<control name=\"b\" text=\"\xE2\x80\xAB" "open\" title=\"far too long\"/>"
        "<control name=\"c\" access-key=\"OK\"><help keywords=\"one;;two\">Body</help></control>"
        "<control name=\"d\" access-key=\" \" text=\"Line&#10;break\"/>"
        "<control name=\"Bad_Name\" text=\"x\"><control name=\"inside\" text=\"y\"/></control>"
        "<control name=\"a\" text=\"Again\"/>"
        "</controls></locale>",
        limits);
    require(static_cast<bool>(read), "a file with faulty fields is still read");
    require(rejected(read, "a", LocaleField::text, LocaleRefusal::control_character) &&
                field_is(read.records.find("a"), LocaleField::tool_tip, "Fine"),
            "a C0 control rejects its field and keeps the record's others");
    require(rejected(read, "b", LocaleField::text, LocaleRefusal::unbalanced_bidi),
            "an embedding without its PDF is rejected");
    require(rejected(read, "b", LocaleField::title, LocaleRefusal::too_long), "a value past its bound is rejected");
    require(rejected(read, "c", LocaleField::access_key, LocaleRefusal::invalid_access_key) &&
                rejected(read, "c", LocaleField::keywords, LocaleRefusal::invalid_keywords) &&
                field_is(read.records.find("c"), LocaleField::help, "Body"),
            "two graphemes and an empty keyword are rejected; the body stays");
    require(rejected(read, "d", LocaleField::access_key, LocaleRefusal::invalid_access_key) &&
                field_is(read.records.find("d"), LocaleField::text, "Line\nbreak"),
            "a space is no access key; a line feed is display text");
    require(rejected(read, "", LocaleField::text, LocaleRefusal::invalid_stable_id) &&
                read.records.find("bad_name.inside") == nullptr,
            "a bad segment skips its subtree");
    require(rejected(read, "a", LocaleField::text, LocaleRefusal::duplicate_record) &&
                !locale_record_field(*read.records.find("a"), LocaleField::text).has_value(),
            "a repeated record is rejected and the first kept");
    require(check_locale_value(LocaleField::text, "\xE2\x81\xA6" "isolated\xE2\x81\xA9") == std::nullopt &&
                check_locale_value(LocaleField::text, "\xE2\x81\xA6" "line\nend\xE2\x81\xA9") ==
                    LocaleRefusal::unbalanced_bidi &&
                check_locale_value(LocaleField::text, "\xE2\x81\xA9") == LocaleRefusal::unbalanced_bidi,
            "an isolate closes within its line, and a lone PDI is unbalanced");
    require(check_locale_value(LocaleField::access_key, "\xC3\xA9") == std::nullopt &&
                check_locale_value(LocaleField::access_key, "e\xCC\x81") == std::nullopt,
            "one grapheme is an access key, combining marks included");
}

void refuses_whole_files() {
    require(read_locale("<language xml:lang=\"en\"/>").error == LocaleFileError::wrong_root, "the root is <locale>");
    require(read_locale("<locale/>").error == LocaleFileError::no_language, "xml:lang is required");
    require(read_locale("<locale xml:lang=\"english language\"/>").error == LocaleFileError::invalid_language,
            "xml:lang is a language tag");
    require(read_locale("<locale xml:lang=\"ar\" direction=\"up\"/>").error == LocaleFileError::invalid_direction,
            "direction is ltr or rtl");
    const LocaleReadResult arabic = read_locale("<locale xml:lang=\"ar-eg\" direction=\"rtl\"/>");
    require(static_cast<bool>(arabic) && arabic.info.direction == LocaleDirection::right_to_left, "rtl is read");
    LocaleLimits limits{};
    limits.maximum_records = 1U;
    const LocaleReadResult crowded = read_locale(
        "<locale xml:lang=\"en\"><controls><control name=\"a\" text=\"A\"/><control name=\"b\" text=\"B\"/>"
        "</controls></locale>",
        limits);
    require(crowded.error == LocaleFileError::too_many_records && crowded.records.empty(),
            "more records than the bound refuse the file");
    // The walk recurses once per level; the XML reader refuses a deeper
    // document while it parses, so the walk never starts.
    std::string deep = "<locale xml:lang=\"en\"><controls>";
    for (int level = 0; level < 300; ++level) deep += "<control name=\"n\">";
    for (int level = 0; level < 300; ++level) deep += "</control>";
    deep += "</controls></locale>";
    const LocaleReadResult too_deep = read_locale(deep);
    require(too_deep.error == LocaleFileError::xml && too_deep.xml_error == XmlError::depth_exceeded,
            "a file deeper than the locale bound is refused before the walk");
    require(locale_xml_limits().maximum_depth == 256U && locale_xml_limits().maximum_input_bytes == 1024U * 1024U,
            "the locale bounds are the translation design's");
}

void writes_in_segment_order() {
    LocaleLayer layer;
    layer.set("a.b.c", LocaleField::text, "C");
    layer.set("a.b-x", LocaleField::text, "X");
    layer.set("a.b", LocaleField::text, "B");
    layer.set("a.b", LocaleField::help, "Body\nwith two lines");
    layer.set("a", LocaleField::keywords, "alpha");
    layer.set("a.b", LocaleField::tool_tip, "Line one\nline two");
    LocaleFileInfo info{};
    info.language = "en";
    info.direction = LocaleDirection::right_to_left;
    const XmlWriteResult written = write_locale(layer, info);
    require(static_cast<bool>(written), "the layer writes");
    const LocaleReadResult reread = read_locale(written.text);
    require(static_cast<bool>(reread) && reread.rejections.empty() && reread.records == layer &&
                reread.info.direction == LocaleDirection::right_to_left,
            "the file reads back to the same records: a.b's children nest under it, a.b-x beside it");
    require(written.text.find("&#10;") != std::string::npos, "an attribute's line feed is written as a reference");
    const LocaleRecord* alpha = reread.records.find("a");
    require(alpha != nullptr && !locale_record_field(*alpha, LocaleField::help).has_value() &&
                field_is(alpha, LocaleField::keywords, "alpha"),
            "keywords without a body round-trip without inventing one");
    bool refused = false;
    try {
        layer.set("a", LocaleField::text, "tab\tok but bell \x07 not");
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && !locale_record_field(*layer.find("a"), LocaleField::text).has_value(),
            "set refuses what the reader would reject, changing nothing");
}

constexpr std::string_view french_file =
    "<locale xml:lang=\"fr\" name=\"Fran\xC3\xA7" "ais\"><controls><control name=\"main\" text=\"Croquis\">"
    "<control name=\"paste\" text=\"Coller\"/></control></controls></locale>";

constexpr std::string_view french_user_file =
    "<locale xml:lang=\"fr\"><controls><control name=\"main\"><control name=\"paste\" text=\"Coller ici\" "
    "help-line=\"Mon aide.\"/></control></controls></locale>";

void catalog_layers() {
    LocaleCatalog catalog;
    require(static_cast<bool>(catalog.load_built_in(built_in_file)), "the built-in file loads");
    require(catalog.load_built_in(french_file).error == LocaleFileError::language_mismatch,
            "a built-in file for another language is refused");
    LocaleLayer program;
    program.set("main.paste", LocaleField::help_line, "Set in code.");
    require(catalog.set_program_values(program), "program values change the catalog");
    const std::uint64_t revision = catalog.revision();
    require(!catalog.set_program_values(program) && catalog.revision() == revision,
            "equal program values change nothing");
    require(catalog.load_user_overrides(french_user_file).error == LocaleFileError::language_mismatch,
            "a user file must be for the language in force");
    require(static_cast<bool>(catalog.load_translation(french_file)) && catalog.current_language() == "fr",
            "the translation sets the language in force");
    require(static_cast<bool>(catalog.load_user_overrides(french_user_file)), "the French user file loads");

    const LocaleResolvedField text = catalog.resolve("main.paste", LocaleField::text);
    require(text.text == "Coller ici" && text.origin == LocaleLayerKind::user, "the user's file wins");
    const LocaleResolvedField title = catalog.resolve("main", LocaleField::text);
    require(title.text == "Croquis" && title.origin == LocaleLayerKind::translation && !title.fallback,
            "the translation is next");
    const LocaleResolvedField tool_tip = catalog.resolve("main.paste", LocaleField::tool_tip);
    require(tool_tip.origin == LocaleLayerKind::built_in && tool_tip.fallback,
            "an untranslated field falls back to the built-in text, marked");
    const LocaleResolvedField line = catalog.resolve("main.paste", LocaleField::help_line);
    require(line.text == "Mon aide." && line.origin == LocaleLayerKind::user, "help fields layer like the others");

    catalog.set_built_in_only(true);
    const LocaleResolvedField rescued = catalog.resolve("main.paste", LocaleField::help_line);
    require(rescued.text == "Set in code." && rescued.origin == LocaleLayerKind::program && !rescued.fallback,
            "the way back shows the program's own text");
    require(catalog.resolve("main.paste", LocaleField::text).text == "Paste", "and the built-in file's");
    catalog.set_built_in_only(false);

    std::vector<std::string> ids;
    catalog.collect_ids(ids);
    require(ids == std::vector<std::string>({"main", "main.find", "main.paste"}), "the IDs of every layer, once");

    // The template for French continues the translation over the built-in
    // text; the user's file never leaves.
    LocaleLayer records;
    locale_template_records(catalog, "fr", records);
    require(field_is(records.find("main"), LocaleField::text, "Croquis") &&
                field_is(records.find("main.paste"), LocaleField::tool_tip, "Pastes the clipboard") &&
                field_is(records.find("main.paste"), LocaleField::help_line, "Set in code."),
            "the French template holds translations over the built-in and program text");
    const XmlWriteResult exported = export_locale_template(catalog, "fr");
    require(static_cast<bool>(exported) && exported.text.find("Coller ici") == std::string::npos &&
                exported.text.find("Mon aide.") == std::string::npos,
            "the template never carries the user's overrides");
    const LocaleReadResult reread = read_locale(exported.text);
    require(static_cast<bool>(reread) && reread.info.language == "fr" && reread.info.name == "Fran\xC3\xA7" "ais",
            "the template is a French locale file with the translation's name");
    locale_template_records(catalog, "", records);
    require(field_is(records.find("main"), LocaleField::text, "Sketch"), "the built-in template has built-in text");

    // Removing the files is a way back too.
    catalog.clear_user_overrides();
    require(catalog.resolve("main.paste", LocaleField::text).text == "Coller", "without the user's file");
    catalog.clear_translation();
    require(catalog.current_language() == "en" && catalog.resolve("main.paste", LocaleField::text).text == "Paste",
            "without the translation");
    require(static_cast<bool>(catalog.load_translation(french_file)) &&
                static_cast<bool>(catalog.load_user_overrides(french_user_file)),
            "the French files load again");
    const LocaleReadResult german = catalog.load_translation("<locale xml:lang=\"de\"/>");
    require(static_cast<bool>(german) && catalog.layer(LocaleLayerKind::user).empty(),
            "another translation drops the user's file for the old language");
}

void rescue_switch() {
    const std::vector<std::string_view> joined{"program", "--gui-forms-locale=builtin"};
    const std::vector<std::string_view> split{"program", "--gui-forms-locale", "builtin"};
    const std::vector<std::string_view> plain{"program", "--gui-forms-locale"};
    require(locale_rescue_requested(joined, {}) && locale_rescue_requested(split, {}) &&
                !locale_rescue_requested(plain, {}) && locale_rescue_requested(plain, "builtin") &&
                !locale_rescue_requested(plain, "fr"),
            "the command line or GUI_FORMS_LOCALE asks for the built-in language");
    require(valid_language_tag("zh-hant-tw") && valid_language_tag("es-419") && !valid_language_tag("e") &&
                !valid_language_tag("en_US") && !valid_language_tag("en-") && !valid_language_tag("123"),
            "language tags have the stated shape");
}

} // namespace

int main() {
    reads_every_field();
    rejects_fields_one_by_one();
    refuses_whole_files();
    writes_in_segment_order();
    catalog_layers();
    rescue_switch();
    std::cout << "locale file tests passed\n";
    return 0;
}
