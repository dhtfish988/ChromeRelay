#pragma once
#include <chromerelay/catalog.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
namespace chromerelay {
class StdioEndpoint {
public:
  using Handler = std::function<Json(const ActionInvocation &)>;
  StdioEndpoint(Handler handler, bool compatibility = false);
  std::optional<Json> receive(const Json &message);
  Json parse_failure() const;

private:
  ActionCatalog catalog_;
  Handler handler_;
  bool compatibility_ = false, initialized_ = false, ready_ = false;
  std::string version_;
};
class MessageLines {
public:
  explicit MessageLines(std::size_t maximum = 16 * 1024 * 1024)
      : maximum_(maximum) {}
  std::vector<std::string> feed(std::string_view bytes, bool end = false);

private:
  std::string pending_;
  std::size_t maximum_;
};
// One execution worker preserves browser operation order while the caller
// continues reading cancellation notifications. Sink calls are serialized.
class RequestQueue {
public:
  using Sink = std::function<void(const Json &)>;
  RequestQueue(StdioEndpoint::Handler handler, Sink sink,
               bool compatibility = false, std::size_t maximum_requests = 128,
               std::size_t maximum_bytes = 64 * 1024 * 1024);
  ~RequestQueue();
  RequestQueue(const RequestQueue &) = delete;
  RequestQueue &operator=(const RequestQueue &) = delete;
  void submit(const std::string &line);
  void finish();
  void check_failure() const;

private:
  struct Engine;
  std::unique_ptr<Engine> engine_;
};
} // namespace chromerelay
