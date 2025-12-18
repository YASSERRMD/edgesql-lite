/**
 * @file context.cpp
 * @brief Execution context implementation
 */

#include "context.hpp"
#include <stdexcept>

namespace edgesql {
namespace executor {

ExecutionContext::ExecutionContext(const QueryBudget &budget,
                                   memory::QueryAllocator &allocator)
    : budget_(budget), allocator_(allocator) {}

void ExecutionContext::start() {
  start_time_ = std::chrono::steady_clock::now();
  started_ = true;
}

bool ExecutionContext::should_stop() const {
  if (aborted_)
    return true;
  if (violation_ != BudgetViolation::NONE)
    return true;

  // Quick time check
  if (started_) {
    auto elapsed = std::chrono::steady_clock::now() - start_time_;
    if (elapsed > budget_.max_time) {
      return true;
    }
  }

  // Check instruction limit
  if (stats_.instructions_executed >= budget_.max_instructions) {
    return true;
  }

  return false;
}

void ExecutionContext::record_instructions(uint64_t count) {
  stats_.instructions_executed += count;
}

void ExecutionContext::record_row_scanned() { stats_.rows_scanned++; }

void ExecutionContext::record_row_returned() { stats_.rows_returned++; }

void ExecutionContext::check_budget() {
  if (aborted_) {
    violation_ = BudgetViolation::ABORTED;
    throw std::runtime_error(violation_message());
  }

  check_time();
  check_instructions();
  check_rows();

  // Check memory
  stats_.memory_used = allocator_.bytes_used();
  if (allocator_.would_exceed(0)) {
    violation_ = BudgetViolation::MEMORY_EXCEEDED;
    throw std::runtime_error(violation_message());
  }
}

void ExecutionContext::abort() {
  aborted_ = true;
  violation_ = BudgetViolation::ABORTED;
}

std::string ExecutionContext::violation_message() const {
  switch (violation_) {
  case BudgetViolation::NONE:
    return "No violation";
  case BudgetViolation::MEMORY_EXCEEDED:
    return "Memory budget exceeded: " + std::to_string(stats_.memory_used) +
           " bytes used, limit is " + std::to_string(budget_.max_memory_bytes);
  case BudgetViolation::INSTRUCTIONS_EXCEEDED:
    return "Instruction limit exceeded: " +
           std::to_string(stats_.instructions_executed) +
           " executed, limit is " + std::to_string(budget_.max_instructions);
  case BudgetViolation::TIMEOUT:
    return "Query timeout after " +
           std::to_string(stats_.elapsed_time.count() / 1000) +
           "ms, limit is " + std::to_string(budget_.max_time.count()) + "ms";
  case BudgetViolation::ROWS_EXCEEDED:
    return "Row limit exceeded: " + std::to_string(stats_.rows_returned) +
           " rows, limit is " + std::to_string(budget_.max_result_rows);
  case BudgetViolation::ABORTED:
    return "Query was aborted";
  }
  return "Unknown violation";
}

void *ExecutionContext::allocate(size_t size) {
  return allocator_.allocate(size);
}

void ExecutionContext::finalize() {
  if (started_) {
    auto elapsed = std::chrono::steady_clock::now() - start_time_;
    stats_.elapsed_time =
        std::chrono::duration_cast<std::chrono::microseconds>(elapsed);
  }
  stats_.memory_used = allocator_.bytes_used();
}

void ExecutionContext::check_time() {
  if (!started_)
    return;

  auto elapsed = std::chrono::steady_clock::now() - start_time_;
  if (elapsed > budget_.max_time) {
    stats_.elapsed_time =
        std::chrono::duration_cast<std::chrono::microseconds>(elapsed);
    violation_ = BudgetViolation::TIMEOUT;
    throw std::runtime_error(violation_message());
  }
}

void ExecutionContext::check_instructions() {
  if (stats_.instructions_executed >= budget_.max_instructions) {
    violation_ = BudgetViolation::INSTRUCTIONS_EXCEEDED;
    throw std::runtime_error(violation_message());
  }
}

void ExecutionContext::check_rows() {
  if (stats_.rows_returned >= budget_.max_result_rows) {
    violation_ = BudgetViolation::ROWS_EXCEEDED;
    throw std::runtime_error(violation_message());
  }
}

} // namespace executor
} // namespace edgesql
