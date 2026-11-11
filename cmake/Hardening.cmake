# Hardening flags and the warning level of first-party code (owner decision
# D11, 2026-11-10, option a: hardening on the shipped libraries now, a
# warning baseline per directory over time).
#
# GUI_FORMS_HARDENING (default ON) compiles every first-party target with
#   - -fstack-protector-strong: a canary in every function with a local
#     array or an address-taken local;
#   - -D_FORTIFY_SOURCE=2 outside Debug (the checks need optimization) and
#     outside sanitized builds (whose interceptors replace the checked
#     functions): bounds-checked copies where the size is known;
#   - on x86-64, -fcf-protection=full (CET shadow stack and indirect-branch
#     markers); on Linux arm64, -mbranch-protection=standard (PAC and BTI);
#   - on Windows (MinGW), -mguard=cf: Control Flow Guard checks on indirect
#     calls, enforced when the process is CFG-enabled;
# and links every first-party shared library and program with
#   - on Windows (MinGW), --guard-cf, so the image carries its CFG table
#     (lld already marks MinGW images dynamic-base, NX-compatible and
#     high-entropy);
#   - on Linux, -z relro -z now: the relocations read only after load.
# macOS images are position-independent and NX by default.
#
# GUI_FORMS_WARNINGS (default ON) compiles first-party code with -Wall
# -Wextra. Nothing fails on a warning: tools/warning_baseline.py counts the
# warning sites a build log reports per directory and fails when a
# directory reports more than tools/warning_baseline.json records for the
# host, so the count only goes down.

option(GUI_FORMS_HARDENING "Compile and link first-party code with the hardening flags (cmake/Hardening.cmake)" ON)
option(GUI_FORMS_WARNINGS "Compile first-party code with -Wall -Wextra (cmake/Hardening.cmake)" ON)

set(GUI_FORMS_HARDENING_COMPILE_OPTIONS "")
set(GUI_FORMS_HARDENING_LINK_OPTIONS "")
if(GUI_FORMS_HARDENING AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    list(APPEND GUI_FORMS_HARDENING_COMPILE_OPTIONS -fstack-protector-strong)
    if(NOT CMAKE_CXX_FLAGS MATCHES "-fsanitize=")
        # A compiler that defines _FORTIFY_SOURCE itself (some Linux
        # distributions' GCC) would see a redefinition.
        list(APPEND GUI_FORMS_HARDENING_COMPILE_OPTIONS
            $<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE> $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=2>)
    endif()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
        list(APPEND GUI_FORMS_HARDENING_COMPILE_OPTIONS -fcf-protection=full)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        list(APPEND GUI_FORMS_HARDENING_COMPILE_OPTIONS -mbranch-protection=standard)
    endif()
    if(MINGW)
        list(APPEND GUI_FORMS_HARDENING_COMPILE_OPTIONS -mguard=cf)
        list(APPEND GUI_FORMS_HARDENING_LINK_OPTIONS LINKER:--guard-cf)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND GUI_FORMS_HARDENING_LINK_OPTIONS LINKER:-z,relro LINKER:-z,now)
    endif()
endif()

set(GUI_FORMS_WARNING_OPTIONS "")
if(GUI_FORMS_WARNINGS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set(GUI_FORMS_WARNING_OPTIONS -Wall -Wextra)
endif()

# Gives `target` (a first-party target of type `type`) the hardening and
# warning options; the link options go to linked images only.
function(gui_forms_harden_target target type)
    set(options ${GUI_FORMS_HARDENING_COMPILE_OPTIONS} ${GUI_FORMS_WARNING_OPTIONS})
    if(options)
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:${options}>")
    endif()
    if(type MATCHES "^(SHARED_LIBRARY|MODULE_LIBRARY|EXECUTABLE)$")
        target_link_options(${target} PRIVATE ${GUI_FORMS_HARDENING_LINK_OPTIONS})
    endif()
endfunction()
