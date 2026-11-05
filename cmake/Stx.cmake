# The stx libraries (stx_zlib, stx_png, stx_conv, stx_rtf2md and, for audio
# builds, stx_vorbis) from malkuth-software/stx, consumed as an installed
# prefix through find_package (owner ruling 2026-11-03, decision 3(a)).
#
# Inputs:
# - stx.lock names the stx commit GUI.Forms builds with, in lockstep.
# - FETCHCONTENT_SOURCE_DIR_GUI_FORMS_STX=<dir> replaces the pinned commit with
#   a local stx checkout (lockstep development); it is rebuilt incrementally on
#   every configure, so edits there reach this build.
# - GUI_FORMS_STX_PREFIX=<dir> names a prefix where stx is already installed
#   (a CI cache); nothing is fetched or built, and only the packages are read.
# - GUI_FORMS_BUILD_AUDIO with GUI_FORMS_AUDIO_VORBIS_BACKEND=stx adds stx_vorbis.
#
# Outputs:
# - the imported targets stx_zlib::stx_zlib, stx_png::stx_png,
#   stx_conv::stx_conv, stx_rtf2md::stx_rtf2md (and stx_vorbis's when asked);
# - GUI_FORMS_STX_PREFIX_DIR, the prefix they come from, which the SDK install
#   copies whole (GUIFormsConfig finds the packages beside GUI.Forms);
# - GUI_FORMS_STX_SOURCE_DIR, the stx source (empty with GUI_FORMS_STX_PREFIX),
#   for the targets that read stx sources: the fuzzers and the rtf2md corpus.
#
# Without GUI_FORMS_STX_PREFIX, stx is configured, built and installed at
# configure time into <build>/stx-prefix, with this build's compilers, build
# type, flags and generator, and without its tests. A pinned commit's prefix is
# reused while its stamp (commit, compilers, flags, build type, vorbis) is
# unchanged. A failed stx build stops the configure and leaves no stamp.
include(FetchContent)
get_filename_component(_gui_stx_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(GUI_FORMS_STX_PREFIX "" CACHE PATH "A prefix with stx already installed (empty: build the stx.lock commit)")
set(_gui_stx_with_vorbis OFF)
if(GUI_FORMS_BUILD_AUDIO AND GUI_FORMS_AUDIO_VORBIS_BACKEND STREQUAL "stx")
    set(_gui_stx_with_vorbis ON)
endif()

set(GUI_FORMS_STX_SOURCE_DIR "")
if(NOT GUI_FORMS_STX_PREFIX STREQUAL "")
    set(GUI_FORMS_STX_PREFIX_DIR "${GUI_FORMS_STX_PREFIX}")
else()
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
    FetchContent_Declare(gui_forms_stx
        GIT_REPOSITORY https://github.com/${_gui_stx_repository}.git
        GIT_TAG ${_gui_stx_commit}
        GIT_CONFIG core.autocrlf=false
        SOURCE_SUBDIR gui-forms-no-upstream-build)
    FetchContent_MakeAvailable(gui_forms_stx)
    set(GUI_FORMS_STX_SOURCE_DIR "${gui_forms_stx_SOURCE_DIR}")
    if(NOT EXISTS "${GUI_FORMS_STX_SOURCE_DIR}/CMakeLists.txt" OR
       NOT EXISTS "${GUI_FORMS_STX_SOURCE_DIR}/stx_zlib/CMakeLists.txt")
        message(FATAL_ERROR "the stx source at ${GUI_FORMS_STX_SOURCE_DIR} is not an stx checkout")
    endif()

    set(GUI_FORMS_STX_PREFIX_DIR "${CMAKE_BINARY_DIR}/stx-prefix")
    set(_gui_stx_build "${CMAKE_BINARY_DIR}/stx-build")
    set(_gui_stx_stamp_file "${GUI_FORMS_STX_PREFIX_DIR}/gui-forms-stx.stamp")
    set(_gui_stx_overridden OFF)
    if(DEFINED FETCHCONTENT_SOURCE_DIR_GUI_FORMS_STX AND NOT FETCHCONTENT_SOURCE_DIR_GUI_FORMS_STX STREQUAL "")
        set(_gui_stx_overridden ON)
    endif()
    # The settings stx is built with; each is passed only when this build sets it.
    set(_gui_stx_arguments
        -DSTX_BUILD_TESTS=OFF
        -DSTX_WITH_VORBIS=${_gui_stx_with_vorbis}
        "-DCMAKE_INSTALL_PREFIX=${GUI_FORMS_STX_PREFIX_DIR}"
        "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
        "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
    foreach(_gui_stx_setting IN ITEMS CMAKE_BUILD_TYPE CMAKE_TOOLCHAIN_FILE CMAKE_MAKE_PROGRAM
            CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS CMAKE_C_COMPILER_LAUNCHER
            CMAKE_CXX_COMPILER_LAUNCHER CMAKE_OSX_ARCHITECTURES CMAKE_OSX_DEPLOYMENT_TARGET
            CMAKE_OSX_SYSROOT CMAKE_MSVC_RUNTIME_LIBRARY FETCHCONTENT_SOURCE_DIR_GUI_FORMS_STX_AUXLIB)
        if(DEFINED ${_gui_stx_setting} AND NOT "${${_gui_stx_setting}}" STREQUAL "")
            list(APPEND _gui_stx_arguments "-D${_gui_stx_setting}=${${_gui_stx_setting}}")
        endif()
    endforeach()
    string(SHA256 _gui_stx_stamp "${_gui_stx_commit};${_gui_stx_overridden};${CMAKE_GENERATOR};${_gui_stx_arguments}")
    set(_gui_stx_recorded "")
    if(EXISTS "${_gui_stx_stamp_file}")
        file(READ "${_gui_stx_stamp_file}" _gui_stx_recorded)
    endif()
    if(_gui_stx_overridden OR NOT _gui_stx_recorded STREQUAL _gui_stx_stamp)
        file(REMOVE "${_gui_stx_stamp_file}")
        message(STATUS "stx: building ${GUI_FORMS_STX_SOURCE_DIR} into ${GUI_FORMS_STX_PREFIX_DIR}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -S "${GUI_FORMS_STX_SOURCE_DIR}" -B "${_gui_stx_build}"
                    -G "${CMAKE_GENERATOR}" ${_gui_stx_arguments}
            RESULT_VARIABLE _gui_stx_result OUTPUT_FILE "${CMAKE_BINARY_DIR}/stx-configure.log"
            ERROR_FILE "${CMAKE_BINARY_DIR}/stx-configure.log")
        if(NOT _gui_stx_result EQUAL 0)
            message(FATAL_ERROR "stx: configure failed (${CMAKE_BINARY_DIR}/stx-configure.log)")
        endif()
        execute_process(
            COMMAND "${CMAKE_COMMAND}" --build "${_gui_stx_build}" --target install
            RESULT_VARIABLE _gui_stx_result OUTPUT_FILE "${CMAKE_BINARY_DIR}/stx-build.log"
            ERROR_FILE "${CMAKE_BINARY_DIR}/stx-build.log")
        if(NOT _gui_stx_result EQUAL 0)
            message(FATAL_ERROR "stx: build or install failed (${CMAKE_BINARY_DIR}/stx-build.log)")
        endif()
        file(WRITE "${_gui_stx_stamp_file}" "${_gui_stx_stamp}")
    endif()
    if(_gui_stx_overridden)
        # A local checkout changes between configures: every build first brings
        # its prefix up to date (gui_forms_core depends on this target), and the
        # imported archives' new timestamps relink what uses them.
        add_custom_target(gui_forms_stx_refresh
            COMMAND "${CMAKE_COMMAND}" --build "${_gui_stx_build}" --target install
            COMMENT "stx: refreshing ${GUI_FORMS_STX_PREFIX_DIR} from ${GUI_FORMS_STX_SOURCE_DIR}"
            VERBATIM)
    endif()
endif()

list(PREPEND CMAKE_PREFIX_PATH "${GUI_FORMS_STX_PREFIX_DIR}")
find_package(stx_zlib CONFIG REQUIRED NO_DEFAULT_PATH PATHS "${GUI_FORMS_STX_PREFIX_DIR}")
find_package(stx_png CONFIG REQUIRED NO_DEFAULT_PATH PATHS "${GUI_FORMS_STX_PREFIX_DIR}")
find_package(stx_conv CONFIG REQUIRED NO_DEFAULT_PATH PATHS "${GUI_FORMS_STX_PREFIX_DIR}")
find_package(stx_rtf2md CONFIG REQUIRED NO_DEFAULT_PATH PATHS "${GUI_FORMS_STX_PREFIX_DIR}")
if(_gui_stx_with_vorbis)
    find_package(stx_vorbis CONFIG REQUIRED NO_DEFAULT_PATH PATHS "${GUI_FORMS_STX_PREFIX_DIR}")
endif()
