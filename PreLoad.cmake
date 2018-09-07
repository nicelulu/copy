# Use Ninja instead of Unix Makefiles by default.
# https://stackoverflow.com/questions/11269833/cmake-selecting-a-generator-within-cmakelists-txt
#
# Reason: it have better startup time than make and it parallelize jobs more uniformly.
# (when comparing to make with Makefiles that was generated by CMake)
#
# How to install Ninja on Ubuntu:
#  sudo apt-get install ninja-build

# CLion does not support Ninja
if (NOT ${CMAKE_COMMAND} MATCHES "clion")
    find_program(NINJA_PATH ninja)
    if (NINJA_PATH)
        set(CMAKE_GENERATOR "Ninja" CACHE INTERNAL "" FORCE)
    endif ()
endif()
