#pragma once

/**
 * @file config.hpp
 * @brief Global configuration for EdgeSQL Lite
 */

#include <cstdint>
#include <string>
#include <chrono>

namespace edgesql {

/**
 * @brief Server configuration
 */
struct ServerConfig {
    std::string bind_address = "0.0.0.0";
    uint16_t port = 8080;
    size_t worker_threads = 4;
    size_t max_connections = 1000;
};

/**
 * @brief Storage configuration
 */
struct StorageConfig {
    std::string data_dir = "./data";
    size_t page_size = 8192;  // 8KB pages
    bool wal_sync = true;
    size_t wal_buffer_size = 1024 * 1024;  // 1MB
};

/**
 * @brief Memory configuration
 */
struct MemoryConfig {
    size_t global_limit_bytes = 512 * 1024 * 1024;  // 512MB
    size_t default_query_limit_bytes = 64 * 1024 * 1024;  // 64MB
    size_t arena_block_size = 64 * 1024;  // 64KB
};

/**
 * @brief Query budget limits
 */
struct BudgetConfig {
    uint64_t default_max_instructions = 1000000;
    std::chrono::milliseconds default_max_time{5000};
    size_t default_max_memory_bytes = 64 * 1024 * 1024;  // 64MB
};

/**
 * @brief Security configuration
 */
struct SecurityConfig {
    bool require_auth = true;
    std::string api_keys_file;
    bool tls_enabled = false;
    std::string tls_cert_path;
    std::string tls_key_path;
};

/**
 * @brief Logging configuration
 */
struct LoggingConfig {
    std::string level = "info";
    std::string format = "json";
    std::string file;
};

/**
 * @brief Complete server configuration
 */
struct Config {
    ServerConfig server;
    StorageConfig storage;
    MemoryConfig memory;
    BudgetConfig budget;
    SecurityConfig security;
    LoggingConfig logging;
    
    /**
     * @brief Load configuration from file
     * @param path Path to configuration file
     * @return Loaded configuration
     */
    static Config load(const std::string& path);
    
    /**
     * @brief Get default configuration
     */
    static Config defaults();
};

// Version information
constexpr const char* VERSION = "0.1.0";
constexpr const char* BUILD_DATE = __DATE__;

// Page constants
constexpr size_t PAGE_SIZE = 8192;
constexpr uint32_t PAGE_MAGIC = 0x45444247;  // "EDBG"

// WAL constants
constexpr uint32_t WAL_MAGIC = 0x57414C45;  // "WALE"

}  // namespace edgesql
