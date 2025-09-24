add_library(gba_hal INTERFACE)

target_include_directories(gba_hal INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

add_library(gba_hw_redirect STATIC
  gba_hw_redirect.cpp
)

target_compile_features(gba_hw_redirect PRIVATE cxx_std_17)

target_include_directories(gba_hw_redirect
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(gba_hw_redirect
  PUBLIC
    gba_hal
)

if(MSVC)
  target_compile_options(gba_hw_redirect PRIVATE /W4 /permissive-)
else()
  target_compile_options(gba_hw_redirect PRIVATE -Wall -Wextra -Wpedantic)
endif()
