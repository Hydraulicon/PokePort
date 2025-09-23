# apps/emerald_viewer/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

# Executable
add_executable(emerald_viewer
    main.cpp
)

# C++ standard (same as the rest of the tree)
target_compile_features(emerald_viewer PRIVATE cxx_std_17)

# Include dirs: HAL (gba_port.h), bridge (AgbHwState API), renderer public headers
target_include_directories(emerald_viewer PRIVATE
    ${CMAKE_SOURCE_DIR}/hal
    ${CMAKE_SOURCE_DIR}/bridge
    ${CMAKE_SOURCE_DIR}/renderer/src
)

# Link against the split renderer + bridge
target_link_libraries(emerald_viewer PRIVATE agb_vk agb_bridge)

# If your renderer target didn't propagate Vulkan as PUBLIC, be explicit here.
# Safe to keep even if not strictly required.
find_package(Vulkan REQUIRED)
target_link_libraries(emerald_viewer PRIVATE Vulkan::Vulkan)

# Shader path define: identical contract used by agb_vk.cpp/readFile(SHADER_SPV_PATH)
# This points to the SPIR-V produced under the renderer's binary dir.
target_compile_definitions(emerald_viewer PRIVATE
    SHADER_SPV_PATH=\"${CMAKE_BINARY_DIR}/renderer/compose_frame.comp.spv\"
)

# Make sure the SPIR-V gets built before we run (only if that custom target exists).
# Your build already shows a custom step "Compiling compose_frame.comp -> compose_frame.comp.spv",
# so guard the dependency by name if it's available.
if (TARGET compose_spv)
    add_dependencies(emerald_viewer compose_spv)
endif()
