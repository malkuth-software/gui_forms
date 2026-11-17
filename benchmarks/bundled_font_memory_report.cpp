// Process memory of the bundled fonts for N windows (item 4, step 6). Not a test: run it in a
// fresh process per configuration and compare the reports.
//
//   gui_forms_bundled_font_memory_report <font directory> <files|shared> <windows> <latin|cjk|all>
//
// files:  each window registers every bundled face by path (SkiaRaster::register_typeface_file),
//         as the hosts did before the shared set: a private copy per window. Before the SkData
//         file loaders were removed this mode mapped each file per window instead.
// shared: each window borrows the process's BundledFontSet: verified private read-only copies,
//         one per process, fallbacks read when text first needs them.
// The text each window draws decides which fallbacks a shared set reads.

#include "../src/render/skia/raster/skia_raster.hpp"
#if defined(_WIN32)
#include "../src/surface/windows/fonts/sealed_pages_windows.hpp"
#else
#include "../src/surface/posix/fonts/sealed_pages_posix.hpp"
#endif

#if defined(__APPLE__)
#include <mach/mach.h>
#endif

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

struct MemorySample final {
    std::uint64_t resident{};
    // macOS: phys_footprint; Linux: RssAnon. Private memory the process is charged for.
    std::uint64_t private_bytes{};
    // macOS: external (file-backed) resident; Linux: RssFile.
    std::uint64_t file_bytes{};
};

#if defined(__linux__)
std::uint64_t status_kib(const std::string& text, std::string_view key) {
    const std::string::size_type at = text.find(std::string(key));
    if (at == std::string::npos) return 0;
    const std::uint64_t kib = std::strtoull(text.c_str() + at + key.size(), nullptr, 10);
    return kib * 1024U;
}
#endif

MemorySample sample_memory() {
    MemorySample sample{};
#if defined(__APPLE__)
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        sample.resident = info.resident_size;
        sample.private_bytes = info.phys_footprint;
        sample.file_bytes = info.external;
    }
#elif defined(__linux__)
    std::ifstream stream("/proc/self/status");
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    sample.resident = status_kib(text, "VmRSS:");
    sample.private_bytes = status_kib(text, "RssAnon:");
    sample.file_bytes = status_kib(text, "RssFile:");
#endif
    return sample;
}

std::string_view sample_text(std::string_view text) {
    if (text == "cjk") return "Plan Paint 漢字 日本語 中文";
    if (text == "all") return "Plan 漢字 مرحبا שלום नमस्ते বাংলা ਪੰਜਾਬੀ 🚀";
    return "Plan Paint, File Edit View";
}

bool register_files(gui_forms::render::SkiaRaster& raster, const std::filesystem::path& directory) {
    for (const gui_forms::detail::BundledFaceUse& use : gui_forms::detail::bundled_face_uses()) {
        const std::string path = (directory / std::filesystem::path(use.file_name)).string();
        const bool registered = use.role
            ? raster.register_typeface_file(*use.role, use.weight, use.italic, path.c_str())
            : raster.register_fallback_typeface_file(use.weight, use.italic, path.c_str());
        if (!registered && use.required) return false;
    }
    return true;
}

void print(const char* label, const MemorySample& sample) {
    std::printf("%s resident=%llu private=%llu file=%llu\n", label,
                static_cast<unsigned long long>(sample.resident),
                static_cast<unsigned long long>(sample.private_bytes),
                static_cast<unsigned long long>(sample.file_bytes));
}

// A decimal count argument: the whole text as an int, else 0 (a value the
// caller refuses). Locale-free, and reports overflow (atoi does neither).
[[nodiscard]] int parsed_count(const std::string_view text) noexcept {
    int value = 0;
    const char* const end = text.data() + text.size();
    const std::from_chars_result parsed = std::from_chars(text.data(), end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        return 0;
    }
    return value;
}

} // namespace

int main(int argument_count, char** arguments) {
    if (argument_count != 5) {
        std::fprintf(stderr, "usage: %s <font directory> <files|shared> <windows> <latin|cjk|all>\n",
                     arguments[0]);
        return 2;
    }
    const std::filesystem::path directory(arguments[1]);
    const std::string_view mode(arguments[2]);
    const int windows = parsed_count(arguments[3]);
    const std::string_view text = sample_text(arguments[4]);
    if ((mode != "files" && mode != "shared") || windows < 1 || windows > 64) return 2;
#if defined(_WIN32)
    const gui_forms::detail::SealedPageFunctions pages = gui_forms::host::windows_sealed_pages();
#else
    const gui_forms::detail::SealedPageFunctions pages = gui_forms::host::posix_sealed_pages();
#endif

    std::vector<std::unique_ptr<gui_forms::render::SkiaRaster>> rasters;
    for (int index = 0; index < windows; ++index) {
        std::unique_ptr<gui_forms::render::SkiaRaster> raster = std::make_unique<gui_forms::render::SkiaRaster>();
        if (!(*raster).resize({440, 60}, 2)) return 1;
        rasters.push_back(std::move(raster));
    }
    const MemorySample before = sample_memory();
    std::shared_ptr<gui_forms::detail::BundledFontSet> set;
    if (mode == "shared") set = gui_forms::detail::shared_bundled_font_set(directory, pages);
    for (const std::unique_ptr<gui_forms::render::SkiaRaster>& raster : rasters) {
        const bool registered = mode == "shared" ? (*raster).register_bundled_fonts(set)
                                                 : register_files(*raster, directory);
        if (!registered) return 1;
    }
    const MemorySample registered = sample_memory();
    for (const std::unique_ptr<gui_forms::render::SkiaRaster>& raster : rasters) {
        gui_forms::DamageRegion damage;
        damage.add({0, 0, 440, 60});
        (*raster).begin_frame(damage);
        (*raster).fill_rect({0, 0, 440, 60}, gui_forms::Color::rgba(255, 255, 255));
        (*raster).draw_text_utf8({8, 36}, text, {gui_forms::FontRole::control, 20, 400, false},
                                 gui_forms::Color::rgba(0, 0, 0));
        (*raster).end_frame();
    }
    const MemorySample drawn = sample_memory();
    std::printf("mode=%s windows=%d text=%s set_faces=%zu set_bytes=%llu\n", arguments[2], windows,
                arguments[4], set ? (*set).loaded_face_count() : 0U,
                static_cast<unsigned long long>(set ? (*set).loaded_bytes() : 0U));
    print("before", before);
    print("registered", registered);
    print("drawn", drawn);
    return 0;
}
