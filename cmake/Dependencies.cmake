include(FetchContent)

# --- raylib -----------------------------------------------------------------
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE)

# --- Dear ImGui (no upstream CMake, built by hand below) ---------------------
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9b
    GIT_SHALLOW TRUE)

# --- rlImGui (raylib <-> ImGui glue, no upstream CMake either) ---------------
FetchContent_Declare(rlimgui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG main
    GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(raylib imgui rlimgui)

add_library(imgui_rl STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${rlimgui_SOURCE_DIR}/rlImGui.cpp)
target_include_directories(imgui_rl PUBLIC ${imgui_SOURCE_DIR} ${rlimgui_SOURCE_DIR})
target_link_libraries(imgui_rl PUBLIC raylib)

# --- delaunator (header-only Delaunay triangulation) -------------------------
# Only the header is fetched: the upstream CMake file pulls in mason and test deps.
FetchContent_Declare(delaunator
    GIT_REPOSITORY https://github.com/delfrrr/delaunator-cpp.git
    GIT_TAG v0.4.0
    GIT_SHALLOW TRUE)
cmake_policy(SET CMP0169 OLD) # allow populating without MakeAvailable (see above)
FetchContent_GetProperties(delaunator)
if(NOT delaunator_POPULATED)
    FetchContent_Populate(delaunator)
endif()
add_library(delaunator INTERFACE)
target_include_directories(delaunator INTERFACE ${delaunator_SOURCE_DIR}/include)
