set (ICU_PATHS "/usr/local/opt/icu4c/lib")
set (ICU_INCLUDE_PATHS "/usr/local/opt/icu4c/include")
if (USE_STATIC_LIBRARIES)
	find_library (ICUI18N libicui18n.a PATHS ${ICU_PATHS})
	find_library (ICUUC libicuuc.a PATHS ${ICU_PATHS})
	find_library (ICUDATA libicudata.a PATHS ${ICU_PATHS})
else ()
	find_library (ICUI18N icui18n PATHS ${ICU_PATHS})
	find_library (ICUUC icuuc PATHS ${ICU_PATHS})
	find_library (ICUDATA icudata PATHS ${ICU_PATHS})
endif ()
set (ICU_LIBS ${ICUI18N} ${ICUUC} ${ICUDATA})

find_path (ICU_INCLUDE_DIR NAMES unicode/unistr.h PATHS ${ICU_INCLUDE_PATHS})
message(STATUS "Using icu: ${ICU_INCLUDE_DIR} : ${ICU_LIBS}")
include_directories (${ICU_INCLUDE_DIR})
