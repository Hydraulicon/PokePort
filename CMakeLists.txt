cmake_minimum_required(VERSION 3.24)

project(PokePort LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Debug;Release;RelWithDebInfo;MinSizeRel" FORCE)
endif()

find_package(Vulkan QUIET)

if(NOT TARGET Vulkan::Vulkan)
  if(NOT DEFINED ENV{VULKAN_SDK} AND WIN32)
    file(GLOB _vulkan_sdks LIST_DIRECTORIES true "C:/VulkanSDK/*")
    list(SORT _vulkan_sdks DESCENDING)
    foreach(_sdk IN LISTS _vulkan_sdks)
      if(EXISTS "${_sdk}/Include/vulkan/vulkan.h")
        set(ENV{VULKAN_SDK} "${_sdk}")
        break()
      endif()
    endforeach()
  endif()

  if(DEFINED ENV{VULKAN_SDK})
    find_path(_vulkan_include_dir
      NAMES vulkan/vulkan.h
      HINTS "$ENV{VULKAN_SDK}/Include"
    )
    find_library(_vulkan_library
      NAMES vulkan-1
      HINTS "$ENV{VULKAN_SDK}/Lib64" "$ENV{VULKAN_SDK}/Lib" "$ENV{VULKAN_SDK}/Lib32"
    )
  endif()

  if(_vulkan_include_dir AND _vulkan_library)
    add_library(Vulkan::Vulkan UNKNOWN IMPORTED)
    set_target_properties(Vulkan::Vulkan PROPERTIES
      IMPORTED_LOCATION "${_vulkan_library}"
      INTERFACE_INCLUDE_DIRECTORIES "${_vulkan_include_dir}"
    )
    message(STATUS "Using Vulkan SDK from ${_vulkan_include_dir}")
  else()
    message(FATAL_ERROR
      "Vulkan SDK not found.\n"
      "  - Install the Vulkan SDK and ensure glslc is on PATH\n"
      "  - Or set the VULKAN_SDK environment variable to the SDK root")
  endif()
else()
  get_target_property(_vk_inc Vulkan::Vulkan INTERFACE_INCLUDE_DIRECTORIES)
  if(_vk_inc)
    message(STATUS "Vulkan include directories: ${_vk_inc}")
  endif()
endif()

option(BUILD_EMERALD_VIEWER "Build the emerald_viewer harness" ON)
option(BUILD_FRAME_VIEWER "Build the frame_viewer sample application" ON)

add_subdirectory(hal)
add_subdirectory(renderer)
add_subdirectory(bridge)
add_subdirectory(extern)

if(BUILD_EMERALD_VIEWER)
  add_subdirectory(apps/emerald_viewer)
endif()

if(BUILD_FRAME_VIEWER)
  add_subdirectory(apps/frame_viewer)
endif()
