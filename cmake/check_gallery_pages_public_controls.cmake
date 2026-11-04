if(NOT DEFINED GALLERY_PAGE_FILES)
    message(FATAL_ERROR "GALLERY_PAGE_FILES is required")
endif()

string(REPLACE "," ";" gallery_page_files "${GALLERY_PAGE_FILES}")

# The Gallery's page builders are a public-library consumer. Any local control
# subclass can hide layout, paint, input, scheduler, or semantic behavior that
# should instead be implemented and tested in GUI.Forms itself. (The form
# document's application types, demo/gallery_parts.hpp and
# demo/gallery_instrument.hpp, are outside this policy.) A handler owner deriving from Component is the documented
# authoring idiom (on(event, owner, &Owner::method)) and is admitted. So is an
# application's DataGridViewCellValueProvider: it supplies a virtual grid's
# values by row ID and is data, not a control.
foreach(gallery_page_file IN LISTS gallery_page_files)
    file(READ "${gallery_page_file}" gallery_page_source)
    string(REGEX REPLACE
        "(class|struct)[ \t]+[A-Za-z_][A-Za-z0-9_]*([ \t]+final)?[ \t\r\n]*:[ \t\r\n]*public[ \t]+(gui_forms::)?(Component|DataGridViewCellValueProvider)[ \t\r\n]*{"
        "" gallery_page_source "${gallery_page_source}")
    if(gallery_page_source MATCHES
       "(^|[\r\n])[ \t]*(class|struct)[ \t]+[A-Za-z_][A-Za-z0-9_]*([ \t]+final)?[ \t\r\n]*:")
        message(FATAL_ERROR
            "Gallery pages contain a local control subclass in ${gallery_page_file}; promote it to GUI.Forms")
    endif()
endforeach()

message(STATUS "Gallery pages use public GUI.Forms controls only")
