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

# Source files - Storage
set(STORAGE_SOURCES
    src/storage/wal.cpp
    src/storage/page_manager.cpp
    src/storage/segment.cpp
    src/storage/recovery.cpp
)

# Source files - Memory
set(MEMORY_SOURCES
    src/memory/arena.cpp
    src/memory/query_allocator.cpp
    src/memory/memory_tracker.cpp
)

# Source files - SQL
set(SQL_SOURCES
    src/sql/tokenizer.cpp
    src/sql/parser.cpp
)

# Source files - Planner
set(PLANNER_SOURCES
    src/planner/table_scan.cpp
    src/planner/order_by.cpp
    src/planner/aggregate.cpp
    src/planner/planner.cpp
)

# Source files - Executor
set(EXECUTOR_SOURCES
    src/executor/executor.cpp
)

# Source files - Concurrency
set(CONCURRENCY_SOURCES
    src/concurrency/transaction.cpp
)

# Source files - Server
set(SERVER_SOURCES
    src/server/listener.cpp
    src/server/http_server.cpp
    src/server/query_handler.cpp
    src/server/health_handler.cpp
)

# Source files - Observability
set(OBSERVABILITY_SOURCES
    src/observability/query_log.cpp
    src/observability/metrics.cpp
)

# Source files - Security
set(SECURITY_SOURCES
    src/security/auth.cpp
    src/security/roles.cpp
    src/security/sandbox.cpp
)

# All sources
set(ALL_SOURCES
    ${CORE_SOURCES}
    ${STORAGE_SOURCES}
    ${MEMORY_SOURCES}
    ${SQL_SOURCES}
    ${PLANNER_SOURCES}
    ${EXECUTOR_SOURCES}
    ${CONCURRENCY_SOURCES}
    ${SERVER_SOURCES}
    ${OBSERVABILITY_SOURCES}
    ${SECURITY_SOURCES}
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
