/**
 * @file auth.cpp
 * @brief Authentication implementation
 */

#include "auth.hpp"
#include <algorithm>

namespace edgesql {
namespace security {

Authenticator &Authenticator::instance() {
  static Authenticator instance;
  return instance;
}

void Authenticator::add_key(const std::string &key, const std::string &name,
                            std::unordered_set<Permission> permissions) {
  std::lock_guard<std::mutex> lock(mutex_);

  ApiKeyInfo info;
  info.name = name;
  info.permissions = std::move(permissions);
  info.enabled = true;

  keys_[key] = std::move(info);
}

bool Authenticator::remove_key(const std::string &key) {
  std::lock_guard<std::mutex> lock(mutex_);
  return keys_.erase(key) > 0;
}

std::optional<ApiKeyInfo>
Authenticator::validate(const std::string &key) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = keys_.find(key);
  if (it != keys_.end() && it->second.enabled) {
    return it->second;
  }
  return std::nullopt;
}

bool Authenticator::has_permission(const std::string &key,
                                   Permission perm) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = keys_.find(key);
  if (it != keys_.end() && it->second.enabled) {
    return it->second.permissions.count(perm) > 0;
  }
  return false;
}

bool Authenticator::set_enabled(const std::string &key, bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = keys_.find(key);
  if (it != keys_.end()) {
    it->second.enabled = enabled;
    return true;
  }
  return false;
}

void Authenticator::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  keys_.clear();
}

size_t Authenticator::key_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return keys_.size();
}

std::string extract_api_key(const std::string &auth_header) {
  // Support both "Bearer <key>" and just "<key>"
  const std::string bearer_prefix = "Bearer ";

  if (auth_header.substr(0, bearer_prefix.size()) == bearer_prefix) {
    return auth_header.substr(bearer_prefix.size());
  }

  // Also check for "ApiKey " prefix
  const std::string apikey_prefix = "ApiKey ";
  if (auth_header.substr(0, apikey_prefix.size()) == apikey_prefix) {
    return auth_header.substr(apikey_prefix.size());
  }

  return auth_header;
}

} // namespace security
} // namespace edgesql
