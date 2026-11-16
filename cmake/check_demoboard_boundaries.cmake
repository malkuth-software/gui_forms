file(GLOB_RECURSE demoboard_sources
    "${DEMOBOARD_DIRECTORY}/src/*.cpp"
    "${DEMOBOARD_DIRECTORY}/src/*.mm"
    "${DEMOBOARD_DIRECTORY}/include/*.hpp")

foreach(source IN LISTS demoboard_sources)
    file(READ "${source}" content)
    if(content MATCHES "src/(core|controls|host|render|surface)/" OR
       content MATCHES "#include[ \t]*[<\"](Sk|AppKit|windows\\.h|d2d|gtk)")
        message(FATAL_ERROR "Demoboard reaches through the public GUI.Forms boundary: ${source}")
    endif()
    if(content MATCHES "class[ \t\r\n]+[A-Za-z_][A-Za-z0-9_]*[ \t\r\n]*:[ \t\r\n]*(public|protected|private)[ \t\r\n]+(gui_forms::)?[A-Za-z_][A-Za-z0-9_]*")
        message(FATAL_ERROR "Demoboard contains a local control subclass: ${source}")
    endif()
endforeach()
