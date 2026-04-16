cmake_minimum_required(VERSION 3.20)
project(OpenGLStarter LANGUAGES CXX)

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

# Keep GLFW build minimal.
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw)
FetchContent_MakeAvailable(nlohmann_json)

find_package(OpenGL REQUIRED)

add_executable(opengl_starter
  src/main.cpp
)

target_compile_definitions(opengl_starter
  PRIVATE
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
    glfw
    nlohmann_json::nlohmann_json
    OpenGL::GL
)

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
