cmake_minimum_required(VERSION 3.20)
project(ForbiddenMemoriesRecompiled C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(PSXRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/psxrecomp")
set(PSX_BUILD_REV_OVERRIDE "ForbiddenMemories-v0.1.11-spu-volume-sweep")

# The first milestone is a direct game runtime. Launcher, Vulkan and netplay are
# deliberately excluded until the core game is proven.
set(PSX_LAUNCHER OFF CACHE BOOL "" FORCE)
set(PSX_ENABLE_VULKAN OFF CACHE BOOL "" FORCE)
set(PSX_NETPLAY OFF CACHE BOOL "" FORCE)

# Linux exists only for structural builds and bounded diagnostics. The release
# target remains native Windows x64 through MSYS2/MinGW.
if(NOT WIN32)
  set(SDL2_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/local-sdl2/include")
  if(EXISTS "/lib/x86_64-linux-gnu/libSDL2-2.0.so.0")
    set(FM_SDL2_RUNTIME "/lib/x86_64-linux-gnu/libSDL2-2.0.so.0")
  else()
    find_library(FM_SDL2_RUNTIME NAMES SDL2-2.0 SDL2)
  endif()
  if(NOT FM_SDL2_RUNTIME)
    message(FATAL_ERROR "Linux SDL2 runtime library was not found")
  endif()
  set(SDL2_LIBRARIES "${FM_SDL2_RUNTIME}")
endif()

include("${PSXRECOMP_ROOT}/runtime/runtime.cmake")

# The automation Linux image does not provide OpenGL development headers. The
# Windows build retains the real OpenGL backend.
if(NOT WIN32)
  list(REMOVE_ITEM PSXRECOMP_RUNTIME_SOURCES
    "${PSXRECOMP_ROOT}/runtime/src/gpu_gl_renderer.c")
  list(APPEND PSXRECOMP_RUNTIME_SOURCES
    "${PSXRECOMP_ROOT}/runtime/src/gpu_gl_renderer_unavailable.c")
endif()

set(FM_GAME_FULL "${CMAKE_CURRENT_SOURCE_DIR}/generated/SLUS_014.11_full.c")
set(FM_GAME_DISPATCH "${CMAKE_CURRENT_SOURCE_DIR}/generated/SLUS_014.11_dispatch.c")
set(FM_BIOS_FULL "${PSXRECOMP_ROOT}/generated/SCPH1001_full.c")
set(FM_BIOS_DISPATCH "${PSXRECOMP_ROOT}/generated/SCPH1001_dispatch.c")

foreach(required_file
    "${FM_GAME_FULL}"
    "${FM_GAME_DISPATCH}"
    "${FM_BIOS_FULL}"
    "${FM_BIOS_DISPATCH}")
  if(NOT EXISTS "${required_file}")
    message(FATAL_ERROR
      "Missing generated source: ${required_file}\n"
      "Run BUILD_FORBIDDEN_MEMORIES.cmd; it verifies inputs and regenerates the BIOS.")
  endif()
endforeach()

option(FM_FAST_DEV_COMPILE
  "Use low optimization for bounded Linux structural validation" OFF)
if(FM_FAST_DEV_COMPILE AND NOT WIN32)
  set(FM_GAME_OPT "-O0")
else()
  set(FM_GAME_OPT "-O3")
endif()

set_source_files_properties(
  "${FM_GAME_FULL}"
  PROPERTIES COMPILE_OPTIONS
    "$<$<C_COMPILER_ID:MSVC>:/O2>;$<$<NOT:$<C_COMPILER_ID:MSVC>>:${FM_GAME_OPT}>;$<$<NOT:$<C_COMPILER_ID:MSVC>>:-g0>;$<$<NOT:$<C_COMPILER_ID:MSVC>>:-fno-strict-aliasing>;$<$<NOT:$<C_COMPILER_ID:MSVC>>:-fwrapv>")
set_source_files_properties(
  "${FM_BIOS_FULL}"
  PROPERTIES COMPILE_OPTIONS
    "$<$<C_COMPILER_ID:MSVC>:/O1>;$<$<NOT:$<C_COMPILER_ID:MSVC>>:-O1>;$<$<NOT:$<C_COMPILER_ID:MSVC>>:-g0>;$<$<NOT:$<C_COMPILER_ID:MSVC>>:-fno-strict-aliasing>;$<$<NOT:$<C_COMPILER_ID:MSVC>>:-fwrapv>")

if(MINGW)
  set_property(SOURCE "${FM_GAME_FULL}" "${FM_BIOS_FULL}"
    APPEND PROPERTY COMPILE_OPTIONS "-Wa,-mbig-obj")
endif()

if(WIN32)
  set(_fm_disable_audio_default OFF)
else()
  set(_fm_disable_audio_default ON)
endif()
option(FM_DISABLE_AUDIO "Disable SDL audio output" ${_fm_disable_audio_default})

set(FM_EXTRA_SOURCES)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/overlays_static.c")
  list(APPEND FM_EXTRA_SOURCES
       "${CMAKE_CURRENT_SOURCE_DIR}/generated/overlays_static.c")
  message(STATUS "Including statically compiled captured code")
endif()

psxrecomp_add_runtime_target(psx-runtime
  GAME_GENERATED_FULL_C "${FM_GAME_FULL}"
  GAME_GENERATED_DISPATCH_C "${FM_GAME_DISPATCH}"
  EXTRAS_SOURCES ${FM_EXTRA_SOURCES}
  DEBUG_PORT 4511
  WINDOW_TITLE "Yu-Gi-Oh! Forbidden Memories Recompiled"
  EXE_NAME "ForbiddenMemories"
  DEFAULT_BIOS_PATH "bios/SCPH1001.BIN"
  DEFAULT_GAME_CONFIG_PATH "game.toml"
)

if(FM_DISABLE_AUDIO)
  target_compile_definitions(psx-runtime PRIVATE PSX_SDL_NO_AUDIO=1)
endif()
