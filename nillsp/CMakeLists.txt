cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/NilVersion.cmake")
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(nillsp VERSION "${NIL_VERSION_NUMERIC}" LANGUAGES CXX)
endif()

include(GNUInstallDirs)

if(DEFINED NY_BUILD_LSP AND NOT DEFINED NIL_BUILD_LSP)
    set(NIL_BUILD_LSP "${NY_BUILD_LSP}" CACHE BOOL "Build the nillsp executable")
endif()
option(NIL_BUILD_LSP "Build the nillsp executable" ON)

set(NILLSP_SOURCES
    main.cpp
    json_rpc.hpp
    json_rpc.cpp
    lsp_server.hpp
    lsp_server.cpp
    lsp_handlers.hpp
    lsp_handlers.cpp
    ny_parser.hpp
    ny_parser.cpp
    matrix_parser.hpp
    matrix_parser.cpp
)

if(NIL_BUILD_LSP)
    add_executable(nillsp ${NILLSP_SOURCES})
    nil_apply_version(nillsp)
    target_compile_features(nillsp PRIVATE cxx_std_17)

    find_package(Threads REQUIRED)
    target_link_libraries(nillsp PRIVATE Threads::Threads)

    install(TARGETS nillsp RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
