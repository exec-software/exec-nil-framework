# VERSION is the product identity. NIL formats and runtime ABI versions are
# separate compatibility contracts and must not be derived from this value.
include_guard(DIRECTORY)

# Accept existing developer build settings while making NIL the public prefix.
# An explicitly supplied NIL setting always wins over its legacy spelling.
foreach(setting ENABLE_LLVM BUILD_EXPERIMENTAL_TOOLS BUILD_LSP NATIVE_LINKER_DRIVER)
    if(DEFINED NY_${setting} AND NOT DEFINED NIL_${setting})
        if(setting STREQUAL "NATIVE_LINKER_DRIVER")
            set(NIL_${setting} "${NY_${setting}}" CACHE STRING "Native linker driver")
        else()
            set(NIL_${setting} "${NY_${setting}}" CACHE BOOL "Migrated framework option")
        endif()
    endif()
endforeach()

get_filename_component(NIL_VERSION_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${NIL_VERSION_ROOT}/VERSION")
file(READ "${NIL_VERSION_ROOT}/VERSION" NIL_VERSION)
string(STRIP "${NIL_VERSION}" NIL_VERSION)
if(NOT NIL_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-([0-9A-Za-z-]+)(\\.[0-9A-Za-z-]+)*)?(\\+([0-9A-Za-z-]+)(\\.[0-9A-Za-z-]+)*)?$")
    message(FATAL_ERROR "VERSION must contain one semantic product version")
endif()
set(NIL_VERSION_NUMERIC "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
set(NIL_VERSION_PRERELEASE "${CMAKE_MATCH_4}")
if(NIL_VERSION_PRERELEASE)
    string(SUBSTRING "${NIL_VERSION_PRERELEASE}" 1 -1 NIL_VERSION_PRERELEASE)
    string(REPLACE "." ";" NIL_VERSION_IDENTIFIERS "${NIL_VERSION_PRERELEASE}")
    foreach(identifier IN LISTS NIL_VERSION_IDENTIFIERS)
        if(identifier MATCHES "^0[0-9]+$")
            message(FATAL_ERROR "VERSION prerelease numeric identifiers cannot have leading zeroes")
        endif()
    endforeach()
endif()

set(NIL_VERSION_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated/nil-version")
configure_file("${CMAKE_CURRENT_LIST_DIR}/nil_version.hpp.in"
    "${NIL_VERSION_INCLUDE_DIR}/nil_version.hpp" @ONLY)

function(nil_apply_version target)
    target_include_directories(${target} PRIVATE "${NIL_VERSION_INCLUDE_DIR}")
endfunction()
