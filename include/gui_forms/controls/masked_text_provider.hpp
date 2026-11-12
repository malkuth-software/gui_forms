#pragma once

#include "gui_forms/text/types/text_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

struct MaskPositionTag;
// A position in a mask: the index of one slot, editable or literal. Every
// slot shows as exactly one Unicode scalar in the display text, so a
// position is also the scalar index of its slot in display_text(). It is not
// a byte offset; display_offset converts.
using MaskPosition = TextUnitIndex<MaskPositionTag>;

// The symbols the mask's culture characters stand for: `.` the decimal
// separator, `,` the group separator, `:` the time separator, `/` the date
// separator and `$` the currency symbol. Each symbol is one or more Unicode
// scalars and each scalar becomes one literal slot. GUI.Forms has no culture
// service, so the default is the invariant culture's symbols (the invariant
// currency symbol is U+00A4, the generic currency sign); a caller that wants
// another culture supplies its symbols.
struct MaskCulture final {
    std::string decimal_separator{"."};
    std::string group_separator{","};
    std::string time_separator{":"};
    std::string date_separator{"/"};
    std::string currency_symbol{"\xC2\xA4"};

    friend bool operator==(const MaskCulture& left, const MaskCulture& right) noexcept {
        const bool result = left.decimal_separator == right.decimal_separator &&
                            left.group_separator == right.group_separator &&
                            left.time_separator == right.time_separator &&
                            left.date_separator == right.date_separator &&
                            left.currency_symbol == right.currency_symbol;
        return result;
    }
};

// Which characters of the text a string conversion keeps (Windows Forms'
// MaskFormat). Without prompts an unassigned position shows as a space and
// the text ends at the last assigned position.
enum class MaskTextFormat : std::uint8_t {
    exclude_prompt_and_literals,
    include_prompt,
    include_literals,
    include_prompt_and_literals,
};

// The outcome of an edit or a check (Windows Forms' MaskedTextResultHint).
// `success` and `character_escaped` are successes: an escaped character was
// a literal the input matched, or a prompt or space that reset its position.
enum class MaskResultHint : std::uint8_t {
    success,
    character_escaped,
    digit_expected,
    signed_digit_expected,
    letter_expected,
    alphanumeric_expected,
    ascii_character_expected,
    invalid_input,
    prompt_character_not_allowed,
    unavailable_edit_position,
    non_edit_position,
    position_out_of_range,
};

[[nodiscard]] std::string_view mask_result_hint_name(MaskResultHint hint) noexcept;

// What an edit did. On success `position` is the last position the edit
// wrote or passed over (the start position when the input was empty); on
// failure it is the position the refused character was tested at, and the
// provider is unchanged.
struct MaskEditResult final {
    MaskResultHint hint{MaskResultHint::success};
    MaskPosition position{};

    [[nodiscard]] bool succeeded() const noexcept {
        const bool result = hint == MaskResultHint::success ||
                            hint == MaskResultHint::character_escaped;
        return result;
    }
};

// A mask and the text assigned to it (Windows Forms' MaskedTextProvider).
// Renderer-free: it holds no control and paints nothing.
//
// Mask language, one slot per mask character unless noted:
//   0  digit, required          9  digit or space, optional
//   #  digit, space, + or -, optional
//   L  ASCII letter, required   ?  ASCII letter, optional
//   &  any character, required  C  any character, optional
//   A  ASCII letter or digit, required   a  the same, optional
//   .  ,  :  /  $   the culture's symbols, as literal slots (MaskCulture)
//   <  >  |   no slot: lower-case, upper-case or stop converting the letters
//             typed into the slots that follow
//   \x  the literal x
//   any other character: itself, as a literal slot
// "Any character" means any Unicode scalar that is not a control character.
// Letters and alphanumerics are ASCII only: GUI.Forms has no Unicode
// character-category data, and Windows Forms documents L as [a-zA-Z]. Case
// conversion likewise applies to ASCII letters only.
//
// Every edit validates the whole input before it changes anything: a failed
// edit leaves the provider as it was and reports the first refused character.
class MaskedTextProvider final {
public:
    // Throws std::invalid_argument for a mask or culture symbol that is not
    // valid UTF-8, a mask ending in a lone backslash, a control character in
    // the mask, or an empty culture symbol. An empty mask has no positions.
    explicit MaskedTextProvider(std::string mask = {}, MaskCulture culture = {});

    [[nodiscard]] std::string_view mask() const& noexcept { return mask_; }
    std::string_view mask() const&& = delete;
    [[nodiscard]] const MaskCulture& culture() const noexcept { return culture_; }
    // The number of slots, literal and editable.
    [[nodiscard]] std::size_t length() const noexcept { return slots_.size(); }
    [[nodiscard]] std::size_t edit_position_count() const noexcept;
    [[nodiscard]] std::size_t assigned_edit_position_count() const noexcept;
    [[nodiscard]] std::size_t available_edit_position_count() const noexcept;
    // Every required position is assigned (Windows Forms' MaskCompleted).
    [[nodiscard]] bool mask_completed() const noexcept;
    // Every edit position, required or optional, is assigned (MaskFull).
    [[nodiscard]] bool mask_full() const noexcept;
    // False for a literal and for a position past the end.
    [[nodiscard]] bool is_edit_position(MaskPosition position) const noexcept;
    [[nodiscard]] bool is_assigned(MaskPosition position) const noexcept;
    // The first edit position at or after `from` (forward), or at or before
    // it (backward); none when there is no such position.
    [[nodiscard]] std::optional<MaskPosition> find_edit_position(MaskPosition from,
                                                                 bool forward) const noexcept;
    [[nodiscard]] std::optional<MaskPosition> last_assigned_position() const noexcept;

    // The character an unassigned position shows: any non-control scalar.
    // Throws std::invalid_argument otherwise.
    [[nodiscard]] char32_t prompt_character() const noexcept { return prompt_character_; }
    void set_prompt_character(char32_t character);
    // Whether the prompt character may be typed; with reset_on_prompt it
    // then clears its position.
    [[nodiscard]] bool allow_prompt_as_input() const noexcept { return allow_prompt_as_input_; }
    void set_allow_prompt_as_input(bool allow) noexcept { allow_prompt_as_input_ = allow; }
    [[nodiscard]] bool reset_on_prompt() const noexcept { return reset_on_prompt_; }
    void set_reset_on_prompt(bool reset) noexcept { reset_on_prompt_ = reset; }
    // Whether a typed space clears its position instead of being tested.
    [[nodiscard]] bool reset_on_space() const noexcept { return reset_on_space_; }
    void set_reset_on_space(bool reset) noexcept { reset_on_space_ = reset; }
    // Whether input past the ASCII range is refused at every position.
    [[nodiscard]] bool ascii_only() const noexcept { return ascii_only_; }
    void set_ascii_only(bool ascii_only) noexcept { ascii_only_ = ascii_only; }
    // Whether an input character equal to the literal at its position is
    // taken as that literal and passed over. Otherwise input at a literal
    // goes to the next edit position.
    [[nodiscard]] bool skip_literals() const noexcept { return skip_literals_; }
    void set_skip_literals(bool skip) noexcept { skip_literals_ = skip; }

    // Whether `character` could be assigned at `position`. Changes nothing.
    [[nodiscard]] MaskEditResult verify_character(char32_t character, MaskPosition position) const;

    // Inserts `text` at `position`, moving the characters at and after it
    // to later edit positions; fails when they do not fit or no longer
    // match their new positions.
    MaskEditResult insert_at(std::string_view text, MaskPosition position);
    // Overwrites from `position` on (overwrite mode).
    MaskEditResult replace_at(std::string_view text, MaskPosition position);
    // Removes the characters in [start, end), then inserts `text` at start.
    MaskEditResult replace_range(std::string_view text, MaskPosition start, MaskPosition end);
    // Removes the assigned characters in [start, end) and moves the
    // characters after them back into the freed positions.
    MaskEditResult remove_range(MaskPosition start, MaskPosition end);
    // Clears everything, then places `text` from the first position.
    MaskEditResult set_text(std::string_view text);
    // Unassigns every position.
    void clear() noexcept;

    [[nodiscard]] std::string to_string(MaskTextFormat format) const;
    // Every slot: assigned characters, prompts and literals. One scalar per
    // position.
    [[nodiscard]] std::string display_text() const;
    // The byte offset in display_text() where `position`'s slot starts; a
    // position at length() gives the text's size.
    [[nodiscard]] Utf8Offset display_offset(MaskPosition position) const;
    // The position whose slot starts at `offset` in display_text(), or the
    // one the offset falls inside.
    [[nodiscard]] MaskPosition display_position(Utf8Offset offset) const;

private:
    enum class SlotKind : std::uint8_t {
        literal,
        digit_required,
        digit_or_space,
        digit_or_sign,
        letter_required,
        letter_optional,
        character_required,
        character_optional,
        alphanumeric_required,
        alphanumeric_optional,
    };
    enum class CaseConversion : std::uint8_t { none, lower, upper };
    // One slot of the parsed mask; fixed for the provider's life.
    struct MaskSlot final {
        SlotKind kind{SlotKind::literal};
        CaseConversion conversion{CaseConversion::none};
        char32_t literal{};
    };
    // What one edit position holds.
    struct MaskSlotValue final {
        bool assigned{};
        char32_t character{};
    };
    // How a placement treats the position it writes: move what is there on
    // (insert) or write over it (overwrite).
    enum class Placement : std::uint8_t { insert, overwrite };

    [[nodiscard]] static bool required(SlotKind kind) noexcept;
    [[nodiscard]] MaskResultHint test(char32_t character, std::size_t position, MaskSlotValue& value) const noexcept;
    [[nodiscard]] MaskResultHint test_moved(MaskSlotValue value, std::size_t position) const noexcept;
    [[nodiscard]] MaskEditResult place(std::vector<MaskSlotValue>& values, std::string_view text,
                                       std::size_t position, Placement placement) const;
    [[nodiscard]] MaskEditResult shift_right(std::vector<MaskSlotValue>& values, std::size_t position) const;
    [[nodiscard]] MaskEditResult remove(std::vector<MaskSlotValue>& values, std::size_t start,
                                        std::size_t end) const;
    [[nodiscard]] char32_t shown(std::size_t position) const noexcept;

    std::string mask_;
    MaskCulture culture_;
    std::vector<MaskSlot> slots_;
    // One per slot; a literal's entry is never assigned.
    std::vector<MaskSlotValue> values_;
    char32_t prompt_character_{U'_'};
    bool allow_prompt_as_input_{true};
    bool reset_on_prompt_{true};
    bool reset_on_space_{true};
    bool ascii_only_{};
    bool skip_literals_{true};
};

} // namespace gui_forms
