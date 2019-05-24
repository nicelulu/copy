# TODO(danlark1). Disable LFAlloc for a while to fix mmap count problem
if (NOT OS_LINUX AND NOT SANITIZE AND NOT ARCH_ARM AND NOT ARCH_32 AND NOT ARCH_PPC64LE AND NOT OS_FREEBSD AND NOT APPLE)
    option (ENABLE_LFALLOC "Set to FALSE to use system libgsasl library instead of bundled" ${NOT_UNBUNDLED})
endif ()

if (ENABLE_LFALLOC)
    set (USE_LFALLOC 1)
    set (USE_LFALLOC_RANDOM_HINT 1)
    set (LFALLOC_INCLUDE_DIR ${ClickHouse_SOURCE_DIR}/contrib/lfalloc/src)
    message (STATUS "Using lfalloc=${USE_LFALLOC}: ${LFALLOC_INCLUDE_DIR}")
endif ()
