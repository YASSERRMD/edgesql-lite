cmake_minimum_required(VERSION 3.20)
project(edgesql-lite VERSION 0.1.0 LANGUAGES CXX)

# C++20 Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wpedantic -Werror")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Static linking option
option(STATIC_BUILD "Build static binary" OFF)
if(STATIC_BUILD)
    set(CMAKE_EXE_LINKER_FLAGS "-static")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/src)

# Source files - Core
set(CORE_SOURCES
    src/core/signal_handler.cpp
    src/core/thread_pool.cpp
    src/core/shutdown.cpp
)

# Source files - Storage (Phase 2)
set(STORAGE_SOURCES
    src/storage/wal.cpp
    src/storage/page_manager.cpp
    src/storage/segment.cpp
    src/storage/recovery.cpp
)

# Source files - Memory (Phase 3)
set(MEMORY_SOURCES
    src/memory/arena.cpp
    src/memory/query_allocator.cpp
    src/memory/memory_tracker.cpp
)

# Source files - SQL (Phase 4 - stub files for now)
set(SQL_SOURCES)

# Source files - Planner (Phase 5 - stub files for now)
set(PLANNER_SOURCES)

# Source files - Executor (Phase 6 - stub files for now)
set(EXECUTOR_SOURCES)

# Source files - Concurrency (Phase 7 - stub files for now)
set(CONCURRENCY_SOURCES)

# Source files - Server (Phase 1 - implemented)
set(SERVER_SOURCES
    src/server/listener.cpp
)

# Source files - Observability (Phase 9 - stub files for now)
set(OBSERVABILITY_SOURCES)

# Source files - Security (Phase 10 - stub files for now)
set(SECURITY_SOURCES)

# All sources
set(ALL_SOURCES
    ${CORE_SOURCES}
    ${STORAGE_SOURCES}
    ${MEMORY_SOURCES}
    ${SERVER_SOURCES}
)

# Main executable
add_executable(edgesql-lite src/main.cpp ${ALL_SOURCES})

# Link libraries
target_link_libraries(edgesql-lite PRIVATE pthread)

# Install
install(TARGETS edgesql-lite DESTINATION bin)
install(FILES config/edgesql.conf.example DESTINATION etc/edgesql)

# Testing
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "EdgeSQL Lite Configuration:")
message(STATUS "  Version:      ${PROJECT_VERSION}")
message(STATUS "  Build type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Static build: ${STATIC_BUILD}")
message(STATUS "  Build tests:  ${BUILD_TESTS}")
message(STATUS "")
