# Aperture — CMake Build

# Aperture integrates with the NIL framework build.
# This CMakeLists.txt defines the aperture target.

set(APERTURE_SOURCES
    src/main.cpp
    src/core/aperture_core.cpp
    src/explore/explore_engine.cpp
    src/memory/memory_engine.cpp
    src/trace/trace_engine.cpp
    src/agent/agent_interface.cpp
)

set(APERTURE_HEADERS
    src/core/aperture_core.hpp
    src/explore/explore_engine.hpp
    src/memory/memory_engine.hpp
    src/trace/trace_engine.hpp
    src/agent/agent_interface.hpp
)

add_executable(aperture ${APERTURE_SOURCES} ${APERTURE_HEADERS})

target_include_directories(aperture PRIVATE src)

# Link against NIL core libraries
target_link_libraries(aperture PRIVATE nilcompiler_core nilruntime)

# C++17 required
target_compile_features(aperture PRIVATE cxx_std_17)

install(TARGETS aperture DESTINATION ${CMAKE_INSTALL_BINDIR})
