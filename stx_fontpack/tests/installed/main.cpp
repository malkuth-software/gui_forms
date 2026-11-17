// Installed stx_fontpack consumer: pack two fonts in an eager and a lazy
// group, open the pack as a lazy set, find both fonts and refuse a damaged
// container, using only the installed headers and the exported targets.
#include <stx_fontpack/loader.hpp>
#include <stx_fontpack/packer.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (condition) {
        return;
    }
    std::fprintf(stderr, "stx_fontpack: %s\n", message);
    std::abort();
}

stx_fontpack::PackInput make_input(const char* name, std::size_t size, std::uint32_t step) {
    stx_fontpack::PackInput input;
    input.name = name;
    input.data.resize(size);
    for (std::size_t k = 0; k < size; k += 1) {
        input.data[k] = static_cast<std::uint8_t>((k * step + k / 97U) & 255U);
    }
    return input;
}

}  // namespace

int main() {
    std::vector<stx_fontpack::PackInput> fonts;
    fonts.push_back(make_input("one.ttf", 5000, 29));
    fonts.push_back(make_input("two.ttf", 7000, 31));
    std::vector<stx_fontpack::PackGroupSpec> groups;
    std::string error;
    const std::string manifest = "group interface eager\none.ttf\ngroup more lazy\n*\n";
    require(stx_fontpack::manifest_groups(manifest, fonts, groups, error), "the manifest parses");
    std::vector<std::uint8_t> pack;
    require(stx_fontpack::pack_fonts(fonts, groups, stx_fontpack::serial_executor(), nullptr, pack, error), "the fonts pack");

    const stx_fontpack::Allocator allocator = stx_fontpack::standard_allocator();
    const stx_fontpack::Executor executor = stx_fontpack::serial_executor();
    std::unique_ptr<stx_fontpack::LoadedSet> set(new stx_fontpack::LoadedSet);
    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(pack.data()), pack.size());
    require(stx_fontpack::set_open(bytes, allocator, executor, *set), "the pack opens");
    require((*set).loaded[0] && !(*set).loaded[1], "only the eager group is decoded");
    for (std::size_t f = 0; f < fonts.size(); f += 1) {
        const stx_fontpack::LoadedFont* font = stx_fontpack::set_find_font(*set, fonts[f].name);
        require(font != nullptr && (*font).size == fonts[f].data.size() &&
                    std::memcmp((*font).data, fonts[f].data.data(), (*font).size) == 0,
                "a font loads byte for byte");
    }
    stx_fontpack::set_release(*set);

    pack[0] = static_cast<std::uint8_t>('X');
    require(!stx_fontpack::set_open(bytes, allocator, executor, *set) && (*set).index_refused,
            "a damaged pack is refused");
    return 0;
}
