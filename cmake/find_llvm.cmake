option (ENABLE_EMBEDDED_COMPILER "Set to TRUE to enable support for 'compile' option for query execution" 1)
option (USE_INTERNAL_LLVM_LIBRARY "Use bundled or system LLVM library. Default: system library for quicker developer builds." 0)

if (ENABLE_EMBEDDED_COMPILER)

    if (USE_INTERNAL_LLVM_LIBRARY AND NOT EXISTS "${ClickHouse_SOURCE_DIR}/contrib/llvm/llvm/CMakeLists.txt")
        message (WARNING "submodule contrib/llvm is missing. to fix try run: \n git submodule update --init --recursive")
        set (USE_INTERNAL_LLVM_LIBRARY 0)
    endif ()

    if (NOT USE_INTERNAL_LZ4_LIBRARY)
        set (LLVM_PATHS "/usr/local/lib/llvm")

        if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            find_package(LLVM CONFIG PATHS ${LLVM_PATHS})
        else ()
            find_package(LLVM 5 CONFIG PATHS ${LLVM_PATHS})
        endif ()

        if (LLVM_FOUND)
            # Remove dynamically-linked zlib and libedit from LLVM's dependencies:
            set_target_properties(LLVMSupport PROPERTIES INTERFACE_LINK_LIBRARIES "-lpthread;LLVMDemangle")
            set_target_properties(LLVMLineEditor PROPERTIES INTERFACE_LINK_LIBRARIES "LLVMSupport")

            option(LLVM_HAS_RTTI "Enable if LLVM was build with RTTI enabled" ON)
            set (USE_EMBEDDED_COMPILER 1)
        endif()
    else()
        set (LLVM_FOUND 1)
        set (USE_EMBEDDED_COMPILER 1)
        set (LLVM_VERSION "7.0.0bundled")
        set (LLVM_INCLUDE_DIRS ${ClickHouse_SOURCE_DIR}/contrib/llvm/llvm/include ${ClickHouse_BINARY_DIR}/contrib/llvm/llvm/include)
        set (LLVM_LIBRARY_DIRS ${ClickHouse_BINARY_DIR}/contrib/llvm/llvm)
    endif()

    if (LLVM_FOUND)
        message(STATUS "LLVM version: ${LLVM_PACKAGE_VERSION}")
        message(STATUS "LLVM include Directory: ${LLVM_INCLUDE_DIRS}")
        message(STATUS "LLVM library Directory: ${LLVM_LIBRARY_DIRS}")
        message(STATUS "LLVM C++ compiler flags: ${LLVM_CXXFLAGS}")
    endif()
endif()
