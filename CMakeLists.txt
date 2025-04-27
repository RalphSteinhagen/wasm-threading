cmake_minimum_required(VERSION 3.10)
project(ImGuiEmscriptenApp)

set(CMAKE_CXX_STANDARD 23)

# ImGui and SDL includes
include_directories(
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/third_party/imgui
  ${CMAKE_SOURCE_DIR}/third_party/imgui/backends
  ${CMAKE_SOURCE_DIR}/third_party/misc)

set(DATA_DIR "${CMAKE_SOURCE_DIR}/assets/audio")
if(EXISTS ${DATA_DIR})
  if(EMSCRIPTEN)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --preload-file ${DATA_DIR}@/assets/audio")
  else()
    file(COPY ${CMAKE_SOURCE_DIR}/assets/audio DESTINATION ${CMAKE_BINARY_DIR}/assets)
  endif()
endif()

file(COPY ${CMAKE_SOURCE_DIR}/assets/css DESTINATION ${CMAKE_BINARY_DIR}/assets)
configure_file(${CMAKE_SOURCE_DIR}/assets/index.html ${CMAKE_BINARY_DIR}/index.html COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/assets/css/favicon.ico ${CMAKE_BINARY_DIR}/favicon.ico COPYONLY)

# Source files
set(SOURCES
    src/main.cpp
    src/background.cpp
    src/audio.cpp
    src/audio_sdl.cpp
    src/file_io.cpp
    third_party/misc/dr_wav.h
    third_party/misc/stb_vorbis.c
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_sdl3.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp)

# Create target
add_executable(ImGuiEmscriptenApp ${SOURCES})

# Emscripten-specific flags only at link time
if(EMSCRIPTEN)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -matomics")
  set(CMAKE_EXE_LINKER_FLAGS
      "${CMAKE_EXE_LINKER_FLAGS} -s USE_PTHREADS=1 -s PTHREAD_POOL_SIZE=4 -s ALLOW_MEMORY_GROWTH=1 -s FULL_ES2=1 -s MAX_WEBGL_VERSION=2 -s MIN_WEBGL_VERSION=2 -sUSE_SDL=3 -sUSE_SDL_MIXER=3"
  )

  target_compile_options(ImGuiEmscriptenApp PRIVATE "-sUSE_SDL=3" "-sUSE_SDL_MIXER=3")

  target_link_options(
    ImGuiEmscriptenApp
    PRIVATE
    "-sUSE_SDL=3"
    "-sUSE_SDL_MIXER=3"
    "-sUSE_PTHREADS=1"
    "-sPTHREAD_POOL_SIZE=4"
    "-sALLOW_TABLE_GROWTH"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sFULL_ES2=1"
    "-sMAX_WEBGL_VERSION=2"
    "-sMIN_WEBGL_VERSION=2"
    "-sEXPORTED_FUNCTIONS=_main,_malloc,_free,_handle_uploaded_files"
    "-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,addFunction,removeFunction,HEAPU8"
    "-sFETCH=1" # needed for file_io
    "--bind" # needed for Clipboard
    "-sNO_DISABLE_EXCEPTION_CATCHING"
    "-lopenal" # for OpenAL audio (soon mandatory: https://emscripten.org/docs/porting/Audio.html)
  )

  # JS support
  set(JS_LIB "${CMAKE_SOURCE_DIR}/js/file_io.js")
  set(JS_DST "${CMAKE_CURRENT_BINARY_DIR}/file_io.js")
  if(EXISTS ${JS_LIB})
    add_custom_command(
      OUTPUT ${JS_DST}
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${JS_LIB} ${JS_DST}
      DEPENDS ${JS_LIB}
      COMMENT "Copying updated JS bindings: file_io.js")
    add_custom_target(copy_js ALL DEPENDS ${JS_DST})
    add_dependencies(ImGuiEmscriptenApp copy_js)

    target_link_options(
      ImGuiEmscriptenApp
      PRIVATE
      "-sUSE_SDL=3"
      "-sUSE_SDL_MIXER=2"
      "-sUSE_PTHREADS=1"
      "-sALLOW_MEMORY_GROWTH=1"
      "--js-library=${JS_LIB}")
    message(STATUS "found JavaScript file: ${JS_LIB}")
  else()
    message(WARNING "could not find JavaScript file: ${JS_LIB}")
  endif()
else()
  find_package(OpenGL REQUIRED)
  find_package(SDL3 REQUIRED)
  find_package(OpenAL REQUIRED)

  target_link_libraries(ImGuiEmscriptenApp PRIVATE SDL3::SDL3 OpenGL::GL OpenAL::OpenAL)
endif()
