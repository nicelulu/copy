include(CMakeFindDependencyMacro)
find_dependency(PocoFoundation)
find_dependency(PocoUtil)
find_dependency(PocoNet)
find_dependency(PocoCrypto)
include("${CMAKE_CURRENT_LIST_DIR}/PocoNetSSLTargets.cmake")
