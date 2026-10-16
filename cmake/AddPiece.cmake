# add_piece(<name> <sources...>)
# Creates one executable that links the shared core. Each piece lives in its
# own folder under pieces/ and calls this from its CMakeLists.txt.
function(add_piece name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE core)
    set_target_properties(${name} PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    if(EMSCRIPTEN)
        # Emits <name>.html/.js/.wasm using the mobile page in web/shell.html.
        set_target_properties(${name} PROPERTIES SUFFIX ".html")
        target_link_options(${name} PRIVATE
            --shell-file ${CMAKE_SOURCE_DIR}/web/shell.html
            -sUSE_GLFW=3 -sALLOW_MEMORY_GROWTH=1 -sFORCE_FILESYSTEM=1
            -sEXPORTED_RUNTIME_METHODS=HEAPU8,FS)
    endif()
endfunction()
