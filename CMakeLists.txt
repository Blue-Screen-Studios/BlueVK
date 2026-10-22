cmake_minimum_required(VERSION 3.21)
project(bluevk VERSION 0.1.0 LANGUAGES CXX)

# BlueVK: a Vulkan device, its swapchain and the small helpers a renderer
# builds on, in C++20. The game's own passes and targets stay in the game: it
# derives its context from bvk::Ctx. Built alone (its demo, its tests) or
# inside a game (add_subdirectory / FetchContent: the library).
#
# What it links: the Vulkan loader (Vulkan::Vulkan: the game's if it made
# one, else find_package) and SDL3 (the window's surface; the game's target
# in BLUEVK_SDL_TARGET, else found).
set(BLUEVK_TOP_LEVEL ${PROJECT_IS_TOP_LEVEL})
option(BLUEVK_BUILD_DEMO "The demo (SDL3: a window, cleared and presented)" ${BLUEVK_TOP_LEVEL})
option(BLUEVK_BUILD_TESTS "The tests (ctest; skipped without a GPU)" ${BLUEVK_TOP_LEVEL})
set(BLUEVK_SDL_TARGET "" CACHE STRING "The SDL3 target to link (empty: found)")

if(BLUEVK_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(NOT TARGET Vulkan::Vulkan)
  find_package(Vulkan QUIET)
  if(NOT Vulkan_FOUND)
    # No Vulkan SDK: the headers fetched (FloodSim's and Hexoid's pin), the
    # system's loader.
    include(FetchContent)
    FetchContent_Declare(
      bluevk_vulkan_headers
      URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.4.357.tar.gz
      URL_HASH SHA256=7dc0dbcf1d49dd3d7da3761c251c6097dfbaac475321a4a8a99269d3d5abecdc
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(bluevk_vulkan_headers)
    set(Vulkan_INCLUDE_DIR "${bluevk_vulkan_headers_SOURCE_DIR}/include" CACHE FILEPATH "" FORCE)
    find_package(Vulkan REQUIRED)
  endif()
endif()
if(NOT BLUEVK_SDL_TARGET)
  find_package(SDL3 CONFIG QUIET)
  if(TARGET SDL3::SDL3)
    set(BLUEVK_SDL_TARGET SDL3::SDL3)
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(BLUEVK_SDL3 REQUIRED IMPORTED_TARGET sdl3)
    set(BLUEVK_SDL_TARGET PkgConfig::BLUEVK_SDL3)
  endif()
endif()

# ------------------------------------------------------------ the library
add_library(bluevk STATIC src/context.cpp src/util.cpp src/vma.cpp)
target_include_directories(bluevk PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# (vk_mem_alloc.h: third-party, its warnings not the game's.)
target_include_directories(bluevk SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
target_compile_features(bluevk PUBLIC cxx_std_20)
set_target_properties(bluevk PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(bluevk PUBLIC Vulkan::Vulkan PRIVATE ${BLUEVK_SDL_TARGET})
# (GCC and Clang's warnings; clang-cl, MSVC's flags, has its own.)
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
  set(BLUEVK_WARNINGS -Wall -Wextra)
  set_source_files_properties(src/vma.cpp PROPERTIES COMPILE_OPTIONS "-w")
elseif(MSVC)
  set(BLUEVK_WARNINGS /W3)
  set_source_files_properties(src/vma.cpp PROPERTIES COMPILE_OPTIONS "/w")
endif()
target_compile_options(bluevk PRIVATE ${BLUEVK_WARNINGS})

# ------------------------------------------------------------ the demo, the tests
if(BLUEVK_BUILD_DEMO)
  add_executable(bluevk_demo demo/main.cpp)
  target_link_libraries(bluevk_demo PRIVATE bluevk ${BLUEVK_SDL_TARGET})
  target_compile_options(bluevk_demo PRIVATE ${BLUEVK_WARNINGS})
endif()

if(BLUEVK_BUILD_TESTS)
  enable_testing()
  add_executable(bluevk_tests tests/tests.cpp)
  target_link_libraries(bluevk_tests PRIVATE bluevk)
  target_compile_options(bluevk_tests PRIVATE ${BLUEVK_WARNINGS})
  add_test(NAME bluevk_tests COMMAND bluevk_tests)
  set_tests_properties(bluevk_tests PROPERTIES SKIP_RETURN_CODE 77)
endif()
