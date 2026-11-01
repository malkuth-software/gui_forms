// A shared library of its own: it uses no GUI.Forms control, only the
// GUI_FORMS_LOCALIZABLE mark on a function of its own, so it carries its own
// table without a second copy of the toolkit.
#include "gui_forms/text/localizable/localizable_property.hpp"

#include <string_view>

namespace localizable_units {

namespace {

std::string_view shared_caption(GUI_FORMS_LOCALIZABLE(title) std::string_view caption) {
    return caption;
}

} // namespace

#if defined(_WIN32)
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
std::string_view shared_unit() {
    return shared_caption("Shared library title");
}

} // namespace localizable_units
