#include "gui_forms/build_profile/build_profile.hpp"

// The link marker of this library's mode (build_profile.hpp): only a library
// of that mode defines it.
#if defined(GUI_FORMS_HIGH_INTEGRITY)
extern "C" {
GUI_FORMS_API const int gui_forms_build_profile_high_integrity_v1 = 1;
}
#else
extern "C" {
GUI_FORMS_API const int gui_forms_build_profile_standard_v1 = 1;
}
#endif

namespace gui_forms {

BuildProfileRecord build_profile() noexcept {
    BuildProfileRecord record{};
#if defined(GUI_FORMS_HIGH_INTEGRITY)
    record.profile = BuildProfile::high_integrity;
#if defined(GUI_FORMS_HIGH_INTEGRITY_ALLOW_CLIPBOARD_TEXT)
    record.clipboard_text = true;
#else
    record.clipboard_text = false;
#endif
#else
    record.profile = BuildProfile::standard;
    record.clipboard_text = true;
#endif
    return record;
}

} // namespace gui_forms
