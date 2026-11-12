# stx_auxlib's stx_btree headers, which the controls' row height index
# (src/controls/collections/row_height_index.hpp) adapts: the header-only
# sequence tree and RowHeightIndex (btree/include/stx_btree/). Nothing of
# stx_auxlib is compiled into a library here, and stx_auxlib's own CMake
# build is not run.
#
# Inputs:
# - stx_auxlib.lock names the commit, fetched from GitHub.
# - FETCHCONTENT_SOURCE_DIR_GUI_FORMS_STX_AUXLIB=<dir> replaces it with a
#   local checkout (lockstep development; cmake/Stx.cmake passes the same
#   variable to the stx package).
#
# Outputs:
# - GUI_FORMS_STX_BTREE_INCLUDE_DIR, the stx_btree include directory, which
#   the targets that compile the adapter add as a private system directory;
# - GUI_FORMS_STX_AUXLIB_SOURCE_DIR (cached), the stx_auxlib source, whose
#   btree/LICENSE and btree/NOTICE the SDK and the programs ship
#   (tools/package_programs.py reads the cached value).
#
# A lock without a repository and a 40-character commit, or a source without
# the stx_btree headers, stops the configure.
include(FetchContent)
get_filename_component(_gui_stx_auxlib_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
file(STRINGS "${_gui_stx_auxlib_root}/stx_auxlib.lock" _gui_stx_auxlib_lock
     REGEX "^(repository|commit) = ")
set(_gui_stx_auxlib_repository "")
set(_gui_stx_auxlib_commit "")
foreach(_gui_stx_auxlib_line IN LISTS _gui_stx_auxlib_lock)
    if(_gui_stx_auxlib_line MATCHES "^repository = \"([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+)\"$")
        set(_gui_stx_auxlib_repository "${CMAKE_MATCH_1}")
    elseif(_gui_stx_auxlib_line MATCHES "^commit = \"([0-9a-f]+)\"$")
        set(_gui_stx_auxlib_commit "${CMAKE_MATCH_1}")
    endif()
endforeach()
string(LENGTH "${_gui_stx_auxlib_commit}" _gui_stx_auxlib_commit_length)
if(_gui_stx_auxlib_repository STREQUAL "" OR NOT _gui_stx_auxlib_commit_length EQUAL 40)
    message(FATAL_ERROR "stx_auxlib.lock needs repository = \"owner/name\" and a 40-character commit")
endif()
FetchContent_Declare(gui_forms_stx_auxlib
    GIT_REPOSITORY https://github.com/${_gui_stx_auxlib_repository}.git
    GIT_TAG ${_gui_stx_auxlib_commit}
    GIT_CONFIG core.autocrlf=false
    SOURCE_SUBDIR gui-forms-no-upstream-build)
FetchContent_MakeAvailable(gui_forms_stx_auxlib)
set(GUI_FORMS_STX_AUXLIB_SOURCE_DIR "${gui_forms_stx_auxlib_SOURCE_DIR}" CACHE INTERNAL
    "The stx_auxlib source the build takes stx_btree's headers from")
set(GUI_FORMS_STX_BTREE_INCLUDE_DIR "${GUI_FORMS_STX_AUXLIB_SOURCE_DIR}/btree/include")
if(NOT EXISTS "${GUI_FORMS_STX_BTREE_INCLUDE_DIR}/stx_btree/row_height_index.hpp" OR
   NOT EXISTS "${GUI_FORMS_STX_BTREE_INCLUDE_DIR}/stx_btree/sequence_tree.hpp")
    message(FATAL_ERROR "the stx_auxlib source at ${GUI_FORMS_STX_AUXLIB_SOURCE_DIR} has no stx_btree "
                        "sequence tree (btree/include/stx_btree/row_height_index.hpp, sequence_tree.hpp)")
endif()
