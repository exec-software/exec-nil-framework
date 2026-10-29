cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/NilVersion.cmake")
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(nilbuild VERSION "${NIL_VERSION_NUMERIC}" LANGUAGES CXX)
endif()

include(GNUInstallDirs)
add_executable(nilbuild main.cpp ../nilcompiler/src/nildata.cpp)
nil_apply_version(nilbuild)
target_compile_features(nilbuild PRIVATE cxx_std_17)
install(TARGETS nilbuild RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
