// expect: warn warning: "?effects progress_stripes, progress_pulse, progress_laser, progress_woven_stripes and progress_bubbles are unverified: their bounds are not checked
// The progress bar's effects built without the effect pass: CMake defines
// GUI_FORMS_EFFECTS_UNVERIFIED for this case (as gui_forms_verify_effects
// does when GUI_FORMS_EFFECT_VERIFY is OFF or the plugin is unusable), and
// the file that registers them must say so. The case compiles the real file,
// so its warning cannot drift from it; the object is never linked.
#include "../../src/controls/range_control/progress_bar/progress_bar_effects.cpp"
