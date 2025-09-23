cmake_minimum_required(VERSION 3.22)
project(pokework LANGUAGES C CXX)

# --- Global settings ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Debug;Release;RelWithDebInfo;MinSizeRel" FORCE)
endif()

# --- Vulkan bootstrap (Windows-friendly & robust) ---

# Try the standard module first
find_package(Vulkan QUIET)

# Help CMake with hints if needed
if(NOT Vulkan_FOUND)
  # If your SDK env var is set, add it to CMake's search paths
  if(DEFINED ENV{VULKAN_SDK})
    list(PREPEND CMAKE_PREFIX_PATH "$ENV{VULKAN_SDK}")
  endif()
  find_package(Vulkan QUIET)
endif()

# Manual fallback: build an imported target from the SDK layout
if(NOT TARGET Vulkan::Vulkan)
  # Try to infer the SDK root if the env var isn't set
  if(NOT DEFINED ENV{VULKAN_SDK} AND WIN32)
    file(GLOB _vk_roots LIST_DIRECTORIES true "C:/VulkanSDK/*")
    list(SORT _vk_roots DESCENDING)
    foreach(_root IN LISTS _vk_roots)
      if(EXISTS "${_root}/Include/vulkan/vulkan.h")
        set(ENV{VULKAN_SDK} "${_root}")
        break()
      endif()
    endforeach()
  endif()

  # Find header + lib manually
  find_path(Vulkan_INCLUDE_DIR
    NAMES vulkan/vulkan.h
    HINTS "$ENV{VULKAN_SDK}/Include"
  )
  # Prefer 64-bit lib dir on Ninja x64 / MSVC x64
  find_library(Vulkan_LIBRARY
    NAMES vulkan-1
    HINTS "$ENV{VULKAN_SDK}/Lib" "$ENV{VULKAN_SDK}/Lib32"
  )

  if(Vulkan_INCLUDE_DIR AND Vulkan_LIBRARY)
    add_library(Vulkan::Vulkan UNKNOWN IMPORTED)
    set_target_properties(Vulkan::Vulkan PROPERTIES
      IMPORTED_LOCATION "${Vulkan_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${Vulkan_INCLUDE_DIR}"
    )
    message(STATUS "Vulkan (manual): include=${Vulkan_INCLUDE_DIR}, lib=${Vulkan_LIBRARY}")
  else()
    message(FATAL_ERROR "Vulkan SDK not found.\n  - Ensure VULKAN_SDK is set (e.g., C:/VulkanSDK/1.4.321.1)\n  - Or install the Vulkan SDK / repair PATH.")
  endif()
else()
  # Optional: print what we found for sanity
  get_target_property(_vk_inc Vulkan::Vulkan INTERFACE_INCLUDE_DIRECTORIES)
  message(STATUS "Vulkan found. Include dirs: ${_vk_inc}")
endif()


option(BUILD_APPS "Build host apps (frame_viewer)" ON)

# NOTE: pokeemerald/ is present but not built by CMake (it has its own Makefile/devkitARM flow).
# We'll integrate it later via a PC shim.

add_subdirectory(renderer)
add_subdirectory(bridge)
add_subdirectory(extern)        # provides 'pokeemerald_headers'
add_executable(emerald_viewer apps/emerald_viewer/main.cpp)
target_link_libraries(emerald_viewer PRIVATE agb_vk agb_bridge gba_hal pokeemerald_headers)
target_include_directories(emerald_viewer PRIVATE ${CMAKE_SOURCE_DIR}/hal)
target_link_libraries(agb_bridge PRIVATE gba_hal)         # bridge can see gba_port.h if needed later







if(BUILD_APPS)
  add_subdirectory(apps/frame_viewer)
endif()

target_link_libraries(frame_viewer PRIVATE gba_hal pokeemerald_headers)