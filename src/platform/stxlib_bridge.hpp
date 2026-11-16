#pragma once

// The one private bridge between STXlib's vocabulary and the standard C++
// vocabulary GUI.Forms' code and public headers use (STXlib swap-in A2, T0;
// the D15 migration plan's V0, which calls it stxlib_adapt.hpp: this is
// that header). Every conversion between the two goes through here, so
// none is written inline at a call site, and T2/D15 change one file.
//
// All of it is header-only. A view converted either way borrows exactly
// what its source borrows; nothing here extends a lifetime. Text from
// STXlib is validated UTF-8 already and converts losslessly; text into
// STXlib is validated (validated_text). Paths and durations join when A2's
// G7 (after 1.0) needs them.
//
// The owned forms (D15 V0) convert by named copies, never implicitly:
// - into STXlib (stxlib_text_copy, stxlib_vector_copy) the copy is STXlib's
//   allocation and its failure is a value: an OperationResult holding the refusal
//   (out_of_memory, or invalid_argument for text that is not UTF-8), with
//   nothing allocated;
// - out of STXlib (standard_text_copy, standard_vector_copy) the copy is
//   the standard library's allocation and throws std::bad_alloc as any
//   std::string or std::vector would; the source is unchanged either way.
// Optional converts both ways without allocating (stxlib_optional,
// standard_optional): a present value is copied, an absent one stays
// absent.

#include <stxlib/optional.hpp>
#include <stxlib/result.hpp>
#include <stxlib/text.hpp>
#include <stxlib/text_view.hpp>
#include <stxlib/types.hpp>
#include <stxlib/vector.hpp>
#include <stxlib/view.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
[[nodiscard]] inline stxlib::OperationResult<stxlib::TextView> validated_text(const std::string_view text) noexcept {
    stxlib::OperationResult<stxlib::TextView> result = stxlib::validate_utf8(text.data(), text.size());
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

// Standard bytes (a font's) as STXlib's view of the same bytes as unsigned
// octets. std::byte and unsigned char may alias each other, so the view
// reads exactly the source's storage.
[[nodiscard]] inline stxlib::View<stxlib::u8> stxlib_bytes(const std::span<const std::byte> bytes) noexcept {
    const stxlib::u8* const octets = reinterpret_cast<const stxlib::u8*>(bytes.data());
    const stxlib::View<stxlib::u8> result(octets, static_cast<stxlib::usize>(bytes.size()));
    return result;
}

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

// -- owned text ----------------------------------------------------------------

// A copy of standard text as STXlib's owned text: the copy when text is
// well-formed UTF-8 and the allocation succeeds, else the refusal
// (validated_text's invalid_argument, or out_of_memory), with nothing held.
[[nodiscard]] inline stxlib::OperationResult<stxlib::Utf8String> stxlib_text_copy(const std::string_view text) noexcept {
    const stxlib::OperationResult<stxlib::TextView> validated = validated_text(text);
    if (!validated.succeeded()) {
        stxlib::OperationResult<stxlib::Utf8String> refused = stxlib::OperationResult<stxlib::Utf8String>::failure(*validated.error());
        return refused;
    }
    stxlib::OperationResult<stxlib::Utf8String> result = stxlib::Utf8String::copy_of(*validated.value());
    return result;
}

// A standard copy of STXlib text. Throws std::bad_alloc as std::string does.
[[nodiscard]] inline std::string standard_text_copy(const stxlib::TextView text) {
    std::string result(text.data(), static_cast<std::size_t>(text.count()));
    return result;
}

// -- owned sequences ---------------------------------------------------------

// A copy of standard elements as STXlib's owned vector (capacity exactly
// the count), or out_of_memory with nothing held. Element needs a
// non-throwing copy constructor (Vector::copy_of).
template <typename Element>
[[nodiscard]] stxlib::OperationResult<stxlib::Vector<Element>> stxlib_vector_copy(const std::span<const Element> elements) noexcept {
    const stxlib::View<Element> source = stxlib_view(elements);
    stxlib::OperationResult<stxlib::Vector<Element>> result = stxlib::Vector<Element>::copy_of(source);
    return result;
}

template <typename Element>
[[nodiscard]] stxlib::OperationResult<stxlib::Vector<Element>> stxlib_vector_copy(const std::vector<Element>& elements) noexcept {
    const std::span<const Element> source(elements.data(), elements.size());
    stxlib::OperationResult<stxlib::Vector<Element>> result = stxlib_vector_copy(source);
    return result;
}

// A standard copy of STXlib's elements. Throws std::bad_alloc as
// std::vector does.
template <typename Element>
[[nodiscard]] std::vector<Element> standard_vector_copy(const stxlib::View<Element> elements) {
    const Element* const first = elements.data();
    const std::size_t count = static_cast<std::size_t>(elements.count());
    std::vector<Element> result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        result.push_back(first[index]);
    }
    return result;
}

// -- optional values -----------------------------------------------------------

// The same value or absence, in the other vocabulary. Value needs a
// non-throwing copy constructor (both optionals copy it).
template <typename Value>
[[nodiscard]] stxlib::Optional<Value> stxlib_optional(const std::optional<Value>& source) noexcept {
    if (!source.has_value()) {
        const stxlib::Optional<Value> absent = stxlib::Optional<Value>::none();
        return absent;
    }
    const stxlib::Optional<Value> result = stxlib::Optional<Value>::of(*source);
    return result;
}

template <typename Value>
[[nodiscard]] std::optional<Value> standard_optional(const stxlib::Optional<Value>& source) noexcept {
    const Value* const held = source.value();
    if (held == nullptr) {
        const std::optional<Value> absent;
        return absent;
    }
    const std::optional<Value> result(*held);
    return result;
}

} // namespace gui_forms::platform
