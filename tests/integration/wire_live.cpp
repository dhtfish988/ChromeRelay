#include <chromerelay/channel.hpp>
#include <iostream>
int main(int argc, char **argv) {
  using namespace chromerelay;
  if (argc != 2)
    return 2;
  try {
    const auto port = static_cast<unsigned>(std::stoul(argv[1]));
    DevToolsChannel channel(port);
    auto version = channel.call("Browser.getVersion");
    auto created =
        channel.call("Target.createTarget", {{"url", "about:blank"}});
    const auto target = created.at("targetId").get<std::string>();
    auto attached = channel.call("Target.attachToTarget",
                                 {{"targetId", target}, {"flatten", true}});
    auto session = attached.at("sessionId").get<std::string>();
    channel.call("Runtime.enable", Json::object(), session);
    const auto evaluation = channel.call(
        "Runtime.evaluate", {{"expression", "21 * 2"}, {"returnByValue", true}},
        session);
    if (evaluation.at("result").at("value") != 42)
      throw RelayError("evaluation mismatch");
    bool rejected = false;
    try {
      channel.call("NoSuchDomain.missing");
    } catch (const RelayError &) {
      rejected = true;
    }
    if (!rejected)
      throw RelayError("protocol error was not propagated");
    auto events = channel.drain_events();
    if (events.empty())
      throw RelayError("no execution-context event");
    channel.call("Target.closeTarget", {{"targetId", target}});
    channel.disconnect();
    if (channel.connected())
      throw RelayError("disconnect did not close channel");
    if (!discover_browser(port).contains("Browser"))
      throw RelayError("browser was terminated by disconnect");
    std::cout << Json({{"passed", true},
                       {"checks", 7},
                       {"browser", version},
                       {"events", events.size()}})
                     .dump(2)
              << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
