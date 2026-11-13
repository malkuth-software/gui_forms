#include "gui_forms/controls/masked_text_provider.hpp"

#include "../../input_control_utilities.hpp"

#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

// Decodes `text`, which validate_utf8 has accepted, into its scalars.
void decode_scalars(std::string_view text, std::vector<char32_t>& scalars) {
    scalars.clear();
    std::size_t index = 0U;
    while (index < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        std::size_t length = 1U;
        char32_t value = lead;
        if (lead >= 0xF0U) {
            length = 4U;
            value = lead & 0x07U;
        } else if (lead >= 0xE0U) {
            length = 3U;
            value = lead & 0x0FU;
        } else if (lead >= 0xC0U) {
            length = 2U;
            value = lead & 0x1FU;
        }
        for (std::size_t next = 1U; next < length; ++next) {
            const unsigned char continuation = static_cast<unsigned char>(text[index + next]);
            value = (value << 6U) | (continuation & 0x3FU);
        }
        scalars.push_back(value);
        index += length;
    }
}

[[nodiscard]] std::size_t utf8_length(char32_t value) noexcept {
    if (value <= U'\u007f') return 1U;
    if (value <= U'\u07ff') return 2U;
    if (value <= U'\uffff') return 3U;
    return 4U;
}

// The C0 and C1 controls, DEL, and the line and paragraph separators
// (U+2028, U+2029): characters a one-line field cannot show.
[[nodiscard]] bool control_character(char32_t value) noexcept {
    const bool result = value < U' ' || (value >= U'\u007f' && value <= U'\u009f') ||
                        value == U'\u2028' || value == U'\u2029';
    return result;
}

[[nodiscard]] bool ascii_digit(char32_t value) noexcept {
    const bool result = value >= U'0' && value <= U'9';
    return result;
}

[[nodiscard]] bool ascii_letter(char32_t value) noexcept {
    const bool result = (value >= U'a' && value <= U'z') || (value >= U'A' && value <= U'Z');
    return result;
}

void require_symbol(std::string_view symbol) {
    if (symbol.empty() || !validate_utf8(symbol).valid()) {
        throw std::invalid_argument("MaskCulture symbols must be non-empty valid UTF-8");
    }
}

} // namespace

std::string_view mask_result_hint_name(MaskResultHint hint) noexcept {
    switch (hint) {
    case MaskResultHint::success: return "success";
    case MaskResultHint::character_escaped: return "character_escaped";
    case MaskResultHint::digit_expected: return "digit_expected";
    case MaskResultHint::signed_digit_expected: return "signed_digit_expected";
    case MaskResultHint::letter_expected: return "letter_expected";
    case MaskResultHint::alphanumeric_expected: return "alphanumeric_expected";
    case MaskResultHint::ascii_character_expected: return "ascii_character_expected";
    case MaskResultHint::invalid_input: return "invalid_input";
    case MaskResultHint::prompt_character_not_allowed: return "prompt_character_not_allowed";
    case MaskResultHint::unavailable_edit_position: return "unavailable_edit_position";
    case MaskResultHint::non_edit_position: return "non_edit_position";
    case MaskResultHint::position_out_of_range: return "position_out_of_range";
    }
    return "unknown";
}

// -- parsing -----------------------------------------------------------------

MaskedTextProvider::MaskedTextProvider(std::string mask, MaskCulture culture)
    : mask_(std::move(mask)), culture_(std::move(culture)) {
    if (!validate_utf8(mask_).valid()) {
        throw std::invalid_argument("MaskedTextProvider mask must be valid UTF-8");
    }
    require_symbol(culture_.decimal_separator);
    require_symbol(culture_.group_separator);
    require_symbol(culture_.time_separator);
    require_symbol(culture_.date_separator);
    require_symbol(culture_.currency_symbol);
    std::vector<char32_t> characters;
    decode_scalars(mask_, characters);
    std::vector<char32_t> symbol;
    CaseConversion conversion = CaseConversion::none;
    for (std::size_t index = 0U; index < characters.size(); ++index) {
        const char32_t character = characters[index];
        if (control_character(character)) {
            throw std::invalid_argument("MaskedTextProvider mask may not contain control characters");
        }
        MaskSlot slot;
        slot.conversion = conversion;
        std::string_view culture_symbol;
        switch (character) {
        case U'<': conversion = CaseConversion::lower; continue;
        case U'>': conversion = CaseConversion::upper; continue;
        case U'|': conversion = CaseConversion::none; continue;
        case U'\\':
            if (index + 1U == characters.size()) {
                throw std::invalid_argument("MaskedTextProvider mask ends in a lone backslash");
            }
            ++index;
            if (control_character(characters[index])) {
                throw std::invalid_argument("MaskedTextProvider mask may not contain control characters");
            }
            slot.literal = characters[index];
            slots_.push_back(slot);
            continue;
        case U'0': slot.kind = SlotKind::digit_required; break;
        case U'9': slot.kind = SlotKind::digit_or_space; break;
        case U'#': slot.kind = SlotKind::digit_or_sign; break;
        case U'L': slot.kind = SlotKind::letter_required; break;
        case U'?': slot.kind = SlotKind::letter_optional; break;
        case U'&': slot.kind = SlotKind::character_required; break;
        case U'C': slot.kind = SlotKind::character_optional; break;
        case U'A': slot.kind = SlotKind::alphanumeric_required; break;
        case U'a': slot.kind = SlotKind::alphanumeric_optional; break;
        case U'.': culture_symbol = culture_.decimal_separator; break;
        case U',': culture_symbol = culture_.group_separator; break;
        case U':': culture_symbol = culture_.time_separator; break;
        case U'/': culture_symbol = culture_.date_separator; break;
        case U'$': culture_symbol = culture_.currency_symbol; break;
        default:
            slot.literal = character;
            break;
        }
        if (!culture_symbol.empty()) {
            // One literal slot per scalar of the culture's symbol.
            decode_scalars(culture_symbol, symbol);
            for (std::size_t part = 0U; part < symbol.size(); ++part) {
                if (control_character(symbol[part])) {
                    throw std::invalid_argument("MaskCulture symbols may not contain control characters");
                }
                MaskSlot literal;
                literal.literal = symbol[part];
                slots_.push_back(literal);
            }
            continue;
        }
        slots_.push_back(slot);
    }
    values_.assign(slots_.size(), MaskSlotValue{});
}

// -- queries -----------------------------------------------------------------

bool MaskedTextProvider::required(SlotKind kind) noexcept {
    const bool result = kind == SlotKind::digit_required || kind == SlotKind::letter_required ||
                        kind == SlotKind::character_required ||
                        kind == SlotKind::alphanumeric_required;
    return result;
}

std::size_t MaskedTextProvider::edit_position_count() const noexcept {
    std::size_t count = 0U;
    for (const MaskSlot& slot : slots_) {
        if (slot.kind != SlotKind::literal) ++count;
    }
    return count;
}

std::size_t MaskedTextProvider::assigned_edit_position_count() const noexcept {
    std::size_t count = 0U;
    for (const MaskSlotValue& value : values_) {
        if (value.assigned) ++count;
    }
    return count;
}

std::size_t MaskedTextProvider::available_edit_position_count() const noexcept {
    const std::size_t result = edit_position_count() - assigned_edit_position_count();
    return result;
}

bool MaskedTextProvider::mask_completed() const noexcept {
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        if (required(slots_[index].kind) && !values_[index].assigned) return false;
    }
    return true;
}

bool MaskedTextProvider::mask_full() const noexcept {
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        if (slots_[index].kind != SlotKind::literal && !values_[index].assigned) return false;
    }
    return true;
}

bool MaskedTextProvider::is_edit_position(MaskPosition position) const noexcept {
    const bool result = position.value() < slots_.size() &&
                        slots_[position.value()].kind != SlotKind::literal;
    return result;
}

bool MaskedTextProvider::is_assigned(MaskPosition position) const noexcept {
    const bool result = position.value() < values_.size() && values_[position.value()].assigned;
    return result;
}

std::optional<MaskPosition> MaskedTextProvider::find_edit_position(MaskPosition from,
                                                                   bool forward) const noexcept {
    if (slots_.empty()) return std::nullopt;
    if (forward) {
        for (std::size_t index = from.value(); index < slots_.size(); ++index) {
            if (slots_[index].kind != SlotKind::literal) return MaskPosition(index);
        }
        return std::nullopt;
    }
    const std::size_t last = slots_.size() - 1U;
    std::size_t index = from.value() < last ? from.value() : last;
    for (std::size_t step = index + 1U; step > 0U; --step) {
        index = step - 1U;
        if (slots_[index].kind != SlotKind::literal) return MaskPosition(index);
    }
    return std::nullopt;
}

std::optional<MaskPosition> MaskedTextProvider::last_assigned_position() const noexcept {
    for (std::size_t step = values_.size(); step > 0U; --step) {
        if (values_[step - 1U].assigned) return MaskPosition(step - 1U);
    }
    return std::nullopt;
}

void MaskedTextProvider::set_prompt_character(char32_t character) {
    if (control_character(character) || character > U'\U0010ffff' ||
        (character >= static_cast<char32_t>(0xD800U) && character <= static_cast<char32_t>(0xDFFFU))) {
        throw std::invalid_argument("MaskedTextProvider prompt must be one non-control Unicode scalar");
    }
    prompt_character_ = character;
}

// -- character tests ---------------------------------------------------------

// Tests one typed character at an edit position and writes the value it
// leaves there: assigned (case-converted) or, for a reset, unassigned.
MaskResultHint MaskedTextProvider::test(char32_t character, std::size_t position,
                                        MaskSlotValue& value) const noexcept {
    const MaskSlot& slot = slots_[position];
    if (slot.kind == SlotKind::literal) return MaskResultHint::non_edit_position;
    if (character == prompt_character_) {
        if (!allow_prompt_as_input_) return MaskResultHint::prompt_character_not_allowed;
        if (reset_on_prompt_) {
            value = MaskSlotValue{};
            return MaskResultHint::character_escaped;
        }
    }
    if (character == U' ' && reset_on_space_) {
        value = MaskSlotValue{};
        return MaskResultHint::character_escaped;
    }
    MaskSlotValue candidate{.assigned = true, .character = character};
    // ASCII case conversion: the two cases are 0x20 apart.
    if (slot.conversion == CaseConversion::lower && character >= U'A' && character <= U'Z') {
        candidate.character = static_cast<char32_t>(character + 0x20U);
    } else if (slot.conversion == CaseConversion::upper && character >= U'a' && character <= U'z') {
        candidate.character = static_cast<char32_t>(character - 0x20U);
    }
    const MaskResultHint hint = test_moved(candidate, position);
    if (hint == MaskResultHint::success) value = candidate;
    return hint;
}

// Tests an assigned value at an edit position, without prompt or space
// resets: used for typed characters after those and for characters an edit
// moves to another position.
MaskResultHint MaskedTextProvider::test_moved(MaskSlotValue value, std::size_t position) const noexcept {
    if (!value.assigned) return MaskResultHint::success;
    const char32_t character = value.character;
    if (control_character(character)) return MaskResultHint::invalid_input;
    if (ascii_only_ && character > U'\u007f') return MaskResultHint::ascii_character_expected;
    switch (slots_[position].kind) {
    case SlotKind::literal:
        return MaskResultHint::non_edit_position;
    case SlotKind::digit_required:
        return ascii_digit(character) ? MaskResultHint::success : MaskResultHint::digit_expected;
    case SlotKind::digit_or_space:
        return ascii_digit(character) || character == U' ' ? MaskResultHint::success
                                                           : MaskResultHint::digit_expected;
    case SlotKind::digit_or_sign: {
        const bool accepted = ascii_digit(character) || character == U' ' || character == U'+' ||
                              character == U'-';
        return accepted ? MaskResultHint::success : MaskResultHint::signed_digit_expected;
    }
    case SlotKind::letter_required:
    case SlotKind::letter_optional:
        return ascii_letter(character) ? MaskResultHint::success : MaskResultHint::letter_expected;
    case SlotKind::character_required:
    case SlotKind::character_optional:
        return MaskResultHint::success;
    case SlotKind::alphanumeric_required:
    case SlotKind::alphanumeric_optional: {
        const bool accepted = ascii_letter(character) || ascii_digit(character);
        return accepted ? MaskResultHint::success : MaskResultHint::alphanumeric_expected;
    }
    }
    return MaskResultHint::invalid_input;
}

MaskEditResult MaskedTextProvider::verify_character(char32_t character, MaskPosition position) const {
    if (position.value() >= slots_.size()) {
        return MaskEditResult{MaskResultHint::position_out_of_range, position};
    }
    MaskSlotValue ignored;
    const MaskResultHint hint = test(character, position.value(), ignored);
    return MaskEditResult{hint, position};
}

// -- edits on a working copy -------------------------------------------------

// Moves the values at the edit positions from `position` on one edit
// position later. The last edit position must be free, and every moved
// character must suit its new position.
MaskEditResult MaskedTextProvider::shift_right(std::vector<MaskSlotValue>& values, std::size_t position) const {
    std::vector<std::size_t> edits;
    for (std::size_t index = position; index < slots_.size(); ++index) {
        if (slots_[index].kind != SlotKind::literal) edits.push_back(index);
    }
    if (edits.empty() || values[edits.back()].assigned) {
        return MaskEditResult{MaskResultHint::unavailable_edit_position, MaskPosition(position)};
    }
    for (std::size_t step = edits.size() - 1U; step > 0U; --step) {
        const MaskSlotValue moved = values[edits[step - 1U]];
        const MaskResultHint hint = test_moved(moved, edits[step]);
        if (hint != MaskResultHint::success) {
            return MaskEditResult{hint, MaskPosition(edits[step])};
        }
        values[edits[step]] = moved;
    }
    values[edits.front()] = MaskSlotValue{};
    return MaskEditResult{MaskResultHint::success, MaskPosition(position)};
}

// Writes `text` from `position` on. A character equal to the literal it
// meets is passed over (skip_literals); any other character at a literal
// goes to the next edit position.
MaskEditResult MaskedTextProvider::place(std::vector<MaskSlotValue>& values, std::string_view text,
                                         std::size_t position, Placement placement) const {
    if (position > slots_.size()) {
        return MaskEditResult{MaskResultHint::position_out_of_range, MaskPosition(position)};
    }
    if (!validate_utf8(text).valid()) {
        return MaskEditResult{MaskResultHint::invalid_input, MaskPosition(position)};
    }
    std::vector<char32_t> characters;
    decode_scalars(text, characters);
    std::size_t next = position;
    MaskEditResult result{MaskResultHint::success, MaskPosition(position)};
    for (const char32_t character : characters) {
        if (next < slots_.size() && slots_[next].kind == SlotKind::literal) {
            if (skip_literals_ && character == slots_[next].literal) {
                result = MaskEditResult{MaskResultHint::character_escaped, MaskPosition(next)};
                ++next;
                continue;
            }
            const std::optional<MaskPosition> edit = find_edit_position(MaskPosition(next), true);
            next = edit ? (*edit).value() : slots_.size();
        }
        if (next >= slots_.size()) {
            return MaskEditResult{MaskResultHint::unavailable_edit_position, MaskPosition(next)};
        }
        MaskSlotValue value;
        const MaskResultHint hint = test(character, next, value);
        if (hint != MaskResultHint::success && hint != MaskResultHint::character_escaped) {
            return MaskEditResult{hint, MaskPosition(next)};
        }
        if (placement == Placement::insert) {
            const MaskEditResult shifted = shift_right(values, next);
            if (!shifted.succeeded()) return shifted;
        }
        values[next] = value;
        result = MaskEditResult{hint, MaskPosition(next)};
        ++next;
    }
    return result;
}

// Removes the values at the edit positions in [start, end) and moves the
// values after them back into the freed positions.
MaskEditResult MaskedTextProvider::remove(std::vector<MaskSlotValue>& values, std::size_t start,
                                          std::size_t end) const {
    if (start > end || end > slots_.size()) {
        return MaskEditResult{MaskResultHint::position_out_of_range, MaskPosition(start)};
    }
    std::vector<std::size_t> edits;
    std::vector<MaskSlotValue> kept;
    for (std::size_t index = start; index < slots_.size(); ++index) {
        if (slots_[index].kind == SlotKind::literal) continue;
        edits.push_back(index);
        if (index >= end) kept.push_back(values[index]);
    }
    for (std::size_t step = 0U; step < edits.size(); ++step) {
        MaskSlotValue value;
        if (step < kept.size()) value = kept[step];
        const MaskResultHint hint = test_moved(value, edits[step]);
        if (hint != MaskResultHint::success) {
            return MaskEditResult{hint, MaskPosition(edits[step])};
        }
        values[edits[step]] = value;
    }
    return MaskEditResult{MaskResultHint::success, MaskPosition(start)};
}

// -- public edits --------------------------------------------------------------

MaskEditResult MaskedTextProvider::insert_at(std::string_view text, MaskPosition position) {
    std::vector<MaskSlotValue> working = values_;
    const MaskEditResult result = place(working, text, position.value(), Placement::insert);
    if (result.succeeded()) values_ = std::move(working);
    return result;
}

MaskEditResult MaskedTextProvider::replace_at(std::string_view text, MaskPosition position) {
    std::vector<MaskSlotValue> working = values_;
    const MaskEditResult result = place(working, text, position.value(), Placement::overwrite);
    if (result.succeeded()) values_ = std::move(working);
    return result;
}

MaskEditResult MaskedTextProvider::replace_range(std::string_view text, MaskPosition start,
                                                 MaskPosition end) {
    std::vector<MaskSlotValue> working = values_;
    const MaskEditResult removed = remove(working, start.value(), end.value());
    if (!removed.succeeded()) return removed;
    const MaskEditResult result = place(working, text, start.value(), Placement::insert);
    if (result.succeeded()) values_ = std::move(working);
    return result;
}

MaskEditResult MaskedTextProvider::remove_range(MaskPosition start, MaskPosition end) {
    std::vector<MaskSlotValue> working = values_;
    const MaskEditResult result = remove(working, start.value(), end.value());
    if (result.succeeded()) values_ = std::move(working);
    return result;
}

MaskEditResult MaskedTextProvider::set_text(std::string_view text) {
    std::vector<MaskSlotValue> working(slots_.size(), MaskSlotValue{});
    const MaskEditResult result = place(working, text, 0U, Placement::overwrite);
    if (result.succeeded()) values_ = std::move(working);
    return result;
}

void MaskedTextProvider::clear() noexcept {
    for (MaskSlotValue& value : values_) {
        value = MaskSlotValue{};
    }
}

// -- text ----------------------------------------------------------------------

char32_t MaskedTextProvider::shown(std::size_t position) const noexcept {
    const MaskSlot& slot = slots_[position];
    if (slot.kind == SlotKind::literal) return slot.literal;
    if (values_[position].assigned) return values_[position].character;
    return prompt_character_;
}

std::string MaskedTextProvider::to_string(MaskTextFormat format) const {
    const bool prompts = format == MaskTextFormat::include_prompt ||
                         format == MaskTextFormat::include_prompt_and_literals;
    const bool literals = format == MaskTextFormat::include_literals ||
                          format == MaskTextFormat::include_prompt_and_literals;
    std::size_t end = slots_.size();
    if (!prompts) {
        const std::optional<MaskPosition> last = last_assigned_position();
        end = last ? (*last).value() + 1U : 0U;
    }
    std::string text;
    for (std::size_t index = 0U; index < end; ++index) {
        const MaskSlot& slot = slots_[index];
        if (slot.kind == SlotKind::literal) {
            if (literals) text += input_control_detail::utf8_scalar(slot.literal);
        } else if (values_[index].assigned) {
            text += input_control_detail::utf8_scalar(values_[index].character);
        } else if (prompts) {
            text += input_control_detail::utf8_scalar(prompt_character_);
        } else {
            text.push_back(' ');
        }
    }
    return text;
}

std::string MaskedTextProvider::display_text() const {
    std::string text;
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        text += input_control_detail::utf8_scalar(shown(index));
    }
    return text;
}

Utf8Offset MaskedTextProvider::display_offset(MaskPosition position) const {
    const std::size_t end = position.value() < slots_.size() ? position.value() : slots_.size();
    std::size_t offset = 0U;
    for (std::size_t index = 0U; index < end; ++index) {
        offset += utf8_length(shown(index));
    }
    const Utf8Offset result(offset);
    return result;
}

MaskPosition MaskedTextProvider::display_position(Utf8Offset offset) const {
    std::size_t start = 0U;
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        const std::size_t next = start + utf8_length(shown(index));
        if (offset.value() < next) return MaskPosition(index);
        start = next;
    }
    const MaskPosition end(slots_.size());
    return end;
}

} // namespace gui_forms
