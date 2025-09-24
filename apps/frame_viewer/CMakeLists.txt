cmake_minimum_required(VERSION 3.22)

# Prefer the canonical viewer main; fall back for transitional layouts
set(_fv_candidates
  ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp       # <= your repo currently has this
  ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
  ${CMAKE_SOURCE_DIR}/renderer/src/main.cpp  # last resort while migrating
)

set(FRAME_VIEWER_MAIN "")
foreach(cand IN LISTS _fv_candidates)
  if(EXISTS "${cand}")
    set(FRAME_VIEWER_MAIN "${cand}")
    message(STATUS "frame_viewer: using ${cand}")
    break()
  endif()
endforeach()

if(FRAME_VIEWER_MAIN STREQUAL "")
  message(FATAL_ERROR
    "frame_viewer: Could not find main.cpp. Expected apps/frame_viewer/main.cpp (preferred),"
    " or apps/frame_viewer/src/main.cpp, or renderer/src/main.cpp as a temporary fallback.")
endif()

add_executable(frame_viewer ${FRAME_VIEWER_MAIN})

# Build shader before running the viewer
add_dependencies(frame_viewer renderer_shaders)

# Link shim + renderer (renderer carries Vulkan & SHADER_SPV_PATH)
target_link_libraries(frame_viewer PRIVATE agb_bridge agb_vk gba_hal)

if(MSVC)
  target_compile_options(frame_viewer PRIVATE /W4)
else()
  target_compile_options(frame_viewer PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(frame_viewer PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
