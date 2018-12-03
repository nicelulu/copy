function(generate_code TEMPLATE_FILE)
    foreach(NAME IN LISTS ARGN)
       configure_file (${TEMPLATE_FILE}.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/${TEMPLATE_FILE}_${NAME}.cpp)
    endforeach()
endfunction()
