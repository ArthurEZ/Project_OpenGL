cmake_minimum_required(VERSION 3.20)
project(OpenGLStarter LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

FetchContent_Declare(
  glfw
  URL https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz
)
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

if(APPLE)
    # Force CMake to use Homebrew's Assimp on Mac M-series chips
    set(ASSIMP_INCLUDE_DIR "/opt/homebrew/include")
    set(ASSIMP_LIBRARY "/opt/homebrew/lib/libassimp.dylib")
    include_directories(${ASSIMP_INCLUDE_DIR})
    message(STATUS "FORCING Homebrew ASSIMP from ${ASSIMP_INCLUDE_DIR}")
else()
    find_package(ASSIMP REQUIRED)
    set(ASSIMP_LIBRARY ${ASSIMP_LIBRARIES})
    include_directories(${ASSIMP_INCLUDE_DIRS})
    message(STATUS "Found ASSIMP in ${ASSIMP_INCLUDE_DIR}")
endif()

FetchContent_MakeAvailable(glfw)
FetchContent_MakeAvailable(nlohmann_json)

find_package(OpenGL REQUIRED)

# ── GLAD (compile from your copied includes/glad) ────────────────────────────
add_library(GLAD STATIC src/glad.c)
target_include_directories(GLAD PUBLIC ${CMAKE_SOURCE_DIR}/includes)

add_executable(opengl_starter
    src/main.cpp
    src/math.cpp
    src/assets.cpp
    src/graphics.cpp
    src/game.cpp
)

# ── Point to your copied includes/ folder ────────────────────────────────────
target_include_directories(opengl_starter PRIVATE
    ${CMAKE_SOURCE_DIR}/includes
)

target_compile_definitions(opengl_starter PRIVATE
    APP_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

if(EXISTS "${CMAKE_SOURCE_DIR}/resources")
    add_custom_command(TARGET opengl_starter POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:opengl_starter>/resources"
    )
endif()

target_link_libraries(opengl_starter
    PRIVATE
    GLAD
    glfw
    nlohmann_json::nlohmann_json
    ${ASSIMP_LIBRARY}
    OpenGL::GL
)

# ── macOS frameworks ──────────────────────────────────────────────────────────
if(APPLE)
    target_link_libraries(opengl_starter PRIVATE
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
    )
endif()

if(WIN32)
    target_link_libraries(opengl_starter PRIVATE gdiplus)
endif()

if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(MINGW_RUNTIME_DLLS
        libstdc++-6.dll
        libgcc_s_seh-1.dll
        libwinpthread-1.dll
    )
    foreach(RUNTIME_DLL IN LISTS MINGW_RUNTIME_DLLS)
        if(EXISTS "${MINGW_BIN_DIR}/${RUNTIME_DLL}")
            add_custom_command(TARGET opengl_starter POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/${RUNTIME_DLL}"
                "$<TARGET_FILE_DIR:opengl_starter>"
            )
        endif()
    endforeach()
endif()
