#pragma once
#include <chromerelay/common.hpp>
#include <functional>
#include <memory>
namespace chromerelay {
Json discover_browser(unsigned port, Milliseconds timeout = Milliseconds(5000));
Json decode_devtools_message(const std::string &text);
class DevToolsChannel {
public:
  explicit DevToolsChannel(unsigned port,
                           Milliseconds timeout = Milliseconds(5000));
  ~DevToolsChannel();
  DevToolsChannel(const DevToolsChannel &) = delete;
  DevToolsChannel &operator=(const DevToolsChannel &) = delete;
  Json call(const std::string &method, const Json &parameters = Json::object(),
            const std::string &session = {},
            Milliseconds timeout = Milliseconds(10000),
            std::function<void()> progress = {});
  std::vector<Json> drain_events();
  bool connected() const;
  void disconnect();

private:
  struct Engine;
  std::unique_ptr<Engine> engine_;
};
} // namespace chromerelay
