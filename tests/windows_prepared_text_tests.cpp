#include "prepared_text_test_support.hpp"
#include "../src/core/text/prepared/prepared_storage.hpp"

#include <cstdlib>
#include <iostream>

namespace gui_forms::host {
void run_windows_prepared_text_fixture(const std::span<const std::byte> fonts);
}

using namespace prepared_test;

int main(const int argc, char** const argv) {
    try {
        require(argc == 2, "font directory required");
        const std::vector<std::byte> bytes = read_font(std::filesystem::path(argv[1]));
        gui_forms::host::run_windows_prepared_text_fixture(bytes);
        std::cout << "Windows prepared text checks passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
