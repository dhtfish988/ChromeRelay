#pragma once
#include <chromerelay/browser.hpp>
#include <chromerelay/catalog.hpp>
#include <functional>
namespace chromerelay {
class ActionSequence {
public:
  using Dispatch = std::function<Json(const ActionInvocation &)>;
  ActionSequence(BrowserWorkspace &browser, const ActionCatalog &catalog,
                 Dispatch dispatch, bool allow_legacy)
      : browser_(browser), catalog_(catalog), dispatch_(std::move(dispatch)),
        allow_legacy_(allow_legacy) {}
  static bool supports(const std::string &operation);
  Json run(const ActionInvocation &invocation);

private:
  Json batch(const Json &arguments);
  Json retry(const Json &arguments);
  Json steps(const Json &arguments);
  Json row(const Json &input, const std::string &operation) const;
  Json child(const std::string &name, const Json &arguments,
             std::optional<Milliseconds> timeout = {});
  void pause(Milliseconds duration);
  bool expired() const;
  void append(Json &rows, Json value);
  BrowserWorkspace &browser_;
  const ActionCatalog &catalog_;
  Dispatch dispatch_;
  bool allow_legacy_;
  std::size_t output_bytes_ = 0;
};
} // namespace chromerelay
