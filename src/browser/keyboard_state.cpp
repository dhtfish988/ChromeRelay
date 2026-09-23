#include <chromerelay/browser.hpp>
#include <chromerelay/keyboard.hpp>
namespace chromerelay {
void BrowserWorkspace::press_keyboard(const std::string &combination,
                                      Milliseconds timeout) {
  const auto strokes = plan_keyboard(combination);
  const auto prior = exchange_deadline(bounded_deadline(timeout));
  struct Restore {
    BrowserWorkspace &browser;
    Deadline prior;
    ~Restore() { browser.exchange_deadline(prior); }
  } restore{*this, prior};
  const auto session = current_session();
  std::size_t held = 0;
  try {
    for (const auto &stroke : strokes) {
      ++held; // A lost response does not prove keyDown had no effect.
      send("Input.dispatchKeyEvent", stroke.pressed, session, timeout);
    }
    while (held) {
      send("Input.dispatchKeyEvent", strokes[held - 1].released, session, timeout);
      --held;
    }
  } catch (...) {
    CancellationScope cleanup;
    while (held) {
      try {
        if (connected())
          channel_->call("Input.dispatchKeyEvent", strokes[held - 1].released,
                         session, Milliseconds(100));
      } catch (...) {}
      --held;
    }
    throw;
  }
}
} // namespace chromerelay
