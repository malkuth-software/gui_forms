# Everything GUI.Forms takes from malkuth-software/stx, consumed as one built
# package (cmake/stx_package/CMakeLists.txt): the stx libraries (stx_zlib,
# stx_png, stx_conv, stx_rtf2md and, for audio builds, stx_vorbis; owner ruling
# 2026-11-03, decision 3(a)), stx_skia, and the third-party sources of stx
# third_party/ (FreeType, HarfBuzz, SheenBidi, libunibreak, MD4C; owner,
# 2026-11-05: this repository carries no third-party sources). This build
# compiles none of them: it links the package.
#
# Inputs:
# - stx.lock names the stx commit GUI.Forms builds with, in lockstep.
# - GUI_FORMS_STX_PREFIX=<dir> names a package already built (an SDK build, a
#   product's lockstep build): nothing is fetched or built, and the configure
#   stops unless the package was built from stx.lock's commit with the
#   renderer, text and audio choices of this configuration.
# - GUI_FORMS_STX_PACKAGE_DIR=<dir> (default <build>/stx-package) is where the
#   package is built, and reused while its stamp matches: the stx commit, this
#   file and the package recipe, and the compilers, build type, flags,
#   generator and choices passed to the recipe. CI restores it from a cache
#   keyed by the same inputs, so a matching package is linked without fetching
#   stx at all; a stamp that differs (or no package) fetches the stx source
#   and builds the package again at configure time.
# - FETCHCONTENT_SOURCE_DIR_GUI_FORMS_STX=<dir> replaces the pinned commit with
#   a local stx checkout (lockstep development); the package is then brought up
#   to date before every build, so edits there reach this build.
# - GUI_FORMS_ENABLE_SKIA, GUI_FORMS_ENABLE_HARFBUZZ_TEXT, and
#   GUI_FORMS_BUILD_AUDIO with GUI_FORMS_AUDIO_VORBIS_BACKEND=stx choose what
#   the package holds.
#
# Outputs:
# - the imported targets stx_zlib::stx_zlib, stx_png::stx_png,
#   stx_conv::stx_conv, stx_rtf2md::stx_rtf2md (and stx_vorbis's when asked);
#   stx_skia::swizzle and, with the renderer, stx_skia::stx_skia; stx_text::md4c
#   (objects) and, with the text engine, stx_text::freetype, stx_text::harfbuzz,
#   stx_text::sheenbidi, stx_text::linebreak and stx_text::linebreak_conformance;
# - GUI_FORMS_STX_PREFIX_DIR, the package's stx libraries (<package>/stx), which
#   the SDK install copies whole (GUIFormsConfig finds the packages beside
#   GUI.Forms);
# - GUI_FORMS_STX_BUILD_PREFIX_DIR, what only this build links
#   (<package>/build), with the MD4C header (include/md4c), the libunibreak
#   conformance data (share/stx_text/linebreak), the markdown corpus
#   (share/stx_text/rtf2md_corpus) and the licences the SDK and the programs
#   ship (share/stx_text/notices, at their paths in the stx source);
# - GUI_FORMS_STX_SOURCE_DIR, the stx source when this configure fetched it (or
#   the local checkout), and empty when the package was reused: only the
#   fuzzers compile stx sources, and they fetch it always.
#
# A failed package build stops the configure and leaves no stamp.
include(FetchContent)
get_filename_component(_gui_stx_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
# The stamp's file digests ignore line endings (cmake/StxDigest.cmake).
include("${CMAKE_CURRENT_LIST_DIR}/StxDigest.cmake")
set(GUI_FORMS_STX_PREFIX "" CACHE PATH "A built stx package (cmake/stx_package) to link as is (empty: build or reuse GUI_FORMS_STX_PACKAGE_DIR)")
set(GUI_FORMS_STX_PACKAGE_DIR "${CMAKE_BINARY_DIR}/stx-package" CACHE PATH "Where the stx package is built and reused")
file(TO_CMAKE_PATH "${GUI_FORMS_STX_PREFIX}" _gui_stx_given_prefix)
file(TO_CMAKE_PATH "${GUI_FORMS_STX_PACKAGE_DIR}" _gui_stx_package_dir)

# The package's choices, recorded in its stamp and checked for a given prefix.
set(_gui_stx_with_vorbis OFF)
if(GUI_FORMS_BUILD_AUDIO AND GUI_FORMS_AUDIO_VORBIS_BACKEND STREQUAL "stx")
    set(_gui_stx_with_vorbis ON)
endif()
set(_gui_stx_with_skia OFF)
if(GUI_FORMS_ENABLE_SKIA)
    set(_gui_stx_with_skia ON)
endif()
set(_gui_stx_with_text OFF)
if(GUI_FORMS_ENABLE_HARFBUZZ_TEXT)
    set(_gui_stx_with_text ON)
endif()
set(_gui_stx_choices "skia = ${_gui_stx_with_skia}\ntext = ${_gui_stx_with_text}\nvorbis = ${_gui_stx_with_vorbis}\n")

file(STRINGS "${_gui_stx_root}/stx.lock" _gui_stx_lock REGEX "^(repository|commit) = ")
set(_gui_stx_repository "")
set(_gui_stx_commit "")
foreach(_gui_stx_line IN LISTS _gui_stx_lock)
    if(_gui_stx_line MATCHES "^repository = \"([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+)\"$")
        set(_gui_stx_repository "${CMAKE_MATCH_1}")
    elseif(_gui_stx_line MATCHES "^commit = \"([0-9a-f]+)\"$")
        set(_gui_stx_commit "${CMAKE_MATCH_1}")
    endif()
endforeach()
string(LENGTH "${_gui_stx_commit}" _gui_stx_commit_length)
if(_gui_stx_repository STREQUAL "" OR NOT _gui_stx_commit_length EQUAL 40)
    message(FATAL_ERROR "stx.lock needs repository = \"owner/name\" and a 40-character commit")
endif()

set(_gui_stx_overridden OFF)
if(DEFINED FETCHCONTENT_SOURCE_DIR_GUI_FORMS_STX AND NOT FETCHCONTENT_SOURCE_DIR_GUI_FORMS_STX STREQUAL "")
    set(_gui_stx_overridden ON)
endif()
set(_gui_stx_stamp_name "gui-forms-stx-package.stamp")
set(GUI_FORMS_STX_SOURCE_DIR "")

# Fetches the stx source at stx.lock's commit (or takes the local checkout)
# into GUI_FORMS_STX_SOURCE_DIR; nothing is built here.
macro(_gui_stx_fetch_source)
    FetchContent_Declare(gui_forms_stx
        GIT_REPOSITORY https://github.com/${_gui_stx_repository}.git
        GIT_TAG ${_gui_stx_commit}
        GIT_CONFIG core.autocrlf=false
        SOURCE_SUBDIR gui-forms-no-upstream-build)
    FetchContent_MakeAvailable(gui_forms_stx)
    set(GUI_FORMS_STX_SOURCE_DIR "${gui_forms_stx_SOURCE_DIR}")
    if(NOT EXISTS "${GUI_FORMS_STX_SOURCE_DIR}/stx_zlib/CMakeLists.txt" OR
       NOT EXISTS "${GUI_FORMS_STX_SOURCE_DIR}/stx_skia/CMakeLists.txt" OR
       NOT EXISTS "${GUI_FORMS_STX_SOURCE_DIR}/third_party/md4c/md4c.c")
        message(FATAL_ERROR "the stx source at ${GUI_FORMS_STX_SOURCE_DIR} is not an stx checkout with stx_skia/ "
                            "and third_party/ (stx.lock must name a commit after the move of 2026-11-08)")
    endif()
endmacro()

if(NOT _gui_stx_given_prefix STREQUAL "")
    # A package built elsewhere: read its stamp's record, never rebuild it.
    set(_gui_stx_package "${_gui_stx_given_prefix}")
    if(NOT EXISTS "${_gui_stx_package}/${_gui_stx_stamp_name}")
        message(FATAL_ERROR "GUI_FORMS_STX_PREFIX=${_gui_stx_package} is not a built stx package (no "
                            "${_gui_stx_stamp_name}); configure once without it and pass that build's "
                            "GUI_FORMS_STX_PACKAGE_DIR (default <build>/stx-package)")
    endif()
    file(READ "${_gui_stx_package}/${_gui_stx_stamp_name}" _gui_stx_recorded)
    string(FIND "${_gui_stx_recorded}" "commit = ${_gui_stx_commit}\n${_gui_stx_choices}" _gui_stx_record_at)
    if(NOT _gui_stx_record_at EQUAL 0)
        message(FATAL_ERROR "the stx package at ${_gui_stx_package} was built as\n${_gui_stx_recorded}"
                            "but this configuration needs\ncommit = ${_gui_stx_commit} (stx.lock)\n${_gui_stx_choices}")
    endif()
    message(STATUS "stx: linking the package at ${_gui_stx_package}")
else()
    set(_gui_stx_package "${_gui_stx_package_dir}")
    set(_gui_stx_build "${CMAKE_BINARY_DIR}/stx-package-build")
    set(_gui_stx_recipe "${_gui_stx_root}/cmake/stx_package")
    # The settings the package is built with; each is passed only when this
    # build sets it. The build type's own flags are included, so a sanitized
    # build's debug information reaches the package as it did when these
    # sources were part of this build.
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _gui_stx_build_type)
    set(_gui_stx_arguments
        -DGUI_FORMS_STX_PACKAGE_SKIA=${_gui_stx_with_skia}
        -DGUI_FORMS_STX_PACKAGE_TEXT=${_gui_stx_with_text}
        -DSTX_WITH_VORBIS=${_gui_stx_with_vorbis}
        "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
        "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
    set(_gui_stx_settings CMAKE_BUILD_TYPE CMAKE_TOOLCHAIN_FILE CMAKE_MAKE_PROGRAM
        CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS CMAKE_POSITION_INDEPENDENT_CODE
        CMAKE_INSTALL_LIBDIR CMAKE_OSX_ARCHITECTURES CMAKE_OSX_DEPLOYMENT_TARGET CMAKE_OSX_SYSROOT
        CMAKE_MSVC_RUNTIME_LIBRARY FETCHCONTENT_SOURCE_DIR_GUI_FORMS_STX_AUXLIB)
    if(NOT _gui_stx_build_type STREQUAL "")
        list(APPEND _gui_stx_settings CMAKE_C_FLAGS_${_gui_stx_build_type} CMAKE_CXX_FLAGS_${_gui_stx_build_type})
    endif()
    # The stamp names the toolchain file by its content, not its path: the
    # native and programs jobs spell the same file differently on Windows.
    set(_gui_stx_stamped_arguments ${_gui_stx_arguments})
    foreach(_gui_stx_setting IN LISTS _gui_stx_settings)
        if(DEFINED ${_gui_stx_setting} AND NOT "${${_gui_stx_setting}}" STREQUAL "")
            list(APPEND _gui_stx_arguments "-D${_gui_stx_setting}=${${_gui_stx_setting}}")
            if(_gui_stx_setting STREQUAL "CMAKE_TOOLCHAIN_FILE")
                gui_forms_text_digest("${CMAKE_TOOLCHAIN_FILE}" _gui_stx_toolchain_digest)
                list(APPEND _gui_stx_stamped_arguments "toolchain=${_gui_stx_toolchain_digest}")
            else()
                list(APPEND _gui_stx_stamped_arguments "-D${_gui_stx_setting}=${${_gui_stx_setting}}")
            endif()
        endif()
    endforeach()
    # The compiler launchers (ccache) change no object, so they are passed but
    # left out of the stamp.
    set(_gui_stx_launchers "")
    foreach(_gui_stx_setting IN ITEMS CMAKE_C_COMPILER_LAUNCHER CMAKE_CXX_COMPILER_LAUNCHER)
        if(DEFINED ${_gui_stx_setting} AND NOT "${${_gui_stx_setting}}" STREQUAL "")
            list(APPEND _gui_stx_launchers "-D${_gui_stx_setting}=${${_gui_stx_setting}}")
        endif()
    endforeach()
    gui_forms_text_digest("${CMAKE_CURRENT_LIST_FILE}" _gui_stx_this_digest)
    gui_forms_text_digest("${_gui_stx_recipe}/CMakeLists.txt" _gui_stx_recipe_digest)
    gui_forms_text_digest("${_gui_stx_root}/cmake/freetype/gui_forms_ftoption.h" _gui_stx_options_digest)
    string(SHA256 _gui_stx_inputs "${_gui_stx_this_digest};${_gui_stx_recipe_digest};${_gui_stx_options_digest};${CMAKE_GENERATOR};${_gui_stx_stamped_arguments}")
    set(_gui_stx_stamp "commit = ${_gui_stx_commit}\n${_gui_stx_choices}inputs = ${_gui_stx_inputs}\n")
    set(_gui_stx_recorded "")
    if(EXISTS "${_gui_stx_package}/${_gui_stx_stamp_name}")
        file(READ "${_gui_stx_package}/${_gui_stx_stamp_name}" _gui_stx_recorded)
    endif()
    if(_gui_stx_overridden OR NOT _gui_stx_recorded STREQUAL _gui_stx_stamp)
        _gui_stx_fetch_source()
        file(REMOVE "${_gui_stx_package}/${_gui_stx_stamp_name}")
        message(STATUS "stx: building the package from ${GUI_FORMS_STX_SOURCE_DIR} into ${_gui_stx_package}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -S "${_gui_stx_recipe}" -B "${_gui_stx_build}" -G "${CMAKE_GENERATOR}"
                    "-DGUI_FORMS_STX_SOURCE_DIR=${GUI_FORMS_STX_SOURCE_DIR}" ${_gui_stx_arguments} ${_gui_stx_launchers}
            RESULT_VARIABLE _gui_stx_result OUTPUT_FILE "${CMAKE_BINARY_DIR}/stx-configure.log"
            ERROR_FILE "${CMAKE_BINARY_DIR}/stx-configure.log")
        if(NOT _gui_stx_result EQUAL 0)
            message(FATAL_ERROR "stx: package configure failed (${CMAKE_BINARY_DIR}/stx-configure.log)")
        endif()
        execute_process(
            COMMAND "${CMAKE_COMMAND}" --build "${_gui_stx_build}"
            RESULT_VARIABLE _gui_stx_result OUTPUT_FILE "${CMAKE_BINARY_DIR}/stx-build.log"
            ERROR_FILE "${CMAKE_BINARY_DIR}/stx-build.log")
        if(NOT _gui_stx_result EQUAL 0)
            message(FATAL_ERROR "stx: package build failed (${CMAKE_BINARY_DIR}/stx-build.log)")
        endif()
        # Each component into a prefix of its own, emptied first, so nothing
        # of an earlier package remains.
        file(REMOVE_RECURSE "${_gui_stx_package}/stx" "${_gui_stx_package}/build")
        foreach(_gui_stx_component IN ITEMS stx build)
            execute_process(
                COMMAND "${CMAKE_COMMAND}" --install "${_gui_stx_build}" --component ${_gui_stx_component}
                        --prefix "${_gui_stx_package}/${_gui_stx_component}"
                RESULT_VARIABLE _gui_stx_result OUTPUT_FILE "${CMAKE_BINARY_DIR}/stx-install.log"
                ERROR_FILE "${CMAKE_BINARY_DIR}/stx-install.log")
            if(NOT _gui_stx_result EQUAL 0)
                message(FATAL_ERROR "stx: package install failed (${CMAKE_BINARY_DIR}/stx-install.log)")
            endif()
        endforeach()
        file(WRITE "${_gui_stx_package}/${_gui_stx_stamp_name}" "${_gui_stx_stamp}")
        # tools/ci_build.py reports whether this configure built the package.
        file(WRITE "${CMAKE_BINARY_DIR}/stx-package-built.txt" "${_gui_stx_package}\n")
    else()
        file(REMOVE "${CMAKE_BINARY_DIR}/stx-package-built.txt")
        message(STATUS "stx: reusing the package at ${_gui_stx_package}")
    endif()
    if(_gui_stx_overridden)
        # A local checkout changes between configures: every build first brings
        # the package up to date (gui_forms_core depends on this target), and
        # the imported archives' new timestamps relink what uses them.
        add_custom_target(gui_forms_stx_refresh
            COMMAND "${CMAKE_COMMAND}" --build "${_gui_stx_build}"
            COMMAND "${CMAKE_COMMAND}" --install "${_gui_stx_build}" --component stx --prefix "${_gui_stx_package}/stx"
            COMMAND "${CMAKE_COMMAND}" --install "${_gui_stx_build}" --component build --prefix "${_gui_stx_package}/build"
            COMMENT "stx: refreshing ${_gui_stx_package} from ${GUI_FORMS_STX_SOURCE_DIR}"
            VERBATIM)
    endif()
endif()
if(GUI_FORMS_BUILD_FUZZERS AND GUI_FORMS_STX_SOURCE_DIR STREQUAL "")
    # The fuzzers compile stx_zlib's and stx_png's sources with their own
    # instrumentation, so they need the source whether or not the package was built.
    _gui_stx_fetch_source()
endif()

# The package root this configure links, which nested generations of this
# source (cmake/ApplicationText.cmake's consumer checks) link as their prefix.
set(GUI_FORMS_STX_PACKAGE_ROOT "${_gui_stx_package}")
set(GUI_FORMS_STX_PREFIX_DIR "${_gui_stx_package}/stx")
set(GUI_FORMS_STX_BUILD_PREFIX_DIR "${_gui_stx_package}/build")
list(PREPEND CMAKE_PREFIX_PATH "${GUI_FORMS_STX_PREFIX_DIR}" "${GUI_FORMS_STX_BUILD_PREFIX_DIR}")
find_package(stx_zlib CONFIG REQUIRED NO_DEFAULT_PATH PATHS "${GUI_FORMS_STX_PREFIX_DIR}")
find_package(stx_png CONFIG REQUIRED NO_DEFAULT_PATH PATHS "${GUI_FORMS_STX_PREFIX_DIR}")
find_package(stx_conv CONFIG REQUIRED NO_DEFAULT_PATH PATHS "${GUI_FORMS_STX_PREFIX_DIR}")
find_package(stx_rtf2md CONFIG REQUIRED NO_DEFAULT_PATH PATHS "${GUI_FORMS_STX_PREFIX_DIR}")
if(_gui_stx_with_vorbis)
    find_package(stx_vorbis CONFIG REQUIRED NO_DEFAULT_PATH PATHS "${GUI_FORMS_STX_PREFIX_DIR}")
endif()
find_package(stx_skia CONFIG REQUIRED NO_DEFAULT_PATH PATHS "${GUI_FORMS_STX_BUILD_PREFIX_DIR}")
find_package(stx_text CONFIG REQUIRED NO_DEFAULT_PATH PATHS "${GUI_FORMS_STX_BUILD_PREFIX_DIR}")
