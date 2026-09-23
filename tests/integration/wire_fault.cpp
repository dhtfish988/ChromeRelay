#include <chromerelay/channel.hpp>
#include <future>
#include <iostream>
#include <thread>
using namespace chromerelay;
namespace {
unsigned checks = 0;
void check(bool value, const std::string &label) {
  if (!value)
    throw RelayError(label);
  ++checks;
}
template <class Function> std::string failure(Function action) {
  try {
    action();
  } catch (const std::exception &error) {
    return error.what();
  }
  throw RelayError("Expected a transport failure, but the call succeeded");
}
} // namespace
int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  try {
    const auto port = static_cast<unsigned>(std::stoul(argv[1]));
    const std::string mode = argv[2];
    const auto started = std::chrono::steady_clock::now();
    if (mode.starts_with("discovery-") || mode.starts_with("handshake-") ||
        mode == "shared-deadline") {
      const auto error = failure([&] { DevToolsChannel channel(port, Milliseconds(250)); });
      check(!error.empty(), "setup failure is reported");
      check(std::chrono::steady_clock::now() - started < Milliseconds(1500),
            "setup failure and teardown remain bounded");
    } else {
      DevToolsChannel channel(port, Milliseconds(1000));
      if (mode == "fragmented") {
        auto result = channel.call("Owned.echo", {{"text", "中文😀"}}, "session-a");
        check(result.at("text") == "中文😀", "fragmented UTF-8 response");
        auto events = channel.drain_events();
        check(events.size() == 1 && events[0].at("params").at("text") == "事件😀" &&
                  events[0].at("sessionId") == "session-a", "fragmented session event");
        check(channel.call("Owned.echo", {{"after", true}}).at("after") == true,
              "ping/pong and following request preserve stream framing");
        check(channel.drain_events().empty(), "event drain consumes each event once");
      } else if (mode == "multiplex" || mode == "close-pending") {
        std::vector<std::future<Json>> pending;
        for (int i = 0; i < 8; ++i)
          pending.push_back(std::async(std::launch::async, [&, i] {
            return channel.call("Owned.parallel", {{"index", i}},
                                "session-" + std::to_string(i % 2), Milliseconds(2000));
          }));
        for (int i = 0; i < 8; ++i) {
          if (mode == "multiplex")
            check(pending[static_cast<std::size_t>(i)].get().at("index") == i,
                  "out-of-order response reaches its exact concurrent caller");
          else
            check(!failure([&] { (void)pending[static_cast<std::size_t>(i)].get(); }).empty(),
                  "peer close fails every pending caller");
        }
        check(channel.connected() == (mode == "multiplex"), "channel liveness after peer results");
      } else if (mode == "protocol-error") {
        bool matched = false;
        try { channel.call("Owned.error"); }
        catch (const ProtocolFailure &error) {
          matched = error.code == -32601 && error.description == "Owned unsupported method";
        }
        check(matched, "structured CDP error preserves code and message");
        check(channel.call("Owned.echo", {{"recovered", true}}).at("recovered") == true,
              "ordinary protocol error leaves channel usable");
      } else if (mode == "late-reply" || mode == "progress-error" || mode == "cancel-call") {
        if (mode == "late-reply") {
          bool timed_out = false;
          try { channel.call("Owned.wait", Json::object(), {}, Milliseconds(40)); }
          catch (const DeadlineExceeded &) { timed_out = true; }
          check(timed_out, "missing reply reaches request deadline");
        } else if (mode == "progress-error") {
          auto error = failure([&] {
            channel.call("Owned.wait", Json::object(), {}, Milliseconds(1000),
                         [] { throw RelayError("owned progress failure"); });
          });
          check(error == "owned progress failure", "progress failure cancels its pending response");
        } else {
          std::stop_source source;
          std::jthread stopping([&] { std::this_thread::sleep_for(Milliseconds(40)); source.request_stop(); });
          bool cancelled = false;
          try {
            CancellationScope scope(source.get_token());
            channel.call("Owned.wait", Json::object(), {}, Milliseconds(1000));
          } catch (const RequestCancelled &) { cancelled = true; }
          check(cancelled, "request cancellation interrupts pending transport wait");
        }
        check(channel.call("Owned.echo", {{"fresh", 98}}).at("fresh") == 98,
              "late abandoned reply cannot satisfy the following request");
        check(channel.connected(), "abandoned call keeps healthy transport usable");
      } else if (mode == "request-limit") {
        const auto error = failure([&] {
          channel.call("Owned.tooLarge", {{"data", std::string(16 * 1024 * 1024, 'x')}});
        });
        check(error.find("size limit") != std::string::npos, "oversized request rejected before transmission");
        check(channel.call("Owned.echo", {{"small", true}}).at("small") == true,
              "request size refusal preserves connection");
      } else {
        const auto error = failure([&] {
          channel.call("Owned.bad", Json::object(), "session-a", Milliseconds(4000));
        });
        check(error.find("timed out") == std::string::npos,
              "wire violation actively fails the request before its deadline: " + error);
        check(!channel.connected(), "invalid wire data closes the transport");
      }
      channel.disconnect();
      check(!channel.connected(), "explicit disconnect leaves no live channel");
    }
    std::cout << Json({{"mode", mode}, {"checks", checks}, {"passed", true}}).dump() << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
