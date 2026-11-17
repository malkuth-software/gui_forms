# A program's font folder (a macOS bundle's Contents/Resources/fonts): the licences of the faces
# the embedded pack holds and the attribution, and no face file, since every face is read from
# the pack (font pack stage 4). Usage: cmake -DFONT_DIRECTORY=<dir> -P check_gallery_fonts.cmake
if(NOT IS_DIRECTORY "${FONT_DIRECTORY}")
    message(FATAL_ERROR "Program font licence directory is missing: ${FONT_DIRECTORY}")
endif()

file(GLOB face_files LIST_DIRECTORIES false "${FONT_DIRECTORY}/*.ttf" "${FONT_DIRECTORY}/*.otf")
if(face_files)
    message(FATAL_ERROR "A program carries face files the embedded pack already holds: ${face_files}")
endif()

# The notices that must be present and must not drift: the attribution and the licences of the
# primary faces, then the two Noto texts with their digests when the fallback faces are in.
foreach(filename IN ITEMS
        ATTRIBUTION.md LICENSE-PortsmouthRapids.txt OFL-Arimo.txt OFL-Carlito.txt
        OFL-Cousine.txt OFL-Selawik.txt OFL-Tinos.txt)
    if(NOT EXISTS "${FONT_DIRECTORY}/${filename}")
        message(FATAL_ERROR "Program font licence is missing: ${filename}")
    endif()
endforeach()
foreach(entry IN ITEMS
        "OFL-NotoSansCJKjp.txt|6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2"
        "OFL-NotoEmoji.txt|6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2")
    string(REPLACE "|" ";" fields "${entry}")
    list(GET fields 0 filename)
    list(GET fields 1 expected_hash)
    set(path "${FONT_DIRECTORY}/${filename}")
    if(EXISTS "${path}")
        file(SHA256 "${path}" actual_hash)
        if(NOT actual_hash STREQUAL expected_hash)
            message(FATAL_ERROR "Program font licence changed for ${filename}: ${actual_hash}")
        endif()
    endif()
endforeach()
