cmake_minimum_required(VERSION 3.22.1)
project(bedrockclient LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)   # cần mạng lúc build (CI có sẵn)

# ── Dear ImGui (chỉ lõi + backend OpenGL ES3; input tự đưa vào từ hook) ──
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.5)
FetchContent_MakeAvailable(imgui)

# ── Dobby (build tĩnh). Chưa pin tag: hãy ghim commit đã test để build tái lập được. ──
set(DOBBY_GENERATE_SHARED OFF CACHE BOOL "" FORCE)
set(DOBBY_DEBUG OFF CACHE BOOL "" FORCE)
set(Plugin.SymbolResolver ON CACHE BOOL "" FORCE)
FetchContent_Declare(dobby GIT_REPOSITORY https://github.com/jmpews/Dobby.git GIT_TAG master)
FetchContent_MakeAvailable(dobby)

# ── Loader JNI của launcher (memfd + android_dlopen_ext) ──
add_library(launcher_native SHARED launcher_native.cpp)
target_link_libraries(launcher_native log)

# ── libclient.so ──
add_library(client SHARED
    client_hooks.cpp
    game_internals.cpp
    client_ui.cpp
    module_manager.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${dobby_SOURCE_DIR}/include)
target_compile_definitions(client PRIVATE IMGUI_IMPL_OPENGL_ES3 IMGUI_DISABLE_DEMO_WINDOWS IMGUI_DISABLE_DEBUG_TOOLS)
target_link_libraries(client dobby log android EGL GLESv3)

# Android 15+: trang 16 KB
foreach(t launcher_native client)
    target_link_options(${t} PRIVATE -Wl,-z,max-page-size=16384)
endforeach()
