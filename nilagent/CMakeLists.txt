cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/NilVersion.cmake")
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(nilagent VERSION "${NIL_VERSION_NUMERIC}" LANGUAGES CXX)
endif()

include(GNUInstallDirs)

add_executable(nilagent main.cpp)
nil_apply_version(nilagent)
target_compile_features(nilagent PRIVATE cxx_std_17)
target_include_directories(nilagent PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

find_package(Threads REQUIRED)
target_link_libraries(nilagent PRIVATE Threads::Threads)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(X11 QUIET)
    if(X11_FOUND)
        target_compile_definitions(nilagent PRIVATE NY_HAS_X11=1)
        target_include_directories(nilagent PRIVATE ${X11_INCLUDE_DIR})
        target_link_libraries(nilagent PRIVATE ${X11_LIBRARIES})
    else()
        message(STATUS "X11 not found - nilagent window control disabled")
    endif()
endif()

install(TARGETS nilagent RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
