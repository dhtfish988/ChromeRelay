#pragma once
#include <chromerelay/browser.hpp>
#include <chromerelay/catalog.hpp>
#include <chromerelay/files.hpp>
namespace chromerelay {
class ActionRuntime {
public:
  explicit ActionRuntime(
      unsigned port,
      std::vector<std::filesystem::path> roots = PathAccess::defaults())
      : browser_(port), paths_(std::move(roots)), port_(port) {}
  Json invoke(const ActionInvocation &invocation);
  BrowserWorkspace &browser() { return browser_; }

private:
  Json execute(const ActionInvocation &invocation);
  Milliseconds deadline(const Json &arguments, int fallback) const;
  Milliseconds allowance(const std::string &operation,
                         const Json &arguments) const;
  BrowserWorkspace browser_;
  PathAccess paths_;
  ActionCatalog catalog_;
  unsigned invocation_depth_ = 0, dispatched_ = 0;
  unsigned port_;
  int quick_ = 3000, normal_ = 5000, long_ = 10000;
  bool diagnostic_ = false;
  struct Metric {
    std::uint64_t count = 0, failures = 0, duration = 0;
  };
  std::map<std::string, Metric> metrics_;
};
} // namespace chromerelay
