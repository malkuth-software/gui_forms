#include "winforms_mapping.hpp"

#include "designer_reader.hpp"
#include "winforms_member_table.hpp"

#include <algorithm>

namespace gui_forms::winforms_converter {

namespace {

struct TypeRowBelow final {
    [[nodiscard]] bool operator()(const WinFormsTypeRow& row, std::string_view name) const noexcept {
        const bool below = row.winforms < name;
        return below;
    }
};

struct MemberRowBelow final {
    [[nodiscard]] bool operator()(const WinFormsMemberRow& row, std::string_view name) const noexcept {
        const bool below = row.winforms < name;
        return below;
    }
};

struct DivergenceRowBelow final {
    std::string_view control{};
    [[nodiscard]] bool operator()(const WinFormsDivergenceRow& row, std::string_view member) const noexcept {
        if (row.control != control) return row.control < control;
        const bool below = row.member < member;
        return below;
    }
};

// The member of `kind` named `member` in one type's own rows.
[[nodiscard]] const WinFormsMemberRow* own_member(const WinFormsTypeRow& type, std::string_view member,
                                                  WinFormsMemberKind kind) noexcept {
    const WinFormsMemberRow* const first = winforms_member_rows.data() + type.first_member;
    const WinFormsMemberRow* const last = first + type.member_count;
    const WinFormsMemberRow* found = std::lower_bound(first, last, member, MemberRowBelow{});
    // Rows of one name follow each other, one per kind.
    while (found != last && (*found).winforms == member) {
        if ((*found).kind == kind) return found;
        found = found + 1;
    }
    return nullptr;
}

[[nodiscard]] bool upper(char byte) noexcept {
    const bool capital = byte >= 'A' && byte <= 'Z';
    return capital;
}

[[nodiscard]] bool lower(char byte) noexcept {
    const bool small = byte >= 'a' && byte <= 'z';
    return small;
}

[[nodiscard]] bool numeral(char byte) noexcept {
    const bool decimal = byte >= '0' && byte <= '9';
    return decimal;
}

[[nodiscard]] char lowered(char byte) noexcept {
    const char small = upper(byte) ? static_cast<char>(byte - 'A' + 'a') : byte;
    return small;
}

// Words of a Pascal or camel name joined by `separator`: a capital starts
// a word after a lowercase letter or digit, and the last capital of a run
// starts a word before a lowercase letter (`HScrollBar`: h scroll bar,
// `btnOK`: btn ok). '_' and other non-alphanumerics separate words.
[[nodiscard]] std::string words_of(std::string_view name, char separator) {
    std::string out{};
    out.reserve(name.size() + 8U);
    for (std::size_t index = 0U; index < name.size(); ++index) {
        const char byte = name[index];
        const bool alphanumeric = upper(byte) || lower(byte) || numeral(byte);
        if (!alphanumeric) {
            if (!out.empty() && out.back() != separator) out.push_back(separator);
            continue;
        }
        if (upper(byte) && !out.empty() && out.back() != separator) {
            const char previous = name[index - 1U];
            const bool after_word = lower(previous) || numeral(previous);
            const bool next_lower = index + 1U < name.size() && lower(name[index + 1U]);
            const bool run_end = upper(previous) && next_lower;
            if (after_word || run_end) out.push_back(separator);
        }
        out.push_back(lowered(byte));
    }
    while (!out.empty() && out.back() == separator) out.pop_back();
    return out;
}

} // namespace

const WinFormsTypeRow* find_winforms_type(std::string_view name) noexcept {
    const std::string_view short_name = last_segment(name);
    const WinFormsTypeRow* const first = winforms_type_rows.data();
    const WinFormsTypeRow* const last = first + winforms_type_rows.size();
    const WinFormsTypeRow* const found = std::lower_bound(first, last, short_name, TypeRowBelow{});
    if (found == last || (*found).winforms != short_name) return nullptr;
    return found;
}

WinFormsMemberMatch find_winforms_member(const WinFormsTypeRow& type, std::string_view member,
                                         WinFormsMemberKind kind) noexcept {
    WinFormsMemberMatch match{};
    const WinFormsTypeRow* current = &type;
    for (std::size_t depth = 0U; current != nullptr && depth < maximum_winforms_base_depth; ++depth) {
        const WinFormsMemberRow* const row = own_member(*current, member, kind);
        if (row != nullptr) {
            match.member = row;
            match.declared_by = current;
            return match;
        }
        if ((*current).winforms_base.empty()) break;
        current = find_winforms_type((*current).winforms_base);
    }
    return match;
}

const WinFormsDivergenceRow* find_winforms_divergence(std::string_view gui_forms_type,
                                                      std::string_view member) noexcept {
    const std::string_view controls[] = {gui_forms_type, "Control"};
    for (const std::string_view control : controls) {
        const WinFormsDivergenceRow* const first = winforms_divergence_rows.data();
        const WinFormsDivergenceRow* const last = first + winforms_divergence_rows.size();
        const WinFormsDivergenceRow* const found = std::lower_bound(first, last, member, DivergenceRowBelow{control});
        if (found != last && (*found).control == control && (*found).member == member) return found;
    }
    return nullptr;
}

std::string snake_case_of(std::string_view pascal) {
    std::string snake = words_of(pascal, '_');
    return snake;
}

std::string kebab_case_of(std::string_view pascal) {
    std::string kebab = words_of(pascal, '-');
    return kebab;
}

std::string stable_id_segment_of(std::string_view field) {
    // Non-ASCII bytes and other punctuation separate words like '_'.
    std::string segment = words_of(field, '-');
    while (!segment.empty() && segment.front() == '-') segment.erase(segment.begin());
    return segment;
}

std::string member_identifier_of(std::string_view name) {
    std::string identifier = words_of(name, '_');
    if (!identifier.empty() && numeral(identifier.front())) identifier.insert(0U, "source_");
    return identifier;
}

} // namespace gui_forms::winforms_converter
