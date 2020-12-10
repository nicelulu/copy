option(ENABLE_LIBPQXX "Enalbe libpqxx" ${ENABLE_LIBRARIES})

if (NOT ENABLE_LIBPQXX)
    return()
endif()

if (NOT EXISTS "${ClickHouse_SOURCE_DIR}/contrib/libpqxx/CMakeLists.txt")
    message (WARNING "submodule contrib/libpqxx is missing. to fix try run: \n git submodule update --init --recursive")
    message (${RECONFIGURE_MESSAGE_LEVEL} "Can't find internal libpqxx library")
    set (USE_LIBPQXX 0)
    return()
endif()

if (NOT EXISTS "${ClickHouse_SOURCE_DIR}/contrib/libpq")
    message (ERROR "submodule contrib/libpq is missing. to fix try run: \n git submodule update --init --recursive")
    message (${RECONFIGURE_MESSAGE_LEVEL} "Can't find internal libpq needed for libpqxx")
    set (USE_LIBPQXX 0)
    return()
endif()

set (USE_LIBPQXX 1)
set (LIBPQXX_LIBRARY libpqxx)
set (LIBPQ_LIBRARY libpq)

set (LIBPQXX_INCLUDE_DIR "${ClickHouse_SOURCE_DIR}/contrib/libpqxx/include")
set (LIBPQ_ROOT_DIR "${ClickHouse_SOURCE_DIR}/contrib/libpq")

message (STATUS "Using libpqxx=${USE_LIBPQXX}: ${LIBPQXX_INCLUDE_DIR} : ${LIBPQXX_LIBRARY}")
message (STATUS "Using libpq: ${LIBPQ_ROOT_DIR} : ${LIBPQ_INCLUDE_DIR} : ${LIBPQ_LIBRARY}")

