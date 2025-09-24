add_executable(emerald_viewer
  main.cpp
)

target_compile_features(emerald_viewer PRIVATE cxx_std_17)

target_include_directories(emerald_viewer PRIVATE
  ${CMAKE_SOURCE_DIR}/hal
  ${CMAKE_SOURCE_DIR}/bridge
)

target_link_libraries(emerald_viewer
  PRIVATE
    agb_vk
    agb_bridge
    gba_hal
    gba_hw_redirect
)

add_dependencies(emerald_viewer renderer_shaders)

if(MSVC)
  target_compile_options(emerald_viewer PRIVATE /W4 /permissive-)
else()
  target_compile_options(emerald_viewer PRIVATE -Wall -Wextra -Wpedantic)
endif()
