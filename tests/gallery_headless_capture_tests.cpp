// The Gallery's headless page capture (demo/gallery_headless_capture.hpp,
// `--page <id> --capture <path.png> --headless`): the parsed options, a page
// staged and captured offscreen into a PNG of the requested size and scale
// with no host window, and the refusals (no --capture: exit 2; an unwritable
// path: exit 1, saved false).

#include "demo/gallery.hpp"
#include "demo/gallery_headless_capture.hpp"
#include "demo/gallery_staging.hpp"
#include "demo/program_options.hpp"
#include "gui_forms/window/window.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using namespace gui_forms;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        failures = failures + 1;
    }
}

// The options of `arguments`, parsed as a Gallery main parses them.
std::optional<demo::ProgramOptions> parsed(std::vector<std::string>& arguments) {
    std::vector<char*> pointers{};
    pointers.reserve(arguments.size());
    for (std::string& argument : arguments) pointers.push_back(argument.data());
    std::optional<demo::ProgramOptions> options =
        demo::parse_program_options(static_cast<int>(pointers.size()), pointers.data());
    return options;
}

// The file's first bytes, up to `count`.
std::vector<unsigned char> leading_bytes(const std::filesystem::path& path, std::size_t count) {
    std::vector<unsigned char> bytes(count, 0U);
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return {};
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(count));
    if (file.gcount() != static_cast<std::streamsize>(count)) return {};
    return bytes;
}

// A big-endian 32-bit word of `bytes` at `offset` (the PNG header's width
// and height).
std::uint32_t big_endian_word(const std::vector<unsigned char>& bytes, std::size_t offset) {
    std::uint32_t word = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        word = (word << 8U) | static_cast<std::uint32_t>(bytes[offset + index]);
    }
    return word;
}

void test_options() {
    std::vector<std::string> arguments{"gallery", "--page", "industrial-surfaces", "--capture", "page.png",
                                       "--headless", "--size", "640x400", "--scale", "2"};
    const std::optional<demo::ProgramOptions> options = parsed(arguments);
    require(options.has_value() && (*options).headless && (*options).page == "industrial-surfaces" &&
                (*options).capture_path == "page.png" && (*options).capture_size.has_value() &&
                (*options).scale_hint == 2.0,
            "--headless parses beside --page, --capture, --size and --scale");
    std::vector<std::string> plain{"gallery", "--page", "industrial-surfaces"};
    const std::optional<demo::ProgramOptions> windowed = parsed(plain);
    require(windowed.has_value() && !(*windowed).headless, "without --headless the Gallery shows its window");
}

void test_capture() {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "gui_forms_gallery_headless";
    std::error_code ignored{};
    std::filesystem::create_directories(directory, ignored);
    const std::filesystem::path png = directory / "industrial-surfaces.png";
    std::filesystem::remove(png, ignored);
    std::vector<std::string> arguments{"gallery", "--page", "industrial-surfaces", "--capture", png.string(),
                                       "--headless", "--size", "640x400", "--scale", "2"};
    const std::optional<demo::ProgramOptions> options = parsed(arguments);
    require(options.has_value(), "the options parse");
    if (!options) return;
    std::unique_ptr<Window> window = gallery::make_gallery();
    require(gallery::apply_gallery_staging(*options, *window), "the page is staged");
    const int code = gallery::run_headless_gallery_capture(*options, *window, GUI_FORMS_TEST_FONT_DIR);
    require(code == 0, "the headless capture saves the page");
    const std::vector<unsigned char> header = leading_bytes(png, 24U);
    const std::array<unsigned char, 8> signature{0x89U, 'P', 'N', 'G', 0x0DU, 0x0AU, 0x1AU, 0x0AU};
    bool is_png = header.size() == 24U;
    for (std::size_t index = 0U; is_png && index < signature.size(); ++index) {
        if (header[index] != signature[index]) is_png = false;
    }
    require(is_png, "the file is a PNG");
    if (is_png) {
        require(big_endian_word(header, 16U) == 1280U && big_endian_word(header, 20U) == 800U,
                "at the requested size and scale: 640 x 400 at 2");
    }
    require(window.get() != nullptr && (*window).client_size().width == 640.0, "the window took the --size");
    std::filesystem::remove(png, ignored);
}

void test_refusals() {
    std::vector<std::string> no_path{"gallery", "--headless"};
    const std::optional<demo::ProgramOptions> without = parsed(no_path);
    require(without.has_value(), "--headless alone parses");
    if (without) {
        std::unique_ptr<Window> window = gallery::make_gallery();
        require(gallery::run_headless_gallery_capture(*without, *window, GUI_FORMS_TEST_FONT_DIR) == 2,
                "--headless without --capture is a usage error");
    }
    const std::filesystem::path missing =
        std::filesystem::temp_directory_path() / "gui_forms_gallery_headless_no_such_directory" / "page.png";
    std::vector<std::string> unwritable{"gallery", "--capture", missing.string(), "--headless"};
    const std::optional<demo::ProgramOptions> options = parsed(unwritable);
    require(options.has_value(), "the options parse");
    if (options) {
        std::unique_ptr<Window> window = gallery::make_gallery();
        require(gallery::run_headless_gallery_capture(*options, *window, GUI_FORMS_TEST_FONT_DIR) == 1,
                "a path that cannot be written fails with exit 1");
    }
}

} // namespace

int main() {
    test_options();
    test_capture();
    test_refusals();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "Gallery headless capture tests passed\n";
    return EXIT_SUCCESS;
}
