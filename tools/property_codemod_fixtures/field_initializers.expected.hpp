// property_codemod selftest input: the migrated fields' initializers hold
// `;`, quotes, nested braces and comments; the kept field must survive.
#pragma once

#include <string>
#include <vector>

class Fixture {
public:
    void keep() noexcept;

private:
    int kept_{};
};
