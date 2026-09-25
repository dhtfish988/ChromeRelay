#pragma once
#include <chromerelay/browser.hpp>
namespace chromerelay {
class ActionClock {
public:
  explicit ActionClock(Milliseconds allowance);
  Milliseconds remaining() const;
  void pause(Milliseconds duration) const;

private:
  std::chrono::steady_clock::time_point end_;
};

// A remote object is held only for the duration of an action. Its identity
// stays fixed across readiness checks and input, and is released on failure as
// well.
class ElementLease {
public:
  ElementLease(BrowserWorkspace &browser, std::string identity);
  ElementLease(ElementLease &&other) noexcept;
  ElementLease(const ElementLease &) = delete;
  ElementLease &operator=(const ElementLease &) = delete;
  ~ElementLease();
  Json call(const Json &arguments, Milliseconds timeout);
  const std::string &identity() const { return identity_; }
  const std::string &session() const { return session_; }

private:
  BrowserWorkspace *browser_;
  std::string identity_, session_;
};

class DomActions {
public:
  DomActions(BrowserWorkspace &browser, Milliseconds allowance)
      : browser_(browser), clock_(allowance) {}
  static bool supports(const std::string &operation);
  Json execute(const std::string &operation, const Json &arguments);
  Json query(Json arguments, bool by_value = true);
  ElementLease locate(Json arguments, bool visible = false,
                      bool enabled = false, bool editable = false);
  Json capture_bounds(const std::string &selector);

private:
  Json click(const Json &arguments);
  Json enter_text(const Json &arguments, bool filling);
  Json read(const Json &arguments);
  Json check(const Json &arguments);
  Json wait(const Json &arguments);
  Json assert_fact(const Json &arguments);
  Json scroll(const Json &arguments);
  Json fill_form(const Json &arguments);
  Json mouse(const Json &arguments);
  Json drag(const Json &arguments);
  Json point(ElementLease &element, bool scroll = true);
  void press(const std::string &combination);
  void pointer(const std::string &type, double x, double y,
               const std::string &button = "none", int count = 0);
  BrowserWorkspace &browser_;
  ActionClock clock_;
  std::string pointer_target_;
};
} // namespace chromerelay
