// The C ABI library's DllMain (cmake/CAbi.cmake, the Windows library only):
// marks the module's teardown at DLL_PROCESS_DETACH, before its static
// destructors run (allocation/library_memory/library_teardown.hpp,
// "A module's end").
//
// The order: the CRT's DLL entry point (mingw-w64's __DllMainCRTStartup,
// and the MSVC runtime's _DllMainCRTStartup) calls DllMain first at
// DLL_PROCESS_DETACH and then runs the module's atexit table, its static
// destructors among them. So from the first destructor on, an allocation
// STXlib's allocator refuses (the process's end, once ExitProcess has
// stopped the other threads; or exhaustion during a FreeLibrary) comes from
// the teardown reserve or fails fast with a record, and never throws.
//
// Nothing here takes a lock, loads a module or allocates: it runs under
// the loader lock.

#include "gui_forms/allocation/library_memory/library_teardown.hpp"

#include <windows.h>

extern "C" BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved);

extern "C" BOOL WINAPI DllMain(const HINSTANCE module, const DWORD reason, const LPVOID reserved) {
    static_cast<void>(module);
    static_cast<void>(reserved);
    if (reason == DLL_PROCESS_DETACH) {
        gui_forms::detail::begin_library_teardown();
    }
    return TRUE;
}
