find_library (TERMCAP_LIBRARY termcap)
if (NOT TERMCAP_LIBRARY)
    find_library (TERMCAP_LIBRARY tinfo)
endif()
message (STATUS "Using termcap: ${TERMCAP_LIBRARY}")
