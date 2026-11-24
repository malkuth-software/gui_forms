# The test cache (owner, 2026-10-29 and 2026-11-17: the tests live in
# malkuth-software/gui_forms_tests and build against a prebuilt GUI.Forms).
#
# GUI_FORMS_TEST_CACHE=ON describes every target this tree builds, so that
# tools/test_cache.py can pack the tree as the cache gui_forms_tests links:
# the static and shared libraries, object libraries and programs, each as an
# imported target of the same name with the usage requirements it has here
# (include directories, definitions, options, features and links, read raw,
# so their generator expressions keep working in the consumer). Nothing here
# changes what the tree builds.
#
# Outputs, in <build>/test-cache/ (read by tools/test_cache.py, which turns
# the build and source paths into the cache's own):
# - description.cmake, written at configure time: the source and build
#   directories, the directory-scope compile and link settings, every
#   variable the configure left at directory scope (options and the lists
#   the test CMake reads, such as GUI_FORMS_WINDOWS_DEFINITIONS), and for
#   each target its type and raw usage requirements;
# - locations.cmake, written at generate time: each target's file (or a
#   bundle's directory, or an object library's object files).
#
# Call gui_forms_describe_test_cache() once, at the end of the top-level
# CMakeLists.txt, after the last target is defined.
option(GUI_FORMS_TEST_CACHE "Describe the tree for the gui_forms_tests cache (tools/test_cache.py)" OFF)

# The usage requirements an imported target carries.
set(_gui_forms_test_cache_properties
    INTERFACE_INCLUDE_DIRECTORIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES INTERFACE_COMPILE_DEFINITIONS
    INTERFACE_COMPILE_OPTIONS INTERFACE_COMPILE_FEATURES INTERFACE_LINK_LIBRARIES INTERFACE_LINK_OPTIONS
    INTERFACE_LINK_DIRECTORIES INTERFACE_LINK_DEPENDS INTERFACE_SOURCES INTERFACE_PRECOMPILE_HEADERS
    INTERFACE_POSITION_INDEPENDENT_CODE)

# Appends to `output` the build-system targets of `directory` and of every
# directory below it.
function(_gui_forms_test_cache_targets directory output)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        _gui_forms_test_cache_targets("${subdirectory}" below)
        list(APPEND targets ${below})
    endforeach()
    set(${output} ${targets} PARENT_SCOPE)
endfunction()

# Appends to `output` the imported targets of `directory` and of every
# directory below it.
function(_gui_forms_test_cache_imported directory output)
    get_property(targets DIRECTORY "${directory}" PROPERTY IMPORTED_TARGETS)
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        _gui_forms_test_cache_imported("${subdirectory}" below)
        list(APPEND targets ${below})
    endforeach()
    set(${output} ${targets} PARENT_SCOPE)
endfunction()

# `value` as a CMake bracket argument, so that semicolons, quotes and
# generator expressions survive as written.
function(_gui_forms_test_cache_quote value output)
    set(${output} "[==[${value}]==]" PARENT_SCOPE)
endfunction()

function(gui_forms_describe_test_cache)
    if(NOT GUI_FORMS_TEST_CACHE)
        return()
    endif()
    set(cache_directory "${CMAKE_BINARY_DIR}/test-cache")
    set(description "# Written by cmake/TestCache.cmake; read by tools/test_cache.py.\n")
    string(APPEND description "set(GF_TC_SOURCE_DIR [==[${CMAKE_SOURCE_DIR}]==])\n")
    string(APPEND description "set(GF_TC_BINARY_DIR [==[${CMAKE_BINARY_DIR}]==])\n")
    string(APPEND description "set(GF_TC_VERSION [==[${PROJECT_VERSION}]==])\n")
    string(APPEND description "set(GF_TC_CXX_COMPILER_VERSION [==[${CMAKE_CXX_COMPILER_VERSION}]==])\n")
    string(APPEND description "set(GF_TC_SYSTEM_NAME [==[${CMAKE_SYSTEM_NAME}]==])\n")

    # The directory scope's compile and link settings reach every target the
    # top directory defines, the test programs included.
    foreach(property IN ITEMS COMPILE_OPTIONS COMPILE_DEFINITIONS INCLUDE_DIRECTORIES LINK_OPTIONS
                              LINK_DIRECTORIES)
        get_property(value DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY ${property})
        _gui_forms_test_cache_quote("${value}" quoted)
        string(APPEND description "set(GF_TC_DIRECTORY_${property} ${quoted})\n")
    endforeach()

    # The configure's settings that a consumer compiles with.
    foreach(variable IN ITEMS CMAKE_TOOLCHAIN_FILE CMAKE_BUILD_TYPE CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_OBJCXX_FLAGS
                              CMAKE_C_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELEASE CMAKE_EXE_LINKER_FLAGS
                              CMAKE_SHARED_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS
                              CMAKE_POSITION_INDEPENDENT_CODE CMAKE_INTERPROCEDURAL_OPTIMIZATION
                              CMAKE_OSX_DEPLOYMENT_TARGET CMAKE_OSX_ARCHITECTURES CMAKE_MSVC_RUNTIME_LIBRARY
                              CMAKE_CXX_STANDARD_LIBRARIES CMAKE_OBJCXX_STANDARD_LIBRARIES
                              CMAKE_C_STANDARD_LIBRARIES
                              # Windows: the own runtime the toolchain file reads
                              # (cmake/llvm22-windows-msvc-own.cmake).
                              GUI_FORMS_MSVC_RUNTIME GUI_FORMS_OWN_RUNTIME_SOURCE GUI_FORMS_OWN_RUNTIME_PREFIX
                              GUI_FORMS_LIBCXX_PREFIX GUI_FORMS_LINK_MAP)
        _gui_forms_test_cache_quote("${${variable}}" quoted)
        string(APPEND description "set(GF_TC_SETTING_${variable} ${quoted})\n")
    endforeach()

    # Every variable left at directory scope, but CMake's own, the found
    # packages' and this function's.
    get_cmake_property(variables VARIABLES)
    set(kept_variables "")
    # Those that are also cache entries: a machine's own settings (the own
    # runtime and libc++ prefixes, a toolchain path), which the loader leaves
    # to the consumer's cache when it has the entry.
    set(cache_variables "")
    foreach(variable IN LISTS variables)
        if(variable MATCHES "^(CMAKE_|_CMAKE|ARGV|ARGC|ARGN|PROJECT_|GUIForms_|Python3_|_Python3|Threads_|THREADS_|X11_|PKG_CONFIG|FETCHCONTENT_|FIND_PACKAGE_|__)"
           OR variable MATCHES "_(DIR|FOUND|CONSIDERED_CONFIGS|CONSIDERED_VERSIONS)$"
           OR variable MATCHES "^(description|cache_directory|property|value|quoted|variable|variables|kept_variables|cache_variables)$")
            continue()
        endif()
        _gui_forms_test_cache_quote("${${variable}}" quoted)
        string(APPEND description "set(${variable} ${quoted})\n")
        list(APPEND kept_variables ${variable})
        if(DEFINED CACHE{${variable}})
            list(APPEND cache_variables ${variable})
        endif()
    endforeach()

    _gui_forms_test_cache_targets("${CMAKE_SOURCE_DIR}" all_targets)
    set(cached_targets "")
    # The custom targets (generated headers, the localizable-literal plugin):
    # their outputs are in the cache, and a consumer's add_dependencies on
    # them needs a target of the name.
    set(utility_targets "")
    set(locations "# Written by cmake/TestCache.cmake at generate time.\n")
    foreach(target IN LISTS all_targets)
        get_target_property(type ${target} TYPE)
        get_target_property(imported ${target} IMPORTED)
        if(type STREQUAL "UTILITY" AND NOT imported)
            list(APPEND utility_targets ${target})
            continue()
        endif()
        if(imported OR NOT type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|INTERFACE_LIBRARY|EXECUTABLE)$")
            continue()
        endif()
        # A target left out of `all` is not built for the cache.
        get_target_property(excluded ${target} EXCLUDE_FROM_ALL)
        if(excluded AND NOT type STREQUAL "INTERFACE_LIBRARY")
            continue()
        endif()
        list(APPEND cached_targets ${target})
        string(APPEND description "set(GF_TC_TYPE_${target} ${type})\n")
        foreach(property IN LISTS _gui_forms_test_cache_properties)
            get_target_property(value ${target} ${property})
            if(value)
                _gui_forms_test_cache_quote("${value}" quoted)
                string(APPEND description "set(GF_TC_${property}_${target} ${quoted})\n")
            endif()
        endforeach()
        if(type STREQUAL "EXECUTABLE")
            get_target_property(bundle ${target} MACOSX_BUNDLE)
            if(APPLE AND bundle)
                string(APPEND description "set(GF_TC_BUNDLE_${target} ON)\n")
                string(APPEND locations "set(GF_TC_BUNDLE_DIR_${target} [==[$<TARGET_BUNDLE_DIR:${target}>]==])\n")
            endif()
        endif()
        if(type STREQUAL "OBJECT_LIBRARY")
            string(APPEND locations "set(GF_TC_OBJECTS_${target} [==[$<TARGET_OBJECTS:${target}>]==])\n")
        elseif(NOT type STREQUAL "INTERFACE_LIBRARY")
            string(APPEND locations "set(GF_TC_FILE_${target} [==[$<TARGET_FILE:${target}>]==])\n")
            if(type STREQUAL "SHARED_LIBRARY")
                # The import library (Windows), or the name links and the
                # soname link (libx.dylib, libx.1.dylib) a program loads.
                string(APPEND locations "set(GF_TC_LINKER_FILE_${target} [==[$<TARGET_LINKER_FILE:${target}>]==])\n")
                if(NOT WIN32)
                    string(APPEND locations "set(GF_TC_SONAME_FILE_${target} [==[$<TARGET_SONAME_FILE:${target}>]==])\n")
                endif()
            endif()
        endif()
    endforeach()
    # The imported targets the tree found (PkgConfig::..., X11::..., the
    # system libraries), but those of the stx packages, which the loader
    # finds again: a consumer needs them for the links above. Their paths are
    # the system's, the same on the machine the cache is built for.
    set(foreign_targets "")
    _gui_forms_test_cache_imported("${CMAKE_SOURCE_DIR}" imported_targets)
    foreach(target IN LISTS imported_targets)
        if(target MATCHES "^(stx|stxlib|Threads::)" OR target IN_LIST foreign_targets)
            continue()
        endif()
        get_target_property(type ${target} TYPE)
        if(NOT type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|UNKNOWN_LIBRARY|INTERFACE_LIBRARY|EXECUTABLE)$")
            continue()
        endif()
        list(APPEND foreign_targets ${target})
        string(APPEND description "set(GF_TC_FOREIGN_TYPE_${target} ${type})\n")
        foreach(property IN LISTS _gui_forms_test_cache_properties ITEMS IMPORTED_LOCATION IMPORTED_LOCATION_RELEASE
                         IMPORTED_LOCATION_NOCONFIG IMPORTED_IMPLIB IMPORTED_IMPLIB_RELEASE IMPORTED_CONFIGURATIONS
                         IMPORTED_LINK_INTERFACE_LANGUAGES IMPORTED_SONAME IMPORTED_NO_SONAME)
            get_target_property(value ${target} ${property})
            if(value)
                _gui_forms_test_cache_quote("${value}" quoted)
                string(APPEND description "set(GF_TC_FOREIGN_${property}_${target} ${quoted})\n")
            endif()
        endforeach()
    endforeach()
    string(APPEND description "set(GF_TC_FOREIGN [==[${foreign_targets}]==])\n")
    string(APPEND description "set(GF_TC_TARGETS [==[${cached_targets}]==])\n")
    string(APPEND description "set(GF_TC_UTILITIES [==[${utility_targets}]==])\n")
    string(APPEND description "set(GF_TC_VARIABLES [==[${kept_variables}]==])\n")
    string(APPEND description "set(GF_TC_CACHE_VARIABLES [==[${cache_variables}]==])\n")
    file(WRITE "${cache_directory}/description.cmake" "${description}")
    file(GENERATE OUTPUT "${cache_directory}/locations.cmake" CONTENT "${locations}")
    list(LENGTH cached_targets cached_count)
    message(STATUS "Test cache: ${cached_count} targets described in ${cache_directory} (cmake/TestCache.cmake)")
endfunction()
