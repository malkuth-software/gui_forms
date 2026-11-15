// The stand-in form compiler of the shared-source project. Arguments:
// <document> [--output <header>] [other options, ignored]. Reads the integer
// on the document's first line and writes a header defining FORM_VALUE as
// it, leaving the header untouched when its bytes would not change, as the
// form compiler does. Exits 2 on a malformed command line or document, 1
// when a file cannot be read or written.

#include "shared.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main(int argument_count, char** arguments) {
    if (argument_count < 2) {
        std::cerr << "fake_form_compiler: no document\n";
        return 2;
    }
    std::string document_path(arguments[1]);
    std::string output_path;
    for (int index = 2; index + 1 < argument_count; ++index) {
        std::string argument(arguments[index]);
        if (argument == "--output") {
            output_path = arguments[index + 1];
        }
    }
    if (output_path.empty()) {
        std::cerr << "fake_form_compiler: no --output\n";
        return 2;
    }
    std::ifstream document(document_path);
    int value = 0;
    if (!(document >> value)) {
        std::cerr << "fake_form_compiler: " << document_path << " holds no integer\n";
        return 2;
    }
    value += shared_offset();
    std::ostringstream text;
    text << "#pragma once\n#define FORM_VALUE " << value << '\n';
    std::string bytes = text.str();
    std::ifstream existing(output_path, std::ios::binary);
    std::ostringstream existing_text;
    existing_text << existing.rdbuf();
    if (existing && existing_text.str() == bytes) {
        return 0;
    }
    existing.close();
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    output << bytes;
    if (!output) {
        std::cerr << "fake_form_compiler: cannot write " << output_path << '\n';
        return 1;
    }
    return 0;
}
