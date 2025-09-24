# extern/CMakeLists.txt (or top-level where appropriate)
add_library(pe_headers OBJECT
  ${CMAKE_SOURCE_DIR}/extern/host_pe/pe_probe.c
)

# Compile as C, bring in Emerald headers *only* here (not transitive)
set_source_files_properties(${CMAKE_SOURCE_DIR}/extern/host_pe/pe_probe.c PROPERTIES LANGUAGE C)
target_include_directories(pe_headers PRIVATE
  ${CMAKE_SOURCE_DIR}/extern/pokeemerald/include
  ${CMAKE_SOURCE_DIR}/extern/pokeemerald/include/constants
)

# Force-include our host config before any Emerald header, on MSVC and others
if (MSVC)
  target_compile_options(pe_headers PRIVATE /TC /FI ${CMAKE_SOURCE_DIR}/extern/host_pe/pe_host_config.h)
else()
  target_compile_options(pe_headers PRIVATE -std=c11 -include ${CMAKE_SOURCE_DIR}/extern/host_pe/pe_host_config.h)
endif()


