// The private STXlib bridge (src/platform/stxlib_bridge.hpp; A2 T0, D15 V0):
// text and views convert both ways over the same storage, and standard text
// that is not well-formed UTF-8 is refused with the offset of its first
// ill-formed byte.

#include "../src/platform/stxlib_bridge.hpp"

#include <stxlib/error.hpp>

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>

namespace {

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        failures = failures + 1;
    }
}

void text_round_trip() {
    const std::string_view source = "ma\xC3\xAEtre d'h\xC3\xB4tel";
    const stxlib::Result<stxlib::TextView> validated = gui_forms::platform::validated_text(source);
    require(validated.succeeded(), "well-formed UTF-8 is accepted");
    if (!validated.succeeded()) {
        return;
    }
    const stxlib::TextView text = *validated.value();
    require(text.data() == source.data() && text.count() == source.size(),
            "validated text borrows the same bytes, all of them");
    const std::string_view back = gui_forms::platform::standard_text(text);
    require(back.data() == source.data() && back == source, "STXlib text converts back losslessly");

    const stxlib::Result<stxlib::TextView> empty = gui_forms::platform::validated_text(std::string_view{});
    require(empty.succeeded() && (*empty.value()).count() == 0U, "empty text is valid and empty");
    require(gui_forms::platform::standard_text(stxlib::TextView{}).empty(), "an empty STXlib view is empty");
}

void invalid_text_refused() {
    // A lone continuation byte at offset 3, then a truncated sequence.
    const std::string_view ill_formed = "abc\x80" "def";
    const stxlib::Result<stxlib::TextView> refused = gui_forms::platform::validated_text(ill_formed);
    require(!refused.succeeded(), "ill-formed UTF-8 is refused");
    if (!refused.succeeded()) {
        const stxlib::ErrorRecord& record = *refused.error();
        require(record.kind() == stxlib::ErrorKind::invalid_argument && record.code() == 3U,
                "the refusal names the first ill-formed byte's offset");
    }
    const std::string_view truncated = "ok\xE2\x82";
    require(!gui_forms::platform::validated_text(truncated).succeeded(), "a truncated sequence is refused");
}

void view_round_trip() {
    std::array<std::int64_t, 4> storage{1, 2, 3, 4};
    const stxlib::MutableView<std::int64_t> mutable_view = gui_forms::platform::stxlib_mutable_view(storage);
    require(mutable_view.data() == storage.data() && mutable_view.count() == storage.size(),
            "fixed storage becomes a mutable view of all of it");
    const std::span<std::int64_t> writable = gui_forms::platform::standard_span(mutable_view);
    writable[2] = 30;
    require(storage[2] == 30 && writable.size() == storage.size(), "the span writes the same storage");

    const std::span<const std::int64_t> prefix(storage.data(), 2U);
    const stxlib::View<std::int64_t> view = gui_forms::platform::stxlib_view(prefix);
    require(view.data() == storage.data() && view.count() == 2U, "a span's prefix becomes a view of it");
    const std::span<const std::int64_t> back = gui_forms::platform::standard_span(view);
    require(back.data() == storage.data() && back.size() == 2U && back[1] == 2, "a view converts back");

    const std::span<std::int64_t> none{};
    const stxlib::MutableView<std::int64_t> empty = gui_forms::platform::stxlib_mutable_view(none);
    require(empty.count() == 0U && gui_forms::platform::standard_span(empty).empty(), "an empty span stays empty");
}

} // namespace

int main() {
    text_round_trip();
    invalid_text_refused();
    view_round_trip();
    if (failures != 0) {
        std::cerr << failures << " bridge checks failed\n";
        return 1;
    }
    std::cout << "stxlib bridge: all checks passed\n";
    return 0;
}
