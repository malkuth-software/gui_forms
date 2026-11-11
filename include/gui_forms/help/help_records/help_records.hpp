#pragma once

#include "gui_forms/text/locale_file/locale_file.hpp"
#include "gui_forms/xml/types/xml_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// Help is text (help system study, revision 1): three fields on the program's
// stable-ID text records, beside `text` and `tool-tip`, compiled with the
// language files.
//
// - `help-line`: one sentence, shown by What's This, the window help card
//   and under a contents entry.
// - `help`: the full help body, one markdown string in the stx_rtf2md
//   dialect; a link whose destination is a stable ID is a topic link.
// - `keywords`: index keywords. ';' separates keywords and ',' separates a
//   keyword from its subkeyword.
//
// A record's `text` is its title in the help contents. The records of one
// language form a layer (HelpRecordLayer); a HelpCatalog stacks the built-in
// layer, a translation and a user's overrides and resolves each field from
// the highest layer that gives it.
//
// These types are the help view of the language files
// (text/locale_file/locale_file.hpp): the same records, keeping the four
// help fields only. read_help_locale and write_help_locale are read_locale
// and write_locale projected onto them, so there is one reader and one
// format; HelpCatalog::adopt_locale takes a LocaleCatalog's layers.

enum class HelpField : std::uint8_t { title, help_line, help, keywords };

inline constexpr std::size_t help_field_count = 4U;

// The registered name of a field, as the locale XML spells it: "text",
// "help-line", "help" or "keywords". Static storage.
[[nodiscard]] std::string_view help_field_name(HelpField field) noexcept;

// The bounds a layer enforces on its fields; each, when exceeded, rejects
// the one field (HelpEntryRefusal::too_long), not the file. Lengths are in
// bytes of UTF-8. maximum_records bounds a layer's records and rejects a
// whole locale file past it.
struct HelpFieldLimits final {
    std::size_t maximum_title_bytes{1024U};
    std::size_t maximum_help_line_bytes{2048U};
    std::size_t maximum_help_bytes{64U * 1024U};
    std::size_t maximum_keywords_bytes{4096U};
    std::size_t maximum_records{20000U};
};

// One record's help fields as one layer gives them. A field the layer does
// not give is absent, so a lower layer's value shows through; an empty
// value that is present hides the lower layers' value.
struct HelpRecordEntry final {
    std::string stable_id{};
    std::optional<std::string> title{};
    std::optional<std::string> help_line{};
    std::optional<std::string> help{};
    std::optional<std::string> keywords{};
};

// The field of `entry` named by `field`. The reference borrows `entry`.
[[nodiscard]] const std::optional<std::string>& help_entry_field(const HelpRecordEntry& entry,
                                                                 HelpField field) noexcept;
[[nodiscard]] std::optional<std::string>& help_entry_field(HelpRecordEntry& entry, HelpField field) noexcept;

// Why one field of one record was rejected. The record's other fields, and
// the file's other records, are kept.
enum class HelpEntryRefusal : std::uint8_t {
    // A `name` segment, or the joined ID, is not a stable ID: the subtree
    // under it is skipped.
    invalid_stable_id,
    // The value passes its HelpFieldLimits bound.
    too_long,
    // The value is not valid UTF-8 or holds NUL.
    invalid_text,
    // The keyword list has an empty keyword or a keyword with more than one
    // subkeyword.
    invalid_keywords,
    // The record appears twice in the file; the later one is dropped.
    duplicate_record,
    // The help body could not be parsed as markdown (the helpbook pass).
    markdown_fault,
    // A topic link names a record that does not exist (the helpbook pass).
    broken_link,
    // An image names no PNG resource of the program (the helpbook pass).
    missing_image,
    // The value holds a C0 control other than tab and line feed, or DEL
    // (LocaleRefusal::control_character).
    control_character,
    // A bidirectional control is not closed in its line
    // (LocaleRefusal::unbalanced_bidi).
    unbalanced_bidi,
};

[[nodiscard]] std::string_view help_entry_refusal_name(HelpEntryRefusal refusal) noexcept;

struct HelpEntryRejection final {
    std::string stable_id{};
    HelpField field{HelpField::help};
    HelpEntryRefusal reason{HelpEntryRefusal::invalid_text};
    // What was wrong, for logs: the link target, the image name, the
    // offending segment. Empty when the reason says it all.
    std::string detail{};
};

// The records of one language, sorted by stable ID with each ID once. Owns
// its records. Not thread-safe; one owner mutates it.
class HelpRecordLayer final {
public:
    // The record with this ID, or null. The pointer borrows this layer and
    // is invalidated by any change to it; a temporary layer is refused.
    [[nodiscard]] const HelpRecordEntry* find(std::string_view stable_id) const& noexcept;
    const HelpRecordEntry* find(std::string_view stable_id) const&& = delete;
    // Sets one field of the record `stable_id`, creating the record. Throws
    // std::invalid_argument, changing nothing, for an ID that is not a
    // stable ID or a value that is not valid UTF-8 or holds NUL.
    void set(std::string_view stable_id, HelpField field, std::string value);
    // Removes one field; a record with no field left is removed. Returns
    // whether the field was present.
    bool reset(std::string_view stable_id, HelpField field);
    // Puts `entry` in, replacing a record with the same ID. The entry's ID
    // must be a stable ID (std::invalid_argument otherwise).
    void put(HelpRecordEntry entry);
    // Every field `over` gives replaces this layer's; the others stay.
    void merge_over(const HelpRecordLayer& over);
    [[nodiscard]] std::span<const HelpRecordEntry> entries() const& noexcept { return entries_; }
    std::span<const HelpRecordEntry> entries() const&& = delete;
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    // Ends every record's lifetime; capacity remains.
    void clear() noexcept { entries_.clear(); }

private:
    // The position of the first record not before `stable_id`.
    [[nodiscard]] std::size_t lower_bound(std::string_view stable_id) const noexcept;

    std::vector<HelpRecordEntry> entries_{};
};

// One keyword of a `keywords` field: the keyword and, when the entry has a
// ',', its subkeyword. Both are trimmed of spaces.
struct HelpKeyword final {
    std::string keyword{};
    std::string subkeyword{};
};

// Fills `keywords` (cleared first) from a `keywords` field. Returns false,
// leaving `keywords` empty, when an entry is empty or has more than one ','.
// An empty or all-space field has no keywords and is valid.
[[nodiscard]] bool parse_help_keywords(std::string_view field, std::vector<HelpKeyword>& keywords);

// normalize_locale_help_body, under its help name.
void normalize_help_body(std::string_view raw, std::string& body);

// Why a whole locale file was refused; none when it was read (some entries
// may still have been rejected).
enum class HelpLocaleError : std::uint8_t {
    none,
    // The XML reader refused the file (HelpLocaleReadResult::xml_error).
    xml,
    // The root element is not `<locale>`.
    wrong_root,
    // The root has no `xml:lang` attribute, or an empty one.
    no_language,
    // The file holds more records than HelpFieldLimits::maximum_records.
    too_many_records,
    // `xml:lang` is not a language tag (LocaleFileError::invalid_language).
    invalid_language,
    // `direction` is neither "ltr" nor "rtl".
    invalid_direction,
};

[[nodiscard]] std::string_view help_locale_error_name(HelpLocaleError error) noexcept;

struct HelpLocaleReadResult final {
    HelpLocaleError error{HelpLocaleError::none};
    XmlError xml_error{XmlError::none};
    XmlSourcePosition position{};
    // The `xml:lang` tag, as written.
    std::string language{};
    // The help fields the file gives. Empty on a refused file.
    HelpRecordLayer records{};
    // The fields that were rejected one by one. Empty on a refused file.
    std::vector<HelpEntryRejection> rejections{};

    [[nodiscard]] explicit operator bool() const noexcept { return error == HelpLocaleError::none; }
};

// Reads the help fields of a `locales-<tag>.xml` file:
//
//   <locale xml:lang="fr">
//     <controls>
//       <control name="main">
//         <control name="paste" text="Coller" help-line="...">
//           <help keywords="presse-papiers; coller, image">...</help>
//         </control>
//       </control>
//     </controls>
//   </locale>
//
// `<control>` elements nest by stable-ID segment; `text` is the record's
// title, `help-line` an attribute, and the body the `<help>` child's
// character data (normalize_help_body), with `keywords` on it. This is
// read_locale with the other fields left out (their rejections too).
// Expected faults never throw.
[[nodiscard]] HelpLocaleReadResult read_help_locale(std::string_view utf8, const HelpFieldLimits& limits = {},
                                                    const XmlLimits& xml_limits = locale_xml_limits());

// Writes `records` as a locale file read_help_locale accepts: the template
// export when `records` is the built-in layer (every field with the
// built-in text as its value), or a translation. Records nest by segment
// in stable-ID order. The result's error is the XML writer's.
[[nodiscard]] XmlWriteResult write_help_locale(const HelpRecordLayer& records, std::string_view language,
                                               const XmlLimits& xml_limits = locale_xml_limits());

// Fills `target` (cleared first) with the help fields of `source`: its
// `text` as the title, `help-line`, `help` and `keywords`. Records with
// none of them are left out.
void project_help_fields(const LocaleLayer& source, HelpRecordLayer& target);

// The layers of a HelpCatalog, lowest first.
enum class HelpLayer : std::uint8_t {
    // The program's own text: the embedded built-in locale file, declared
    // topics and values set in code. Always present.
    built_in,
    // The shipped translation for the chosen language, if any.
    translation,
    // The user's overrides (a user `locales-<tag>.xml`), if any.
    user,
};

// One resolved field: the text and the layer it came from. `fallback` is
// set when a translation is in force and the field came from the built-in
// layer, so the page can mark it.
struct HelpResolvedField final {
    std::string text{};
    std::optional<HelpLayer> origin{};
    bool fallback{};
};

struct HelpResolvedRecord final {
    std::string stable_id{};
    HelpResolvedField title{};
    HelpResolvedField help_line{};
    HelpResolvedField help{};
    HelpResolvedField keywords{};
    // Whether any layer holds the record.
    bool found{};
};

// The help text store of one program: the three layers, the rescue switch
// and the language in force. The program owns it (HelpBookSystem holds one).
// UI thread.
class HelpCatalog final {
public:
    [[nodiscard]] const HelpRecordLayer& layer(HelpLayer which) const& noexcept;
    const HelpRecordLayer& layer(HelpLayer which) const&& = delete;
    // The built-in layer, for the program to fill. Bumps the revision.
    [[nodiscard]] HelpRecordLayer& built_in() & noexcept;
    // Replaces the translation layer and the language it is in; an empty
    // layer with an empty language removes it. Bumps the revision.
    void set_translation(HelpRecordLayer records, std::string language);
    // Replaces the user layer. Bumps the revision.
    void set_user_overrides(HelpRecordLayer records);
    // Takes every layer from a language-file catalog (the help view of it):
    // its built-in and program layers become the built-in layer (program
    // values over the file's), its translation and user layers these, and
    // its language and way-back switch these. Replaces what built_in(),
    // set_translation and set_user_overrides put in. Bumps the revision.
    void adopt_locale(const LocaleCatalog& locale);
    // The untranslatable way back (translation design): while set, only the
    // built-in layer resolves, whatever the translation and user files say.
    void set_built_in_only(bool built_in_only) noexcept;
    [[nodiscard]] bool built_in_only() const noexcept { return built_in_only_; }
    [[nodiscard]] const std::string& language() const noexcept { return language_; }

    // Resolves every field of `stable_id` from the highest layer in force
    // that gives it.
    [[nodiscard]] HelpResolvedRecord resolve(std::string_view stable_id) const;
    // Fills `ids` (cleared first) with every ID any layer in force holds,
    // sorted and unique.
    void collect_ids(std::vector<std::string>& ids) const;
    // Grows by one on every change; a compiled section remembers the
    // revision it was built from.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    void resolve_field(std::string_view stable_id, HelpField field, HelpResolvedField& resolved) const;

    HelpRecordLayer built_in_{};
    HelpRecordLayer translation_{};
    HelpRecordLayer user_{};
    std::string language_{};
    std::uint64_t revision_{};
    bool built_in_only_{};
};

} // namespace gui_forms
