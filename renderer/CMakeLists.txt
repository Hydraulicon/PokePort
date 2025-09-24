set(RENDERER_SHADER ${CMAKE_CURRENT_SOURCE_DIR}/shaders/compose_frame.comp)
set(RENDERER_SHADER_SPV ${CMAKE_CURRENT_BINARY_DIR}/compose_frame.comp.spv)

find_program(GLSLC glslc
  HINTS
    $ENV{VULKAN_SDK}/Bin
    $ENV{VULKAN_SDK}/bin
    $ENV{VULKAN_SDK}/Bin32
    $ENV{VULKAN_SDK}/bin32
)

if(NOT GLSLC)
  message(FATAL_ERROR "glslc not found. Install the Vulkan SDK or add glslc to PATH.")
endif()

add_custom_command(
  OUTPUT ${RENDERER_SHADER_SPV}
  COMMAND ${GLSLC} --target-env=vulkan1.1 -o ${RENDERER_SHADER_SPV} ${RENDERER_SHADER}
  DEPENDS ${RENDERER_SHADER}
  COMMENT "Compiling compose_frame.comp → compose_frame.comp.spv"
  VERBATIM
)
add_custom_target(renderer_shaders DEPENDS ${RENDERER_SHADER_SPV})

set(AGBVK_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/agb_vk.cpp
)

add_library(agb_vk STATIC ${AGBVK_SOURCES})

target_compile_features(agb_vk PRIVATE cxx_std_17)

target_include_directories(agb_vk
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(agb_vk
  PUBLIC
    Vulkan::Vulkan
)

file(TO_CMAKE_PATH "${RENDERER_SHADER_SPV}" RENDERER_SHADER_SPV_ESCAPED)
target_compile_definitions(agb_vk
  PUBLIC
    SHADER_SPV_PATH="${RENDERER_SHADER_SPV_ESCAPED}"
)

add_dependencies(agb_vk renderer_shaders)

if(MSVC)
  target_compile_options(agb_vk PRIVATE /W4 /permissive-)
else()
  target_compile_options(agb_vk PRIVATE -Wall -Wextra -Wpedantic)
endif()
