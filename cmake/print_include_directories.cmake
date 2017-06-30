get_property (dirs TARGET dbms PROPERTY INCLUDE_DIRECTORIES)
list(REMOVE_DUPLICATES dirs)
file (WRITE ${CMAKE_CURRENT_BINARY_DIR}/include_directories.txt "")
foreach (dir ${dirs})
    string (REPLACE "${ClickHouse_SOURCE_DIR}" "." dir "${dir}")
    file (APPEND ${CMAKE_CURRENT_BINARY_DIR}/include_directories.txt "-I ${dir} ")
endforeach ()
