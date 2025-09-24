add_executable(frame_viewer
  main.cpp
)

target_compile_features(frame_viewer PRIVATE cxx_std_17)

target_link_libraries(frame_viewer
  PRIVATE
    agb_bridge
    agb_vk
    gba_hal
)

add_dependencies(frame_viewer renderer_shaders)

if(MSVC)
  target_compile_options(frame_viewer PRIVATE /W4 /permissive-)
else()
  target_compile_options(frame_viewer PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(frame_viewer PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
