#This strings autochanged from release_lib.sh :
set(VERSION_DESCRIBE v1.1.54147-testing)
set(VERSION_REVISION 54147)
#===end of autochange

set(VERSION_MAJOR 1)
set(VERSION_MINOR 1)
set(VERSION_PATCH ${VERSION_REVISION})
set(VERSION_EXTRA "")
set(VERSION_TWEAK "")
set(VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
if (VERSION_TWEAK)
	set(VERSION_STRING "${VERSION_STRING}.${VERSION_TWEAK}")
endif()
if (VERSION_EXTRA)
	set(VERSION_STRING "${VERSION_STRING}${VERSION_EXTRA}")
endif()

set(VERSION_FULL "${PROJECT_NAME} ${VERSION_STRING}")
