cmake_minimum_required(VERSION 3.22)

# Collect bridge sources (adjust if you add more)
set(BRIDGE_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/agb_bridge.cpp
)

foreach(f IN LISTS BRIDGE_SOURCES)
  if(NOT EXISTS "${f}")
    message(FATAL_ERROR "bridge: expected source not found: ${f}")
  endif()
endforeach()

add_library(agb_bridge STATIC ${BRIDGE_SOURCES})

target_include_directories(agb_bridge
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/renderer/src    # <- so bridge can include "agb_vk.h"
)

# Optional: also link the renderer so dependents inherit SHADER_SPV_PATH & Vulkan
target_link_libraries(agb_bridge
  PUBLIC
    agb_vk
)

if(MSVC)
  target_compile_options(agb_bridge PRIVATE /W4)
else()
  target_compile_options(agb_bridge PRIVATE -Wall -Wextra -Wpedantic)
endif()
