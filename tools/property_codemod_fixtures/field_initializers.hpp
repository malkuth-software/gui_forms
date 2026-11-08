// property_codemod selftest input: the migrated fields' initializers hold
// `;`, quotes, nested braces and comments; the kept field must survive.
#pragma once

#include <string>
#include <vector>

class Fixture {
public:
    void keep() noexcept;

private:
    std::string delimiters_{",;"};
    std::string separator_ = ";"; // a trailing comment; with a semicolon
    char terminator_{';'};
    std::vector<std::vector<int>> groups_{{1, 2}, {3}};
    std::string escaped_{"\";"};
    int plain_;
    int kept_{};
};
