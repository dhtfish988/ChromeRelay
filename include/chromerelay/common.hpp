#pragma once
#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <vector>
namespace chromerelay {
using Json = nlohmann::json;
using Milliseconds = std::chrono::milliseconds;
class RequestCancelled : public std::runtime_error {
public:
  RequestCancelled() : std::runtime_error("Request was cancelled") {}
};
// Request-local cooperative cancellation. An empty token deliberately masks
// cancellation during bounded cleanup; nested scopes restore the prior token.
class CancellationScope {
public:
  explicit CancellationScope(std::stop_token token = {});
  ~CancellationScope();
  CancellationScope(const CancellationScope &) = delete;
  CancellationScope &operator=(const CancellationScope &) = delete;

private:
  std::stop_token previous_;
};
void cancellation_point();
void interruptible_pause(Milliseconds duration);
class RelayError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};
class DeadlineExceeded : public RelayError {
public:
  using RelayError::RelayError;
};
class ActionLimitExceeded : public RelayError {
public:
  using RelayError::RelayError;
};
class ProtocolFailure : public RelayError {
public:
  explicit ProtocolFailure(const Json &detail)
      : RelayError("DevTools: " + detail.dump()), code(detail.value("code", 0)),
        description(detail.value("message", std::string())) {}
  int code;
  std::string description;
};
Json parse_message(const std::string &text,
                   std::size_t limit = 16 * 1024 * 1024);
Json normalize_arguments(const Json &arguments, const Json &schema);
std::string read_text(const std::filesystem::path &path,
                      std::size_t maximum = 16 * 1024 * 1024);
} // namespace chromerelay
