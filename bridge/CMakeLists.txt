set(BRIDGE_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/agb_bridge.cpp
)

add_library(agb_bridge STATIC ${BRIDGE_SOURCES})

target_compile_features(agb_bridge PRIVATE cxx_std_17)

target_include_directories(agb_bridge
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/renderer/src
)

target_link_libraries(agb_bridge
  PUBLIC
    agb_vk
    gba_hal
)

if(MSVC)
  target_compile_options(agb_bridge PRIVATE /W4 /permissive-)
else()
  target_compile_options(agb_bridge PRIVATE -Wall -Wextra -Wpedantic)
endif()
