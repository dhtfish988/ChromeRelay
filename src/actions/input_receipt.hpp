#pragma once
#include <chromerelay/browser.hpp>
namespace chromerelay {
class InputReceipt {
public:
  InputReceipt(BrowserWorkspace &browser, const std::string &element,
               const std::string &event, int count, Milliseconds timeout)
      : browser_(browser),
        ticket_(browser.observe_input(element, event, count, timeout)) {}
  ~InputReceipt() { browser_.release_input_signal(ticket_); }
  InputReceipt(const InputReceipt &) = delete;
  InputReceipt &operator=(const InputReceipt &) = delete;
  void finish(Milliseconds timeout) { browser_.await_input(ticket_, timeout); }

private:
  BrowserWorkspace &browser_;
  std::string ticket_;
};
} // namespace chromerelay
