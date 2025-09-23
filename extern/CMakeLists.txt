# extern/CMakeLists.txt
add_library(pokeemerald_headers INTERFACE)
target_include_directories(pokeemerald_headers INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/pokeemerald/include
    ${CMAKE_CURRENT_SOURCE_DIR}/pokeemerald/include/constants
)
