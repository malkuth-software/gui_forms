#pragma once

// The one private bridge between STXlib's vocabulary and the standard C++
// vocabulary GUI.Forms' code and public headers use (STXlib swap-in A2, T0;
// the D15 migration plan's V0, which calls it stxlib_adapt.hpp: this is
// that header). Every conversion between the two goes through here, so
// none is written inline at a call site, and T2/D15 change one file.
//
// All of it is header-only and allocates nothing. A view converted either
// way borrows exactly what its source borrows; nothing here extends a
// lifetime. Text from STXlib is validated UTF-8 already and converts
// losslessly; text into STXlib is validated (validated_text). Paths and
// durations join when A2's G7 (after 1.0) needs them.

#include <stxlib/result.hpp>
#include <stxlib/text.hpp>
#include <stxlib/text_view.hpp>
#include <stxlib/types.hpp>
#include <stxlib/view.hpp>

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace gui_forms::platform {

// -- text ----------------------------------------------------------------------

// STXlib text as a standard view of the same bytes.
[[nodiscard]] inline std::string_view standard_text(const stxlib::TextView text) noexcept {
    const std::string_view result(text.data(), static_cast<std::size_t>(text.count()));
    return result;
}

// Standard text as STXlib text, borrowing text: the whole of it when it is
// well-formed UTF-8, else STXlib's invalid_argument record, whose code is
// the offset of the first ill-formed byte (u32_max when the offset does not
// fit), as stxlib::validate_utf8 reports.
[[nodiscard]] inline stxlib::Result<stxlib::TextView> validated_text(const std::string_view text) noexcept {
    stxlib::Result<stxlib::TextView> result = stxlib::validate_utf8(text.data(), text.size());
    return result;
}

// -- views -------------------------------------------------------------------

// A standard span as STXlib's view of the same elements.
template <typename Element>
[[nodiscard]] stxlib::View<Element> stxlib_view(const std::span<const Element> elements) noexcept {
    const stxlib::View<Element> result(elements.data(), static_cast<stxlib::usize>(elements.size()));
    return result;
}

template <typename Element>
[[nodiscard]] stxlib::MutableView<Element> stxlib_mutable_view(const std::span<Element> elements) noexcept {
    const stxlib::MutableView<Element> result(elements.data(), static_cast<stxlib::usize>(elements.size()));
    return result;
}

// Fixed storage the caller owns, for STXlib to fill (a frame record array,
// an exception type's text). Borrows storage, so an rvalue is refused.
template <typename Element, std::size_t Count>
[[nodiscard]] stxlib::MutableView<Element> stxlib_mutable_view(std::array<Element, Count>& storage) noexcept {
    const stxlib::MutableView<Element> result(storage.data(), static_cast<stxlib::usize>(Count));
    return result;
}

template <typename Element, std::size_t Count>
stxlib::MutableView<Element> stxlib_mutable_view(std::array<Element, Count>&& storage) = delete;

// STXlib's views as standard spans of the same elements.
template <typename Element>
[[nodiscard]] std::span<const Element> standard_span(const stxlib::View<Element> elements) noexcept {
    const std::span<const Element> result(elements.data(), static_cast<std::size_t>(elements.count()));
    return result;
}

template <typename Element>
[[nodiscard]] std::span<Element> standard_span(const stxlib::MutableView<Element> elements) noexcept {
    const std::span<Element> result(elements.data(), static_cast<std::size_t>(elements.count()));
    return result;
}

} // namespace gui_forms::platform
