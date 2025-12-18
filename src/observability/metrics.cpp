/**
 * @file metrics.cpp
 * @brief Metrics implementation
 */

#include "metrics.hpp"
#include <sstream>

namespace edgesql {
namespace observability {

Metrics &Metrics::instance() {
  static Metrics instance;
  return instance;
}

void Metrics::increment(const std::string &name, uint64_t value) {
  std::lock_guard<std::mutex> lock(mutex_);
  counters_[name].fetch_add(value, std::memory_order_relaxed);
}

uint64_t Metrics::get_counter(const std::string &name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = counters_.find(name);
  if (it != counters_.end()) {
    return it->second.load(std::memory_order_relaxed);
  }
  return 0;
}

void Metrics::set_gauge(const std::string &name, double value) {
  std::lock_guard<std::mutex> lock(mutex_);
  gauges_[name] = value;
}

double Metrics::get_gauge(const std::string &name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = gauges_.find(name);
  if (it != gauges_.end()) {
    return it->second;
  }
  return 0.0;
}

void Metrics::record_query(bool success, std::chrono::microseconds duration) {
  total_queries_.fetch_add(1, std::memory_order_relaxed);
  total_query_time_us_.fetch_add(static_cast<uint64_t>(duration.count()),
                                 std::memory_order_relaxed);

  if (success) {
    successful_queries_.fetch_add(1, std::memory_order_relaxed);
  } else {
    failed_queries_.fetch_add(1, std::memory_order_relaxed);
  }
}

double Metrics::avg_query_time_ms() const {
  uint64_t total = total_queries_.load(std::memory_order_relaxed);
  if (total == 0)
    return 0.0;

  uint64_t time_us = total_query_time_us_.load(std::memory_order_relaxed);
  return static_cast<double>(time_us) / static_cast<double>(total) / 1000.0;
}

void Metrics::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  counters_.clear();
  gauges_.clear();
  total_queries_.store(0, std::memory_order_relaxed);
  successful_queries_.store(0, std::memory_order_relaxed);
  failed_queries_.store(0, std::memory_order_relaxed);
  total_query_time_us_.store(0, std::memory_order_relaxed);
}

std::string Metrics::to_json() const {
  std::ostringstream out;

  out << "{\n";
  out << "  \"queries\": {\n";
  out << "    \"total\": " << total_queries_.load() << ",\n";
  out << "    \"successful\": " << successful_queries_.load() << ",\n";
  out << "    \"failed\": " << failed_queries_.load() << ",\n";
  out << "    \"avg_time_ms\": " << avg_query_time_ms() << "\n";
  out << "  },\n";

  out << "  \"counters\": {";
  {
    std::lock_guard<std::mutex> lock(mutex_);
    bool first = true;
    for (const auto &[name, value] : counters_) {
      if (!first)
        out << ",";
      first = false;
      out << "\n    \"" << name << "\": " << value.load();
    }
  }
  if (!counters_.empty())
    out << "\n  ";
  out << "},\n";

  out << "  \"gauges\": {";
  {
    std::lock_guard<std::mutex> lock(mutex_);
    bool first = true;
    for (const auto &[name, value] : gauges_) {
      if (!first)
        out << ",";
      first = false;
      out << "\n    \"" << name << "\": " << value;
    }
  }
  if (!gauges_.empty())
    out << "\n  ";
  out << "}\n";
  out << "}";

  return out.str();
}

// HealthChecker

HealthChecker &HealthChecker::instance() {
  static HealthChecker instance;
  return instance;
}

HealthStatus HealthChecker::check() {
  std::lock_guard<std::mutex> lock(mutex_);

  HealthStatus status;
  status.healthy = true;

  for (const auto &[name, info] : components_) {
    status.components[name] = info.second;
    if (!info.first) {
      status.healthy = false;
    }
  }

  status.status = status.healthy ? "ok" : "degraded";
  return status;
}

void HealthChecker::set_component_status(const std::string &name, bool healthy,
                                         const std::string &message) {
  std::lock_guard<std::mutex> lock(mutex_);
  components_[name] = {healthy,
                       message.empty() ? (healthy ? "ok" : "error") : message};
}

std::string HealthStatus::to_json() const {
  std::ostringstream out;

  out << "{\n";
  out << "  \"healthy\": " << (healthy ? "true" : "false") << ",\n";
  out << "  \"status\": \"" << status << "\",\n";
  out << "  \"components\": {";

  bool first = true;
  for (const auto &[name, value] : components) {
    if (!first)
      out << ",";
    first = false;
    out << "\n    \"" << name << "\": \"" << value << "\"";
  }
  if (!components.empty())
    out << "\n  ";
  out << "}\n";
  out << "}";

  return out.str();
}

} // namespace observability
} // namespace edgesql
