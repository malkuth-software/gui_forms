// stx_fontpack: the build-time packer tool.
//
//   fontpack pack <out.fpk> [--manifest <file>] [--threads N] [--verbose] <font>...
//       packs (one group per manifest group; one eager group "all" without a
//       manifest), then loads every group and compares every font with its
//       input; writes out.fpk only when they are identical
//   fontpack verify <in.fpk> [--threads N] <font>...
//       loads every group and compares against the given files
//   fontpack unpack <in.fpk> <directory>
//       writes every font of every group into directory
//   fontpack embed <in.fpk> <out.inc>
//       writes the pack's bytes as the body of a C++ array initializer
//       (decimal numbers, 24 to a line) for a program to compile in, after
//       checking that the pack opens; out.inc is rewritten only when its
//       content changes, so an unchanged pack rebuilds nothing
//
// A font's name in the pack is its file name without the directory. Exit
// status 0 on success, 1 on a failure (with a message), 2 on bad usage.
#include "stx_fontpack/loader.hpp"
#include "stx_fontpack/packer.hpp"
#include "thread_executor.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

typedef std::vector<std::uint8_t> Bytes;

bool read_file(const char* path, Bytes& bytes) {
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }
    bool ok = std::fseek(file, 0, SEEK_END) == 0;
    const long size = ok ? std::ftell(file) : -1;
    ok = ok && size >= 0 && std::fseek(file, 0, SEEK_SET) == 0;
    if (ok) {
        bytes.resize(static_cast<std::size_t>(size));
        ok = std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    }
    std::fclose(file);
    return ok;
}

bool write_file(const char* path, const std::uint8_t* data, std::size_t size) {
    std::FILE* file = std::fopen(path, "wb");
    if (file == nullptr) {
        return false;
    }
    const bool written = std::fwrite(data, 1, size, file) == size;
    const bool closed = std::fclose(file) == 0;
    return written && closed;
}

const char* base_name(const char* path) {
    const char* name = path;
    for (const char* at = path; *at != 0; at += 1) {
        if (*at == '/' || *at == '\\') {
            name = at + 1;
        }
    }
    return name;
}

std::string group_name(const stx_fontpack::PackGroup& group) {
    return std::string(group.name, group.name_length);
}

// Loads every group of pack and compares each font with the input of the same name.
bool verify(const Bytes& pack, const std::vector<stx_fontpack::PackInput>& fonts, const stx_fontpack::Executor& executor) {
    const stx_fontpack::Allocator allocator = stx_fontpack::standard_allocator();
    // PackIndex holds kMaxFonts records: on the heap, not the stack.
    std::unique_ptr<stx_fontpack::PackIndex> index(new stx_fontpack::PackIndex);
    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(pack.data()), pack.size());
    bool same = stx_fontpack::pack_open(bytes, *index);
    if (!same) {
        std::printf("verify: the pack does not open\n");
    }
    same = same && (*index).font_count == fonts.size();
    std::vector<bool> seen(fonts.size(), false);
    for (std::uint32_t g = 0; g < (*index).group_count && same; g += 1) {
        stx_fontpack::LoadedGroup loaded;
        if (!stx_fontpack::pack_load_group(*index, g, allocator, executor, loaded)) {
            std::printf("verify: group %s does not load (%s)\n", group_name((*index).groups[g]).c_str(),
                        stx_fontpack::refusal_name(loaded.refusal));
            same = false;
            break;
        }
        for (std::uint32_t f = 0; f < loaded.font_count && same; f += 1) {
            const stx_fontpack::LoadedFont& font = loaded.fonts[f];
            const std::string name(font.name, font.name_length);
            std::size_t k = 0;
            while (k < fonts.size() && (seen[k] || fonts[k].name != name)) {
                k += 1;
            }
            if (k == fonts.size() || font.size != fonts[k].data.size() ||
                std::memcmp(font.data, fonts[k].data.data(), font.size) != 0) {
                std::printf("verify: %s differs or is not an input\n", name.c_str());
                same = false;
            } else {
                seen[k] = true;
            }
        }
        stx_fontpack::group_release(loaded);
    }
    return same;
}

// Reads the font files at paths; a font is named by its file name.
bool read_fonts(const std::vector<const char*>& paths, std::vector<stx_fontpack::PackInput>& fonts, std::size_t& total) {
    total = 0;
    for (std::size_t k = 0; k < paths.size(); k += 1) {
        stx_fontpack::PackInput input;
        input.name = base_name(paths[k]);
        if (!read_file(paths[k], input.data)) {
            std::printf("cannot read %s\n", paths[k]);
            return false;
        }
        total += input.data.size();
        fonts.push_back(input);
    }
    return true;
}

struct Arguments {
    const char* manifest;
    std::uint32_t threads;
    bool verbose;
    std::vector<const char*> paths;
};

// Parses the options and file names after argv[first].
bool parse_arguments(int argc, char** argv, int first, Arguments& arguments) {
    arguments.manifest = nullptr;
    arguments.threads = 0;
    arguments.verbose = false;
    for (int k = first; k < argc; k += 1) {
        const bool has_value = k + 1 < argc;
        if (std::strcmp(argv[k], "--manifest") == 0 && has_value) {
            arguments.manifest = argv[k + 1];
            k += 1;
        } else if (std::strcmp(argv[k], "--threads") == 0 && has_value) {
            const long value = std::strtol(argv[k + 1], nullptr, 10);
            if (value < 0 || value > static_cast<long>(stx_fontpack::kMaxWorkers)) {
                return false;
            }
            arguments.threads = static_cast<std::uint32_t>(value);
            k += 1;
        } else if (std::strcmp(argv[k], "--verbose") == 0) {
            arguments.verbose = true;
        } else if (std::strncmp(argv[k], "--", 2) == 0) {
            return false;
        } else {
            arguments.paths.push_back(argv[k]);
        }
    }
    return true;
}

int command_pack(const char* out_path, const Arguments& arguments) {
    std::vector<stx_fontpack::PackInput> fonts;
    std::size_t total = 0;
    if (!read_fonts(arguments.paths, fonts, total) || fonts.empty()) {
        std::printf("pack: no fonts read\n");
        return 1;
    }
    std::vector<stx_fontpack::PackGroupSpec> groups;
    if (arguments.manifest != nullptr) {
        Bytes text;
        std::string error;
        if (!read_file(arguments.manifest, text)) {
            std::printf("cannot read %s\n", arguments.manifest);
            return 1;
        }
        if (!stx_fontpack::manifest_groups(std::string(text.begin(), text.end()), fonts, groups, error)) {
            std::printf("%s: %s\n", arguments.manifest, error.c_str());
            return 1;
        }
    } else {
        // The single-group baseline: every font in one eager group.
        stx_fontpack::PackGroupSpec all;
        all.name = "all";
        all.eager = true;
        for (std::size_t f = 0; f < fonts.size(); f += 1) {
            all.fonts.push_back(static_cast<std::uint32_t>(f));
        }
        groups.push_back(all);
    }
    const stx_fontpack::Executor executor = stx_fontpack_tools::thread_executor(arguments.threads);
    Bytes pack;
    std::string error;
    if (!stx_fontpack::pack_fonts(fonts, groups, executor, arguments.verbose ? stdout : nullptr, pack, error)) {
        std::printf("pack: %s\n", error.c_str());
        return 1;
    }
    if (!verify(pack, fonts, executor)) {
        std::printf("pack: the pack does NOT reproduce its inputs; nothing written\n");
        return 1;
    }
    if (!write_file(out_path, pack.data(), pack.size())) {
        std::printf("cannot write %s\n", out_path);
        return 1;
    }
    std::printf("packed %zu fonts in %zu groups: %zu -> %zu bytes, verified identical\n", fonts.size(), groups.size(), total,
                pack.size());
    return 0;
}

int command_verify(const char* pack_path, const Arguments& arguments) {
    Bytes pack;
    std::vector<stx_fontpack::PackInput> fonts;
    std::size_t total = 0;
    if (!read_file(pack_path, pack)) {
        std::printf("cannot read %s\n", pack_path);
        return 1;
    }
    if (!read_fonts(arguments.paths, fonts, total)) {
        return 1;
    }
    const stx_fontpack::Executor executor = stx_fontpack_tools::thread_executor(arguments.threads);
    const bool same = verify(pack, fonts, executor);
    std::printf("%s: %zu fonts, %zu bytes from a %zu byte pack\n", same ? "IDENTICAL" : "DIFFERENT", fonts.size(), total,
                pack.size());
    return same ? 0 : 1;
}

int command_unpack(const char* pack_path, const char* directory) {
    Bytes pack;
    if (!read_file(pack_path, pack)) {
        std::printf("cannot read %s\n", pack_path);
        return 1;
    }
    const stx_fontpack::Allocator allocator = stx_fontpack::standard_allocator();
    const stx_fontpack::Executor executor = stx_fontpack_tools::thread_executor(0);
    std::unique_ptr<stx_fontpack::LoadedSet> set(new stx_fontpack::LoadedSet);
    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(pack.data()), pack.size());
    bool ok = stx_fontpack::set_open(bytes, allocator, executor, *set);
    for (std::uint32_t g = 0; ok && g < (*set).index.group_count; g += 1) {
        ok = stx_fontpack::set_need_group(*set, g);
    }
    for (std::uint32_t g = 0; ok && g < (*set).index.group_count; g += 1) {
        const stx_fontpack::LoadedGroup& loaded = (*set).groups[g];
        for (std::uint32_t f = 0; ok && f < loaded.font_count; f += 1) {
            const stx_fontpack::LoadedFont& font = loaded.fonts[f];
            const std::string path = std::string(directory) + "/" + std::string(font.name, font.name_length);
            if (!write_file(path.c_str(), font.data, font.size)) {
                std::printf("cannot write %s\n", path.c_str());
                ok = false;
            }
        }
    }
    if (!ok) {
        std::printf("unpack failed\n");
    }
    stx_fontpack::set_release(*set);
    return ok ? 0 : 1;
}

int command_embed(const char* pack_path, const char* out_path) {
    Bytes pack;
    if (!read_file(pack_path, pack)) {
        std::printf("cannot read %s\n", pack_path);
        return 1;
    }
    std::unique_ptr<stx_fontpack::PackIndex> index(new stx_fontpack::PackIndex);
    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(pack.data()), pack.size());
    if (!stx_fontpack::pack_open(bytes, *index)) {
        std::printf("embed: %s does not open as a pack\n", pack_path);
        return 1;
    }
    std::string text = "// Generated by stx_fontpack embed from a font pack; do not edit.\n";
    // At most 4 characters per byte ("255,") plus a newline per 24 bytes.
    text.reserve(text.size() + pack.size() * 4 + pack.size() / 24 + 1);
    char number[8];
    for (std::size_t k = 0; k < pack.size(); k += 1) {
        const int written = std::snprintf(number, sizeof(number), "%u,", static_cast<unsigned>(pack[k]));
        text.append(number, static_cast<std::size_t>(written));
        if (k % 24 == 23 || k + 1 == pack.size()) {
            text.push_back('\n');
        }
    }
    Bytes existing;
    if (read_file(out_path, existing) && existing.size() == text.size() &&
        std::memcmp(existing.data(), text.data(), text.size()) == 0) {
        return 0;
    }
    if (!write_file(out_path, reinterpret_cast<const std::uint8_t*>(text.data()), text.size())) {
        std::printf("cannot write %s\n", out_path);
        return 1;
    }
    std::printf("embedded %zu pack bytes in %s\n", pack.size(), out_path);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Arguments arguments;
    if (argc >= 4 && std::strcmp(argv[1], "pack") == 0 && parse_arguments(argc, argv, 3, arguments)) {
        return command_pack(argv[2], arguments);
    }
    if (argc >= 4 && std::strcmp(argv[1], "verify") == 0 && parse_arguments(argc, argv, 3, arguments)) {
        return command_verify(argv[2], arguments);
    }
    if (argc == 4 && std::strcmp(argv[1], "unpack") == 0) {
        return command_unpack(argv[2], argv[3]);
    }
    if (argc == 4 && std::strcmp(argv[1], "embed") == 0) {
        return command_embed(argv[2], argv[3]);
    }
    std::printf("usage: fontpack pack <out.fpk> [--manifest <file>] [--threads N] [--verbose] <font>...\n"
                "       fontpack verify <in.fpk> [--threads N] <font>...\n"
                "       fontpack unpack <in.fpk> <directory>\n"
                "       fontpack embed <in.fpk> <out.inc>\n");
    return 2;
}
