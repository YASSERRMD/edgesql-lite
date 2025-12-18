#pragma once

/**
 * @file context.hpp
 * @brief Execution context with budget enforcement
 */

#include "../memory/query_allocator.hpp"
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

namespace edgesql {
namespace executor {

/**
 * @brief Query budget limits
 */
struct QueryBudget {
  size_t max_memory_bytes = 64 * 1024 * 1024; // 64MB default
  uint64_t max_instructions = 10000000;       // 10M instructions
  std::chrono::milliseconds max_time{30000};  // 30 seconds
  size_t max_result_rows = 100000;            // 100K rows
};

/**
 * @brief Execution statistics
 */
struct ExecutionStats {
  uint64_t instructions_executed = 0;
  uint64_t rows_scanned = 0;
  uint64_t rows_returned = 0;
  size_t memory_used = 0;
  std::chrono::microseconds elapsed_time{0};
};

/**
 * @brief Budget violation type
 */
enum class BudgetViolation {
  NONE,
  MEMORY_EXCEEDED,
  INSTRUCTIONS_EXCEEDED,
  TIMEOUT,
  ROWS_EXCEEDED,
  ABORTED
};

/**
 * @brief Execution context
 *
 * Tracks execution state and enforces resource budgets.
 */
class ExecutionContext {
public:
  /**
   * @brief Constructor
   * @param budget Query budget limits
   * @param allocator Memory allocator
   */
  ExecutionContext(const QueryBudget &budget,
                   memory::QueryAllocator &allocator);

  /**
   * @brief Start execution timing
   */
  void start();

  /**
   * @brief Check if we should stop execution
   * @return true if budget exceeded or aborted
   */
  bool should_stop() const;

  /**
   * @brief Record instructions executed
   * @param count Number of instructions
   */
  void record_instructions(uint64_t count);

  /**
   * @brief Record a row scanned
   */
  void record_row_scanned();

  /**
   * @brief Record a row returned in result
   */
  void record_row_returned();

  /**
   * @brief Check budget (throws on violation)
   * @throws std::runtime_error on budget violation
   */
  void check_budget();

  /**
   * @brief Request abort
   */
  void abort();

  /**
   * @brief Check if aborted
   */
  bool is_aborted() const { return aborted_; }

  /**
   * @brief Get violation type
   */
  BudgetViolation violation() const { return violation_; }

  /**
   * @brief Get violation message
   */
  std::string violation_message() const;

  /**
   * @brief Get execution statistics
   */
  const ExecutionStats &stats() const { return stats_; }

  /**
   * @brief Get budget
   */
  const QueryBudget &budget() const { return budget_; }

  /**
   * @brief Get allocator
   */
  memory::QueryAllocator &allocator() { return allocator_; }

  /**
   * @brief Allocate memory through context
   */
  void *allocate(size_t size);

  /**
   * @brief Finalize and update final stats
   */
  void finalize();

private:
  void check_time();
  void check_instructions();
  void check_rows();

  QueryBudget budget_;
  memory::QueryAllocator &allocator_;
  ExecutionStats stats_;

  std::chrono::steady_clock::time_point start_time_;
  bool started_{false};
  bool aborted_{false};
  BudgetViolation violation_{BudgetViolation::NONE};
};

} // namespace executor
} // namespace edgesql
