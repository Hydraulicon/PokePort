set(PE_PROBE ${CMAKE_CURRENT_SOURCE_DIR}/host_pe/pe_probe.c)

add_library(pe_headers OBJECT ${PE_PROBE})
set_source_files_properties(${PE_PROBE} PROPERTIES LANGUAGE C)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/pokeemerald/include)
  target_include_directories(pe_headers PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/pokeemerald/include
  )
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/pokeemerald/include/constants)
  target_include_directories(pe_headers PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/pokeemerald/include/constants
  )
endif()

if(MSVC)
  target_compile_options(pe_headers PRIVATE /TC "/FI${CMAKE_CURRENT_SOURCE_DIR}/host_pe/pe_host_config.h")
else()
  target_compile_options(pe_headers PRIVATE -std=gnu11 -include ${CMAKE_CURRENT_SOURCE_DIR}/host_pe/pe_host_config.h)
endif()

add_custom_target(pe_headers_build ALL DEPENDS pe_headers)
