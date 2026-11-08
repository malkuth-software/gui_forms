// The collection gallery's model (stage B reference): the sources its
// form binds, declared before the window that binds them.

#pragma once

#include <gui_forms/gui_forms.hpp>

class GalleryModel final {
public:
    GalleryModel() { music.set_checked(true); }

    gui_forms::Command run{"run", "Run"};
    gui_forms::Command music{"music", "Music"};
    gui_forms::Value<double> volume{40.0};
    gui_forms::Value<int> selection{1};
    gui_forms::Value<bool> first_open{true};
    gui_forms::Value<bool> second_open{false};
};
