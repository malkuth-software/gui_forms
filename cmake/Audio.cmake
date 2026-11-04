include(FetchContent)
include(GNUInstallDirs)
find_package(Threads REQUIRED)
get_filename_component(_gui_audio_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
# A source SDK archive (tools/package_sdk.py) carries the pinned miniaudio tree
# at third_party/miniaudio, so the archive configures without fetching it; the
# header and decoder digests below still apply. An explicit
# FETCHCONTENT_SOURCE_DIR_GUI_FORMS_MINIAUDIO keeps precedence.
if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_GUI_FORMS_MINIAUDIO AND
   EXISTS "${_gui_audio_root}/third_party/miniaudio/miniaudio.h")
    set(FETCHCONTENT_SOURCE_DIR_GUI_FORMS_MINIAUDIO "${_gui_audio_root}/third_party/miniaudio")
endif()
FetchContent_Declare(gui_forms_miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG f40cf03f80cdb7e741d43e53b7e706e8c1394bcf
    GIT_CONFIG core.autocrlf=false
    SOURCE_SUBDIR gui-forms-no-upstream-build)
FetchContent_MakeAvailable(gui_forms_miniaudio)
file(READ "${gui_forms_miniaudio_SOURCE_DIR}/miniaudio.h" _gui_audio_header)
string(REPLACE "\r\n" "\n" _gui_audio_header "${_gui_audio_header}")
string(SHA256 _gui_audio_hash "${_gui_audio_header}")
unset(_gui_audio_header)
if(NOT _gui_audio_hash STREQUAL "7e4f3f13c8fe66df2080ac3dd12a89193e3c2463cb7f067c798abd7331cd8ee6")
    message(FATAL_ERROR "The pinned miniaudio header hash differs")
endif()
if(GUI_FORMS_AUDIO_VORBIS_BACKEND STREQUAL "stb")
    file(READ "${gui_forms_miniaudio_SOURCE_DIR}/extras/stb_vorbis.c" _gui_vorbis_source)
    string(REPLACE "\r\n" "\n" _gui_vorbis_source "${_gui_vorbis_source}")
    string(SHA256 _gui_vorbis_hash "${_gui_vorbis_source}")
    if(NOT _gui_vorbis_hash STREQUAL "4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636")
        message(FATAL_ERROR "The pinned stb_vorbis source hash differs")
    endif()
    # Preserve the pinned upstream tree. Apply this single bounds correction to a
    # generated include projection: validate the integer offset before forming a
    # pointer. The upstream wraparound comparison itself invokes undefined behavior.
    string(REPLACE
        "f->stream_start + loc >= f->stream_end || f->stream_start + loc < f->stream_start"
        "(size_t) loc >= (size_t) (f->stream_end - f->stream_start)"
        _gui_vorbis_patched "${_gui_vorbis_source}")
    if(_gui_vorbis_patched STREQUAL _gui_vorbis_source)
        message(FATAL_ERROR "The pinned Vorbis bounds patch did not apply")
    endif()
    set(_gui_audio_projection "${CMAKE_CURRENT_BINARY_DIR}/third_party/audio")
    file(MAKE_DIRECTORY "${_gui_audio_projection}/extras")
    file(CONFIGURE OUTPUT "${_gui_audio_projection}/extras/stb_vorbis.c"
        CONTENT "${_gui_vorbis_patched}" @ONLY NEWLINE_STYLE UNIX)
    unset(_gui_vorbis_source)
    unset(_gui_vorbis_patched)
elseif(GUI_FORMS_AUDIO_VORBIS_BACKEND STREQUAL "stx")
    # Keep standalone codec test/oracle/fuzz settings out of the audio build.
    # stx_vorbis::stx_vorbis_stb comes from the stx prefix (cmake/Stx.cmake).
    if(NOT TARGET stx_vorbis::stx_vorbis_stb)
        message(FATAL_ERROR "the stx prefix has no stx_vorbis (GUI_FORMS_AUDIO_VORBIS_BACKEND=stx)")
    endif()
else()
    message(FATAL_ERROR "GUI_FORMS_AUDIO_VORBIS_BACKEND must be stb or stx")
endif()
add_library(gui_forms_audio STATIC "${_gui_audio_root}/src/audio/audio.cpp")
option(GUI_FORMS_BUILD_AUDIO_LOOP_TRANSPORT "Build the development audio loop transport" OFF)
if(GUI_FORMS_BUILD_AUDIO_LOOP_TRANSPORT)
    target_sources(gui_forms_audio PRIVATE
        "${_gui_audio_root}/src/audio/loop_transport/loop_transport.cpp"
        "${_gui_audio_root}/src/audio/loop_transport/scheduler.cpp")
    # audio.hpp declares the loop transport only under this definition, and the
    # installed library holds it, so installed consumers must see it too.
    target_compile_definitions(gui_forms_audio PUBLIC GUI_FORMS_AUDIO_LOOP_TRANSPORT)
endif()
add_library(GUIForms::Audio ALIAS gui_forms_audio)
set_target_properties(gui_forms_audio PROPERTIES EXPORT_NAME Audio)
target_compile_features(gui_forms_audio PUBLIC cxx_std_20)
target_include_directories(gui_forms_audio PUBLIC
    $<BUILD_INTERFACE:${_gui_audio_root}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE "${_gui_audio_projection}" "${gui_forms_miniaudio_SOURCE_DIR}")
target_compile_definitions(gui_forms_audio PRIVATE MA_NO_DECODING MA_NO_ENCODING
    MA_NO_RESOURCE_MANAGER MA_NO_GENERATION MA_ENABLE_ONLY_SPECIFIC_BACKENDS)
target_link_libraries(gui_forms_audio PUBLIC GUIForms::Threading PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
if(GUI_FORMS_AUDIO_VORBIS_BACKEND STREQUAL "stx")
    target_compile_definitions(gui_forms_audio PRIVATE GUI_FORMS_AUDIO_VORBIS_STX)
    target_link_libraries(gui_forms_audio PRIVATE stx_vorbis::stx_vorbis_stb)
endif()
if(WIN32)
    target_compile_definitions(gui_forms_audio PRIVATE MA_ENABLE_WASAPI)
    target_link_libraries(gui_forms_audio PRIVATE ole32)
elseif(APPLE)
    target_compile_definitions(gui_forms_audio PRIVATE MA_ENABLE_COREAUDIO)
    target_link_libraries(gui_forms_audio PRIVATE "-framework CoreAudio" "-framework AudioToolbox" "-framework CoreFoundation")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(gui_forms_audio PRIVATE MA_ENABLE_ALSA)
else()
    message(FATAL_ERROR "GUI.Forms audio device backend is unavailable on this platform")
endif()
if(GUI_FORMS_BUILD_TESTS)
    target_compile_definitions(gui_forms_audio PRIVATE GUI_FORMS_AUDIO_TESTING)
    add_executable(gui_forms_audio_tests "${_gui_audio_root}/tests/audio/audio_service_tests.cpp")
    target_compile_definitions(gui_forms_audio_tests PRIVATE GUI_FORMS_AUDIO_TESTING)
    target_link_libraries(gui_forms_audio_tests PRIVATE GUIForms::Audio)
    if(GUI_FORMS_AUDIO_VORBIS_BACKEND STREQUAL "stx")
        target_compile_definitions(gui_forms_audio_tests PRIVATE GUI_FORMS_AUDIO_VORBIS_STX)
    endif()
    add_test(NAME gui_forms_audio_tests COMMAND gui_forms_audio_tests)
    if(GUI_FORMS_BUILD_AUDIO_LOOP_TRANSPORT)
        add_executable(gui_forms_audio_loop_transport_tests
            "${_gui_audio_root}/tests/audio/audio_loop_transport_tests.cpp")
        target_compile_definitions(gui_forms_audio_loop_transport_tests PRIVATE GUI_FORMS_AUDIO_TESTING)
        target_link_libraries(gui_forms_audio_loop_transport_tests PRIVATE GUIForms::Audio)
        add_test(NAME gui_forms_audio_loop_transport_tests COMMAND gui_forms_audio_loop_transport_tests)
        set_tests_properties(gui_forms_audio_loop_transport_tests PROPERTIES TIMEOUT 30)
    endif()
endif()
install(TARGETS gui_forms_audio EXPORT GUIFormsAudioTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
if(GUI_FORMS_BUILD_AUDIO_LOOP_TRANSPORT)
    # The component a consumer names to require the loop transport: Audio built
    # with it. It adds nothing to Audio's own usage requirements.
    add_library(gui_forms_audio_loop_transport INTERFACE)
    target_link_libraries(gui_forms_audio_loop_transport INTERFACE gui_forms_audio)
    set_target_properties(gui_forms_audio_loop_transport PROPERTIES EXPORT_NAME AudioLoopTransport)
    add_library(GUIForms::AudioLoopTransport ALIAS gui_forms_audio_loop_transport)
    install(TARGETS gui_forms_audio_loop_transport EXPORT GUIFormsAudioTargets)
    install(DIRECTORY "${_gui_audio_root}/include/gui_forms/audio/loop_transport"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gui_forms/audio)
endif()
install(EXPORT GUIFormsAudioTargets NAMESPACE GUIForms:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GUIForms)
install(FILES "${_gui_audio_root}/include/gui_forms/audio/audio.hpp" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gui_forms/audio)
install(FILES "${gui_forms_miniaudio_SOURCE_DIR}/LICENSE" DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME miniaudio-LICENSE.txt)
if(GUI_FORMS_AUDIO_VORBIS_BACKEND STREQUAL "stb")
    # Preserve the full upstream source and its Alternative A MIT notice verbatim.
    install(FILES "${gui_forms_miniaudio_SOURCE_DIR}/extras/stb_vorbis.c" DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms)
else()
    install(FILES "${GUI_FORMS_STX_PREFIX_DIR}/share/licenses/stx_vorbis/LICENSE"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME stx_vorbis-LICENSE.txt)
    # stx_vorbis compiles BFFT's BODFT sources (its third_party/bfft).
    install(FILES "${GUI_FORMS_STX_PREFIX_DIR}/share/licenses/stx_vorbis/BFFT-LICENSE.txt"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME bfft-LICENSE.txt)
endif()
