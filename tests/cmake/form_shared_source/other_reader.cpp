// A target sharing shared.cpp with the form target, with no form of its own.

#include "shared.hpp"

#include <iostream>

int main() {
    int value = shared_offset();
    std::cout << value << '\n';
    return 0;
}
