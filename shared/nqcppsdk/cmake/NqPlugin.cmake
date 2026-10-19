function(nq_add_plugin name)
    add_library(${name} MODULE ${ARGN})
    target_link_libraries(${name} PRIVATE nqcppsdk::nqcppsdk)
    set_target_properties(${name} PROPERTIES
        PREFIX ""
        SUFFIX ".so"
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
endfunction()
