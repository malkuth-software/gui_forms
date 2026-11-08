#pragma once

// The state forms/collection_gallery.form binds, found by name: both sliders
// bind `volume` and both check boxes run `music`. The program binds the
// choices to `selection` and the sections to `first_open` and `second_open`.

#include <gui_forms/binding.hpp>
#include <gui_forms/commands.hpp>
#include <gui_forms/value.hpp>

namespace reference {

struct GalleryModel final {
    gui_forms::Command run{"run", "Run"};
    gui_forms::Command music{"music", "Music"};
    gui_forms::Value<double> volume{40.0};
    gui_forms::Value<int> selection{1};
    gui_forms::Value<bool> first_open{true};
    gui_forms::Value<bool> second_open{false};
};

} // namespace reference
