# extern/CMakeLists.txt
add_library(pokeemerald_headers INTERFACE)

# Mark as SYSTEM and append (AFTER) so it will not shadow CRT later when we use it.
target_include_directories(pokeemerald_headers INTERFACE SYSTEM
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/pokeemerald/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/pokeemerald/include/constants>
)
