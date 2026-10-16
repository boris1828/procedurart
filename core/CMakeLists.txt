if(EMSCRIPTEN)
    set(CORE_APP_SOURCE src/web_app.cpp) # browser front-end, the page owns the UI
else()
    set(CORE_APP_SOURCE src/app.cpp)
endif()
add_library(core STATIC
    ${CORE_APP_SOURCE}
    src/canvas.cpp
    src/palette.cpp)
target_include_directories(core PUBLIC include)
target_link_libraries(core PUBLIC raylib imgui_rl)
if(MSVC)
    target_compile_options(core PUBLIC /W4 /utf-8)
endif()
