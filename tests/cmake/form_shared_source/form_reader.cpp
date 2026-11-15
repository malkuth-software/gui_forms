// Prints the value of the header gui_forms_add_form generated.

#include "sample_form.hpp"
#include "shared.hpp"

#include <iostream>

int main() {
    int value = FORM_VALUE + shared_offset();
    std::cout << value << '\n';
    return 0;
}
