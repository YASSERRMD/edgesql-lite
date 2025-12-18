#pragma once

/**
 * @file metrics.hpp
 * @brief Metrics collection for observability
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace edgesql {
namespace observability {

/**
 * @brief Metrics collector
 */
class Metrics {
public:
  /**
   * @brief Get singleton instance
   */
  static Metrics &instance();

  // Counter operations
  void increment(const std::string &name, uint64_t value = 1);
  uint64_t get_counter(const std::string &name) const;

  // Gauge operations
  void set_gauge(const std::string &name, double value);
  double get_gauge(const std::string &name) const;

  // Query metrics
  void record_query(bool success, std::chrono::microseconds duration);

  // Get summary
  uint64_t total_queries() const { return total_queries_.load(); }
  uint64_t successful_queries() const { return successful_queries_.load(); }
  uint64_t failed_queries() const { return failed_queries_.load(); }
  double avg_query_time_ms() const;

  // Reset all metrics
  void reset();

  // Export as JSON
  std::string to_json() const;

private:
  Metrics() = default;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::atomic<uint64_t>> counters_;
  std::unordered_map<std::string, double> gauges_;

  std::atomic<uint64_t> total_queries_{0};
  std::atomic<uint64_t> successful_queries_{0};
  std::atomic<uint64_t> failed_queries_{0};
  std::atomic<uint64_t> total_query_time_us_{0};
};

/**
 * @brief Health check result
 */
struct HealthStatus {
  bool healthy{true};
  std::string status{"ok"};
  std::unordered_map<std::string, std::string> components;

  std::string to_json() const;
};

/**
 * @brief Health checker
 */
class HealthChecker {
public:
  static HealthChecker &instance();

  HealthStatus check();
  void set_component_status(const std::string &name, bool healthy,
                            const std::string &message = "");

private:
  HealthChecker() = default;

  std::mutex mutex_;
  std::unordered_map<std::string, std::pair<bool, std::string>> components_;
};

} // namespace observability
} // namespace edgesql
