#include "audio_worker.hpp"
#include <iostream>
#ifdef GUI_FORMS_REFERENCE_APPLICATION
#include "gui_forms/gui_forms.hpp"
#endif

int main(int argc, char** argv) {
    if (argc != 2) return 2;
#ifdef GUI_FORMS_REFERENCE_APPLICATION
    // Require real Core symbols from Application while the audio adapter uses
    // Worker through Audio. No GUI session is needed for this linkage fixture.
    const gui_forms::Control::Ptr control = gui_forms::make_control<gui_forms::Control>(
        gui_forms::StableId("audio-worker-linkage"));
    (*control).visible = true;
#endif
    const bool cancelled = cancel_ogg_load(std::filesystem::path(argv[1]));
    if (!cancelled) return 1;
    std::cout << "Worker cancelled an Ogg load without publishing a partial clip\n";
    return 0;
}
