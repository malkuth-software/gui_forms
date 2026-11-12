# The digest of a text file as its content, whatever line endings the
# checkout gave it: carriage returns are removed before hashing, so a CRLF
# working tree (Windows, autocrlf) and an LF one (CI, macOS, Linux) agree,
# and an LF file's digest is its plain SHA-256. Stx.cmake stamps the stx
# package with these digests; a raw byte hash made two checkouts of the
# same commit disagree and rebuilt the package (2026-11-10).
function(gui_forms_text_digest path out_variable)
    file(READ "${path}" _gui_text_digest_content)
    string(REPLACE "\r" "" _gui_text_digest_content "${_gui_text_digest_content}")
    string(SHA256 _gui_text_digest "${_gui_text_digest_content}")
    set(${out_variable} "${_gui_text_digest}" PARENT_SCOPE)
endfunction()
