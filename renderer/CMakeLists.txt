cmake_minimum_required(VERSION 3.22)

# Vulkan should already be found at the root; re-find is harmless
find_package(Vulkan REQUIRED)

# ---- Find glslc (Vulkan SDK) ----
find_program(GLSLC glslc
  HINTS
    $ENV{VULKAN_SDK}/Bin
    $ENV{VULKAN_SDK}/bin
    $ENV{VULKAN_SDK}/Bin32
)
if(NOT GLSLC)
  message(FATAL_ERROR "glslc not found. Install Vulkan SDK (set VULKAN_SDK) or add glslc to PATH.")
endif()

# ---- Shader compile rule ----
set(SHADER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/shaders/compose_frame.comp)
set(SHADER_SPV ${CMAKE_CURRENT_BINARY_DIR}/compose_frame.comp.spv)

add_custom_command(
  OUTPUT  ${SHADER_SPV}
  COMMAND ${GLSLC} -o ${SHADER_SPV} ${SHADER_SRC}
  DEPENDS ${SHADER_SRC}
  COMMENT "Compiling compose_frame.comp -> compose_frame.comp.spv"
  VERBATIM
)
add_custom_target(renderer_shaders DEPENDS ${SHADER_SPV})

# Forward-slash the path so MSVC string literal is clean
set(SHADER_SPV_ESCAPED ${SHADER_SPV})
if(WIN32)
  string(REPLACE "\\" "/" SHADER_SPV_ESCAPED "${SHADER_SPV_ESCAPED}")
endif()

# ---- Renderer library (STATIC, deterministic) ----
set(AGBVK_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/agb_vk.cpp
)

# Optional guard to help diagnose path issues
foreach(f IN LISTS AGBVK_SOURCES)
  if(NOT EXISTS "${f}")
    message(FATAL_ERROR "renderer: expected source not found: ${f}")
  endif()
endforeach()

add_library(agb_vk STATIC ${AGBVK_SOURCES})

target_include_directories(agb_vk
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(agb_vk
  PUBLIC
    Vulkan::Vulkan
)

# Define the shader path for the library itself (so agb_vk.cpp sees it)
target_compile_definitions(agb_vk
  PUBLIC
    SHADER_SPV_PATH="${SHADER_SPV_ESCAPED}"
)

# Make sure the SPIR-V exists before anything uses the lib
add_dependencies(agb_vk renderer_shaders)

# Warnings
if(MSVC)
  target_compile_options(agb_vk PRIVATE /W4 /permissive-)
else()
  target_compile_options(agb_vk PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Debug print
get_target_property(_vk_inc Vulkan::Vulkan INTERFACE_INCLUDE_DIRECTORIES)
message(STATUS "Vulkan include dirs: ${_vk_inc}")
