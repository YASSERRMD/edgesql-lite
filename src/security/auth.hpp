#pragma once

/**
 * @file auth.hpp
 * @brief API key authentication
 */

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace edgesql {
namespace security {

/**
 * @brief Permission levels
 */
enum class Permission { READ, WRITE, ADMIN };

/**
 * @brief API key info
 */
struct ApiKeyInfo {
  std::string name;
  std::unordered_set<Permission> permissions;
  bool enabled{true};
};

/**
 * @brief API key authenticator
 */
class Authenticator {
public:
  /**
   * @brief Get singleton instance
   */
  static Authenticator &instance();

  /**
   * @brief Add an API key
   * @param key The API key string
   * @param name Human-readable name
   * @param permissions Set of permissions
   */
  void add_key(const std::string &key, const std::string &name,
               std::unordered_set<Permission> permissions);

  /**
   * @brief Remove an API key
   */
  bool remove_key(const std::string &key);

  /**
   * @brief Validate an API key
   * @return Key info if valid, nullopt otherwise
   */
  std::optional<ApiKeyInfo> validate(const std::string &key) const;

  /**
   * @brief Check if key has permission
   */
  bool has_permission(const std::string &key, Permission perm) const;

  /**
   * @brief Enable/disable a key
   */
  bool set_enabled(const std::string &key, bool enabled);

  /**
   * @brief Clear all keys
   */
  void clear();

  /**
   * @brief Get number of registered keys
   */
  size_t key_count() const;

private:
  Authenticator() = default;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, ApiKeyInfo> keys_;
};

/**
 * @brief Extract API key from request header
 * @param auth_header Authorization header value
 * @return API key, or empty string if not found
 */
std::string extract_api_key(const std::string &auth_header);

} // namespace security
} // namespace edgesql
