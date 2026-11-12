#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/xml/types/xml_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// The language files (translation design, revision B and the decisions
// settled after it; help system study, section 4): one `locales-<tag>.xml`
// per locale, in CSL locale XML read by the bounded XML reader.
//
//   <locale xml:lang="fr" name="Français" direction="ltr" version="3">
//     <controls>
//       <control name="main">
//         <control name="paste" text="Coller" tool-tip="Colle le presse-papiers"
//                  help-line="Colle le contenu du presse-papiers.">
//           <help keywords="presse-papiers; coller, image">Collez une image.</help>
//         </control>
//       </control>
//     </controls>
//   </locale>
//
// `<control>` elements nest by stable-ID segment and together name one text
// record. A record's fields are attributes, except the help body, which is
// the character data of a `<help>` child (so its line breaks survive) with
// `keywords` as that child's attribute. Control text is never element
// content, so no element is mixed.
//
// What this header covers: the record and its fields, the file's reader and
// writer, the four layers of a program's text (built in, program values, the
// shipped translation, the user's overrides), the template export and the
// way back to the built-in language. Not yet read: `<terms>` (messages),
// `<variant>` and `<item>` (runtime literals and authored list items),
// `<info>` and `<conventions>`; they are skipped without a diagnostic so
// files that carry them load.

// The fields of a text record, as the locale XML names them. The values are
// stable: they index LocaleRecord::fields and LocaleLimits.
enum class LocaleField : std::uint8_t {
    text,
    tool_tip,
    access_key,
    title,
    placeholder,
    header,
    accessible_name,
    accessible_description,
    help_line,
    help,
    keywords,
};

inline constexpr std::size_t locale_field_count = 11U;

// The field's name in the locale XML: "text", "tool-tip", "access-key",
// "title", "placeholder", "header", "accessible-name",
// "accessible-description", "help-line", "help" or "keywords". Static
// storage.
[[nodiscard]] GUI_FORMS_API std::string_view locale_field_name(LocaleField field) noexcept;
// The field a locale XML name names, or nothing.
[[nodiscard]] GUI_FORMS_API std::optional<LocaleField> find_locale_field(std::string_view name) noexcept;

// The bounds on one file's fields. A value past its field's bound rejects
// that field (LocaleRefusal::too_long), not the file; more records than
// maximum_records refuse the file. Lengths are bytes of UTF-8.
struct GUI_FORMS_API LocaleLimits final {
    // Indexed by LocaleField, in its order.
    std::array<std::size_t, locale_field_count> maximum_field_bytes{
        4096U,        // text
        4096U,        // tool-tip
        32U,          // access-key (one grapheme)
        1024U,        // title
        4096U,        // placeholder
        4096U,        // header
        1024U,        // accessible-name
        4096U,        // accessible-description
        2048U,        // help-line
        64U * 1024U,  // help
        4096U,        // keywords
    };
    std::size_t maximum_records{20000U};
};

// The XML reader's bounds for a locale file (translation design, revision
// B): 1 MiB, depth 256, 65,536 elements, 64 attributes, 256-byte names and
// 64 KiB values. The reader enforces the depth while it parses, before any
// walk of the tree, so every walk over a read file is bounded by it.
[[nodiscard]] GUI_FORMS_API XmlLimits locale_xml_limits() noexcept;

// One text record as one layer gives it. An absent field lets a lower
// layer's value show; a present field, even empty, hides it.
struct GUI_FORMS_API LocaleRecord final {
    std::string stable_id{};
    // Indexed by LocaleField.
    std::array<std::optional<std::string>, locale_field_count> fields{};

    friend bool operator==(const LocaleRecord& left, const LocaleRecord& right) noexcept {
        return left.stable_id == right.stable_id && left.fields == right.fields;
    }
};

// The field of `record` named by `field`. The reference borrows `record`.
[[nodiscard]] GUI_FORMS_API const std::optional<std::string>& locale_record_field(const LocaleRecord& record,
                                                                    LocaleField field) noexcept;
const std::optional<std::string>& locale_record_field(const LocaleRecord&& record, LocaleField field) = delete;
[[nodiscard]] GUI_FORMS_API std::optional<std::string>& locale_record_field(LocaleRecord& record, LocaleField field) noexcept;
// Whether no field is present.
[[nodiscard]] GUI_FORMS_API bool locale_record_empty(const LocaleRecord& record) noexcept;

// Why one field of one record was rejected. The record's other fields, and
// the file's other records, are kept.
enum class LocaleRefusal : std::uint8_t {
    // A `name` segment, or the joined ID, is not a stable ID: the subtree
    // under it is skipped.
    invalid_stable_id,
    // The value passes its LocaleLimits bound.
    too_long,
    // The value is not valid UTF-8 or holds NUL.
    invalid_text,
    // The value holds a C0 control other than tab and line feed, or DEL.
    control_character,
    // A bidirectional embedding, override or isolate is not closed in its
    // line, or a closing control has nothing to close.
    unbalanced_bidi,
    // The keyword list has an empty keyword or more than one subkeyword.
    invalid_keywords,
    // The access key is not exactly one grapheme, or is white space.
    invalid_access_key,
    // The record appears twice in the file; the later one is dropped.
    duplicate_record,
};

[[nodiscard]] GUI_FORMS_API std::string_view locale_refusal_name(LocaleRefusal refusal) noexcept;

struct GUI_FORMS_API LocaleRejection final {
    std::string stable_id{};
    LocaleField field{LocaleField::text};
    LocaleRefusal reason{LocaleRefusal::invalid_text};
    // What was wrong, for logs (the offending segment or keyword list);
    // empty when the reason says it all.
    std::string detail{};
};

// Checks one value against its field's rules and bound: the refusal, or
// nothing when the value is acceptable. Display text only: no C0 controls
// but tab and line feed, balanced bidirectional controls, a well-formed
// keyword list for `keywords` and one grapheme for `access-key`.
[[nodiscard]] GUI_FORMS_API std::optional<LocaleRefusal> check_locale_value(LocaleField field, std::string_view value,
                                                              const LocaleLimits& limits = {});

// The records of one layer, sorted by stable ID with each ID once. Owns its
// records. Not thread-safe; one owner mutates it.
class GUI_FORMS_API LocaleLayer final {
public:
    // The record with this ID, or null. The pointer borrows this layer and
    // is invalidated by any change to it; a temporary layer is refused.
    [[nodiscard]] const LocaleRecord* find(std::string_view stable_id) const& noexcept;
    const LocaleRecord* find(std::string_view stable_id) const&& = delete;
    // Sets one field of the record `stable_id`, creating the record. Throws
    // std::invalid_argument, changing nothing, for an ID that is not a
    // stable ID or a value check_locale_value refuses under default limits.
    void set(std::string_view stable_id, LocaleField field, std::string value);
    // Removes one field; a record with no field left is removed. Returns
    // whether the field was present.
    bool reset(std::string_view stable_id, LocaleField field);
    // Puts `record` in, replacing a record with the same ID. Throws
    // std::invalid_argument, changing nothing, when its ID is not a stable
    // ID. An empty record removes the ID.
    void put(LocaleRecord record);
    // Replaces every record with `records`, which must be sorted by stable
    // ID with each ID once and each a stable ID (std::invalid_argument,
    // changing nothing, otherwise). Empty records are dropped.
    void assign_sorted(std::vector<LocaleRecord> records);
    // Every field `over` gives replaces this layer's; the others stay. One
    // pass over both sorted layers.
    void merge_over(const LocaleLayer& over);
    [[nodiscard]] std::span<const LocaleRecord> entries() const& noexcept {
        return std::span<const LocaleRecord>(records_.data(), records_.size());
    }
    std::span<const LocaleRecord> entries() const&& = delete;
    [[nodiscard]] std::size_t size() const noexcept { return records_.size(); }
    [[nodiscard]] bool empty() const noexcept { return records_.empty(); }
    // Ends every record's lifetime; capacity remains.
    void clear() noexcept { records_.clear(); }

    friend bool operator==(const LocaleLayer& left, const LocaleLayer& right) noexcept {
        return left.records_ == right.records_;
    }

private:
    // The position of the first record not before `stable_id`.
    [[nodiscard]] std::size_t lower_bound(std::string_view stable_id) const noexcept;

    std::vector<LocaleRecord> records_{};
};

enum class LocaleDirection : std::uint8_t { left_to_right, right_to_left };

// The `<locale>` element's attributes.
struct GUI_FORMS_API LocaleFileInfo final {
    // `xml:lang`: a language tag, 2 to 8 letters then `-` and 1 to 8
    // letters or digits per subtag ("fr", "es-mx", "zh-hant-tw").
    std::string language{};
    // `name`: the language's own name for the language list; may be empty.
    std::string name{};
    // `version`: the file's own version text; may be empty.
    std::string version{};
    // `direction`: "ltr" (the default) or "rtl".
    LocaleDirection direction{LocaleDirection::left_to_right};
};

// Whether `tag` has the language tag shape LocaleFileInfo::language states.
[[nodiscard]] GUI_FORMS_API bool valid_language_tag(std::string_view tag) noexcept;

// Why a whole locale file was refused; none when it was read (some fields
// may still have been rejected).
enum class LocaleFileError : std::uint8_t {
    none,
    // The XML reader refused the file (LocaleReadResult::xml_error).
    xml,
    // The root element is not `<locale>`.
    wrong_root,
    // The root has no `xml:lang`, or an empty one.
    no_language,
    // `xml:lang` is not a language tag.
    invalid_language,
    // `direction` is neither "ltr" nor "rtl".
    invalid_direction,
    // The file holds more records than LocaleLimits::maximum_records.
    too_many_records,
    // The file is for another language than the layer it was offered to
    // (LocaleCatalog).
    language_mismatch,
};

[[nodiscard]] GUI_FORMS_API std::string_view locale_file_error_name(LocaleFileError error) noexcept;

// The most ignored-name diagnostics one read keeps.
inline constexpr std::size_t locale_maximum_ignored_diagnostics = 256U;

struct GUI_FORMS_API LocaleReadResult final {
    LocaleFileError error{LocaleFileError::none};
    XmlError xml_error{XmlError::none};
    XmlSourcePosition position{};
    LocaleFileInfo info{};
    // The records the file gives. Empty on a refused file.
    LocaleLayer records{};
    // The fields that were rejected one by one. Empty on a refused file.
    std::vector<LocaleRejection> rejections{};
    // Attributes and elements the reader does not know, as "<record>@<name>"
    // (the record's ID, empty above the first control), at most
    // locale_maximum_ignored_diagnostics of them. They change nothing.
    std::vector<std::string> ignored{};

    [[nodiscard]] explicit operator bool() const noexcept { return error == LocaleFileError::none; }
};

// The `<help>` element's character data as a help body: a leading line end
// and the trailing white space go, and the indentation every non-blank line
// shares is removed, so a body indented to sit under its `<control>` reads
// as written. `body` is replaced.
GUI_FORMS_API void normalize_locale_help_body(std::string_view raw, std::string& body);

// Reads a `locales-<tag>.xml` file. Expected faults never throw: a refused
// file gives its error, rejected fields are listed and left out. The walk
// over `<control>` elements recurses once per nesting level and is bounded
// by xml_limits.maximum_depth, which the XML reader enforced before the
// walk began.
[[nodiscard]] GUI_FORMS_API LocaleReadResult read_locale(std::string_view utf8, const LocaleLimits& limits = {},
                                           const XmlLimits& xml_limits = locale_xml_limits());

// Writes `records` as a file read_locale accepts: `<control>` elements
// nested by segment, in segment order. The result's error is the XML
// writer's (an ID with more segments than the depth bound gives
// XmlError::depth_exceeded). A record whose `help` is present but empty
// and has no `keywords` writes no `<help>`: an empty `<help>` element gives
// no body when read.
[[nodiscard]] GUI_FORMS_API XmlWriteResult write_locale(const LocaleLayer& records, const LocaleFileInfo& info,
                                          const XmlLimits& xml_limits = locale_xml_limits());

// The layers of a program's text, lowest first.
enum class LocaleLayerKind : std::uint8_t {
    // The program's built-in language: its embedded built-in locale file.
    built_in,
    // Values the program set in code on its controls (Control::help_line,
    // help and keywords), which win over the built-in file's.
    program,
    // The shipped translation of the chosen language, if any.
    translation,
    // The user's own `locales-<tag>.xml` for the language in force, if any.
    user,
};

// One resolved field: its text and the layer it came from. `fallback` is
// set when a translation is in force and the field came from the built-in
// or program layer, so a page can mark it.
struct GUI_FORMS_API LocaleResolvedField final {
    std::string text{};
    std::optional<LocaleLayerKind> origin{};
    bool fallback{};
};

// The text store of one program: its four layers, the language in force
// and the untranslatable way back. The program owns it. One execution
// context (the UI thread); a refused load changes nothing. On an accepted
// load the result's records are in the catalog and the result's own are
// left empty; its info, rejections and ignored names stay.
class GUI_FORMS_API LocaleCatalog final {
public:
    // `built_in_language` is the built-in file's language tag
    // (std::invalid_argument when it is not a tag).
    explicit LocaleCatalog(std::string built_in_language = "en");

    [[nodiscard]] const LocaleLayer& layer(LocaleLayerKind which) const& noexcept;
    const LocaleLayer& layer(LocaleLayerKind which) const&& = delete;
    [[nodiscard]] const std::string& built_in_language() const noexcept { return built_in_language_; }
    [[nodiscard]] const LocaleFileInfo& translation_info() const noexcept { return translation_info_; }
    // The translation's language while one is set, else the built-in one.
    [[nodiscard]] const std::string& current_language() const noexcept;

    // Reads `utf8` as (part of) the built-in file and merges its fields over
    // the built-in layer. A file for another language than
    // built_in_language() is refused (language_mismatch).
    LocaleReadResult load_built_in(std::string_view utf8);
    // Replaces the program layer. Returns whether it changed; an equal
    // layer changes nothing, not even the revision.
    bool set_program_values(LocaleLayer records);
    // Replaces the translation with `utf8`'s records and info. User
    // overrides for another language are dropped.
    LocaleReadResult load_translation(std::string_view utf8);
    // Removes the translation, and user overrides for its language.
    void clear_translation();
    // Replaces the user layer with `utf8`'s records. A file for another
    // language than current_language() is refused (language_mismatch).
    LocaleReadResult load_user_overrides(std::string_view utf8);
    void clear_user_overrides();
    // The untranslatable way back (translation design 8): while set, only
    // the built-in and program layers resolve, whatever the translation and
    // the user's file say.
    void set_built_in_only(bool built_in_only) noexcept;
    [[nodiscard]] bool built_in_only() const noexcept { return built_in_only_; }

    // Resolves one field of `stable_id` from the highest layer in force
    // that gives it.
    [[nodiscard]] LocaleResolvedField resolve(std::string_view stable_id, LocaleField field) const;
    // Fills `ids` (cleared first) with every ID a layer in force holds,
    // sorted and unique.
    void collect_ids(std::vector<std::string>& ids) const;
    // Grows by one on every change.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    LocaleLayer built_in_{};
    LocaleLayer program_{};
    LocaleLayer translation_{};
    LocaleLayer user_{};
    std::string built_in_language_{};
    LocaleFileInfo translation_info_{};
    std::uint64_t revision_{};
    bool built_in_only_{};
};

// The template export (translation design 7, help system study section 4):
// every record of the built-in and program layers with the built-in text as
// values and, when `language` is the translation's, its translations over
// them, so a translator continues an existing translation. User overrides
// are never exported. Fills `records` (cleared first).
GUI_FORMS_API void locale_template_records(const LocaleCatalog& catalog, std::string_view language, LocaleLayer& records);
// locale_template_records written as a locale file for `language` (the
// built-in language when empty).
[[nodiscard]] GUI_FORMS_API XmlWriteResult export_locale_template(const LocaleCatalog& catalog, std::string_view language,
                                                    const XmlLimits& xml_limits = locale_xml_limits());

// The way back from outside the program (translation design 8): whether
// the command line holds `--gui-forms-locale=builtin` (or
// `--gui-forms-locale builtin`), or the GUI_FORMS_LOCALE environment value
// the caller read is "builtin". The caller then sets
// LocaleCatalog::set_built_in_only.
[[nodiscard]] GUI_FORMS_API bool locale_rescue_requested(std::span<const std::string_view> arguments,
                                           std::string_view environment_value) noexcept;

} // namespace gui_forms
