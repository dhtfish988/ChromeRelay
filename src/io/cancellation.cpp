#include <algorithm>
#include <chromerelay/common.hpp>
#include <thread>
namespace chromerelay {
namespace {
thread_local std::stop_token active_token;
}
CancellationScope::CancellationScope(std::stop_token token)
    : previous_(active_token) {
  active_token = std::move(token);
}
CancellationScope::~CancellationScope() { active_token = previous_; }
void cancellation_point() {
  if (active_token.stop_requested())
    throw RequestCancelled();
}
void interruptible_pause(Milliseconds duration) {
  const auto until = std::chrono::steady_clock::now() + duration;
  while (true) {
    cancellation_point();
    const auto left = std::chrono::ceil<Milliseconds>(
        until - std::chrono::steady_clock::now());
    if (left.count() <= 0)
      return;
    std::this_thread::sleep_for(std::min(left, Milliseconds(10)));
  }
}
} // namespace chromerelay
