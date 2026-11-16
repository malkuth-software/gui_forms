// Kept in the surface, since CoreText and CoreFoundation types stay in
// src/surface/macos/ (tools/check_platform_headers.py); the declaration is the
// renderer's (src/render/skia/executor/core_text_font_bytes.hpp).
#include "../../../render/skia/executor/core_text_font_bytes.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreText/CoreText.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace gui_drawing::render {
namespace {

// Releases an object obtained under CoreFoundation's Create/Copy rule.
struct CoreFoundationRelease final {
    void operator()(const void* const object) const noexcept {
        if (object != nullptr) CFRelease(object);
    }
};

using OwnedString = std::unique_ptr<std::remove_pointer_t<CFStringRef>, CoreFoundationRelease>;
using OwnedNumber = std::unique_ptr<std::remove_pointer_t<CFNumberRef>, CoreFoundationRelease>;
using OwnedMutableDictionary = std::unique_ptr<std::remove_pointer_t<CFMutableDictionaryRef>, CoreFoundationRelease>;
using OwnedSet = std::unique_ptr<std::remove_pointer_t<CFSetRef>, CoreFoundationRelease>;
using OwnedDescriptor = std::unique_ptr<std::remove_pointer_t<CTFontDescriptorRef>, CoreFoundationRelease>;
using OwnedFont = std::unique_ptr<std::remove_pointer_t<CTFontRef>, CoreFoundationRelease>;
using OwnedArray = std::unique_ptr<std::remove_pointer_t<CFArrayRef>, CoreFoundationRelease>;
using OwnedData = std::unique_ptr<std::remove_pointer_t<CFDataRef>, CoreFoundationRelease>;

// CoreText weights for CSS 400 and 700 (NSFontWeightRegular and
// NSFontWeightBold), the values Skia's CSS-to-CoreText mapping gives.
constexpr CGFloat regular_core_text_weight = 0.0;
constexpr CGFloat bold_core_text_weight = 0.4;

constexpr std::uint32_t truetype_outlines = 0x00010000U;
constexpr std::uint32_t cff_outlines = 0x4F54544FU; // 'OTTO'
constexpr CTFontTableTag cff_tag = 0x43464620U;     // 'CFF '
constexpr CTFontTableTag cff2_tag = 0x43464632U;    // 'CFF2'
constexpr std::size_t sfnt_header_bytes = 12U;
constexpr std::size_t table_record_bytes = 16U;

struct FontTable final {
    CTFontTableTag tag{};
    OwnedData data;
};

[[nodiscard]] OwnedNumber make_number(const CGFloat value) {
    OwnedNumber result(CFNumberCreate(kCFAllocatorDefault, kCFNumberCGFloatType, &value));
    return result;
}

// The installed face CoreText resolves for the request, or null.
[[nodiscard]] OwnedFont match_font(const std::string_view family, const bool bold) {
    OwnedFont result;
    const OwnedString name(CFStringCreateWithBytes(kCFAllocatorDefault,
                                                   reinterpret_cast<const UInt8*>(family.data()),
                                                   static_cast<CFIndex>(family.size()), kCFStringEncodingUTF8, false));
    const OwnedMutableDictionary traits(CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                                                  &kCFTypeDictionaryValueCallBacks));
    const OwnedMutableDictionary attributes(CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
    const OwnedNumber weight = make_number(bold ? bold_core_text_weight : regular_core_text_weight);
    const OwnedNumber width = make_number(0.0);
    const OwnedNumber slant = make_number(0.0);
    if (!name || !traits || !attributes || !weight || !width || !slant) return result;
    CFDictionaryAddValue(traits.get(), kCTFontWeightTrait, weight.get());
    CFDictionaryAddValue(traits.get(), kCTFontWidthTrait, width.get());
    CFDictionaryAddValue(traits.get(), kCTFontSlantTrait, slant.get());
    CFDictionaryAddValue(attributes.get(), kCTFontTraitsAttribute, traits.get());
    CFDictionaryAddValue(attributes.get(), kCTFontFamilyNameAttribute, name.get());
    const OwnedDescriptor request(CTFontDescriptorCreateWithAttributes(attributes.get()));
    if (!request) return result;
    // The family name is mandatory: CoreText must not substitute another family.
    const void* mandatory_keys[] = {kCTFontFamilyNameAttribute};
    const OwnedSet mandatory(CFSetCreate(kCFAllocatorDefault, mandatory_keys, 1, &kCFTypeSetCallBacks));
    if (!mandatory) return result;
    const OwnedDescriptor matched(CTFontDescriptorCreateMatchingFontDescriptor(request.get(), mandatory.get()));
    if (!matched) return result;
    result.reset(CTFontCreateWithFontDescriptor(matched.get(), 0.0, nullptr));
    return result;
}

void write_u16(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value) {
    bytes[offset] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>(value & 0xFFU);
}

void write_u32(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value) {
    bytes[offset] = static_cast<std::byte>((value >> 24U) & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 16U) & 0xFFU);
    bytes[offset + 2U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    bytes[offset + 3U] = static_cast<std::byte>(value & 0xFFU);
}

// OpenType table checksum: the sum of the table's big-endian 32-bit words,
// the final word zero-padded.
[[nodiscard]] std::uint32_t table_checksum(const UInt8* const data, const std::size_t length) {
    std::uint32_t sum = 0U;
    for (std::size_t word = 0; word < length; word += 4U) {
        std::uint32_t value = 0U;
        for (std::size_t byte = 0; byte < 4U; ++byte) {
            const std::uint32_t octet = word + byte < length ? data[word + byte] : 0U;
            value = (value << 8U) | octet;
        }
        sum += value;
    }
    return sum;
}

[[nodiscard]] bool table_tag_less(const FontTable& left, const FontTable& right) noexcept {
    return left.tag < right.tag;
}

} // namespace

std::optional<std::vector<std::byte>> core_text_font_bytes(const std::string_view family, const bool bold,
                                                           const std::size_t maximum_bytes) {
    std::optional<std::vector<std::byte>> result;
    if (family.empty()) return result;
    const OwnedFont font = match_font(family, bold);
    if (!font) return result;
    const OwnedArray tags(CTFontCopyAvailableTables(font.get(), kCTFontTableOptionNoOptions));
    if (!tags) return result;
    const CFIndex count = CFArrayGetCount(tags.get());
    if (count <= 0 || count > 0xFFFF) return result;

    // Tables in tag order, as the sfnt directory requires. The array holds
    // unboxed tags, not CFNumbers.
    std::vector<FontTable> tables;
    tables.reserve(static_cast<std::size_t>(count));
    std::uint32_t outlines = truetype_outlines;
    std::size_t total = sfnt_header_bytes + table_record_bytes * static_cast<std::size_t>(count);
    for (CFIndex index = 0; index < count; ++index) {
        const CTFontTableTag tag =
            static_cast<CTFontTableTag>(reinterpret_cast<std::uintptr_t>(CFArrayGetValueAtIndex(tags.get(), index)));
        OwnedData data(CTFontCopyTable(font.get(), tag, kCTFontTableOptionNoOptions));
        if (!data) return result;
        if (tag == cff_tag || tag == cff2_tag) outlines = cff_outlines;
        const std::size_t length = static_cast<std::size_t>(CFDataGetLength(data.get()));
        total += (length + 3U) & ~std::size_t{3U};
        if (total > maximum_bytes) return result;
        tables.push_back(FontTable{tag, std::move(data)});
    }
    std::sort(tables.begin(), tables.end(), table_tag_less);

    // Header: version, table count, and the binary-search fields derived
    // from the largest power of two not above the count.
    std::vector<std::byte> bytes(total, std::byte{0});
    const std::uint32_t table_count = static_cast<std::uint32_t>(count);
    std::uint32_t power = 1U;
    std::uint32_t selector = 0U;
    while (power * 2U <= table_count) {
        power *= 2U;
        ++selector;
    }
    write_u32(bytes, 0U, outlines);
    write_u16(bytes, 4U, table_count);
    write_u16(bytes, 6U, power * 16U);
    write_u16(bytes, 8U, selector);
    write_u16(bytes, 10U, table_count * 16U - power * 16U);

    // Records, then each table at a four-byte boundary, zero-padded.
    std::size_t offset = sfnt_header_bytes + table_record_bytes * tables.size();
    for (std::size_t index = 0; index < tables.size(); ++index) {
        const FontTable& table = tables[index];
        const UInt8* const data = CFDataGetBytePtr(table.data.get());
        const std::size_t length = static_cast<std::size_t>(CFDataGetLength(table.data.get()));
        const std::size_t record = sfnt_header_bytes + table_record_bytes * index;
        write_u32(bytes, record, table.tag);
        write_u32(bytes, record + 4U, table_checksum(data, length));
        write_u32(bytes, record + 8U, static_cast<std::uint32_t>(offset));
        write_u32(bytes, record + 12U, static_cast<std::uint32_t>(length));
        if (length != 0U) std::copy_n(reinterpret_cast<const std::byte*>(data), length, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
        offset += (length + 3U) & ~std::size_t{3U};
    }
    result = std::move(bytes);
    return result;
}

} // namespace gui_drawing::render
