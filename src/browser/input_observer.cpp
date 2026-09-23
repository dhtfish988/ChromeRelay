#include <atomic>
#include <chromerelay/browser.hpp>
#include <thread>
namespace chromerelay {
std::string BrowserWorkspace::observe_input(const std::string &element,
                                            const std::string &event, int count,
                                            Milliseconds timeout) {
  current_session();
  prepare_frames();
  if (input_watches_.size() >= 1024)
    throw RelayError("Too many pending input observations");
  static std::atomic<std::uint64_t> sequence{0};
  const auto ticket =
      "__chromerelay_receipt_" +
      std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()) +
      "_" + std::to_string(sequence.fetch_add(1));
  InputWatch watch;
  watch.session = context_session();
  watch.frame =
      frames_.empty() ? session_roots_.at(watch.session) : frames_.back().frame;
  const auto &document = documents_.at(watch.session).at(watch.frame);
  watch.context = document.at("id");
  watch.unique_context = document.at("uniqueId");
  input_watches_.emplace(ticket, watch);
  try {
    send("Runtime.addBinding",
         {{"name", ticket}, {"executionContextId", watch.context}},
         watch.session, timeout);
    const std::string observer =
        R"observe(function(binding,kind,ordinal,lifetime){
      const node=this,signal=globalThis[binding];delete globalThis[binding];
      let timer,watchdog;
      const cancel=()=>{node.ownerDocument.removeEventListener(kind,accept,true);clearTimeout(timer);clearTimeout(watchdog)};
      const accept=event=>{
        if(event.isTrusted && event.composedPath().includes(node) && (ordinal===0 || event.detail>=ordinal)){
          node.ownerDocument.removeEventListener(kind,accept,true);
          signal('received');timer=setTimeout(()=>{signal('settled');cancel()},0);
        }
      };
      node.ownerDocument.addEventListener(kind,accept,true);
      watchdog=setTimeout(cancel,lifetime);
      return {cancel};
    })observe";
    const auto result =
        send("Runtime.callFunctionOn",
             {{"objectId", element},
              {"functionDeclaration", observer},
              {"arguments", Json::array({{{"value", ticket}},
                                         {{"value", event}},
                                         {{"value", count}},
                                         {{"value", timeout.count()}}})},
              {"returnByValue", false}},
             watch.session, timeout);
    if (result.contains("exceptionDetails"))
      throw RelayError("Cannot observe the target input event");
    input_watches_.at(ticket).object = result.at("result").at("objectId");
    return ticket;
  } catch (...) {
    release_input_signal(ticket);
    throw;
  }
}
void BrowserWorkspace::await_input(const std::string &ticket,
                                   Milliseconds timeout) {
  const auto end = std::chrono::steady_clock::now() + time_left(timeout);
  while (std::chrono::steady_clock::now() < end) {
    pump();
    const auto watch = input_watches_.at(ticket);
    if (watch.settled)
      return;
    if (watch.received) {
      if (!documents_.contains(watch.session) ||
          !documents_.at(watch.session).contains(watch.frame) ||
          documents_.at(watch.session).at(watch.frame).at("uniqueId") !=
              watch.unique_context)
        return;
      // The old renderer can disappear before its final context notification.
      // Probe only the original context; the input itself is never reissued.
      try {
        send("Runtime.evaluate",
             {{"expression", "0"},
              {"uniqueContextId", watch.unique_context},
              {"returnByValue", true}},
             watch.session, std::min(timeout, Milliseconds(100)));
      } catch (const ProtocolFailure &failure) {
        const auto &message = failure.description;
        const bool missing_context =
            failure.code == -32000 &&
            message.find("context") != std::string::npos &&
            (message.find("Cannot find") != std::string::npos ||
             message.find("not found") != std::string::npos ||
             message.find("destroyed") != std::string::npos);
        if (failure.code == -32001 || missing_context)
          return;
        throw;
      } catch (const RelayError &) {
        // A renderer in teardown may stop answering the harmless probe before
        // its detach event arrives. A timeout alone is not event completion.
        if (!connected())
          throw;
      }
    }
    interruptible_pause(Milliseconds(2));
  }
  throw RelayError(
      "Target did not acknowledge the input event before the action deadline");
}
void BrowserWorkspace::release_input_signal(
    const std::string &ticket) noexcept {
  CancellationScope cleanup;
  const auto found = input_watches_.find(ticket);
  if (found == input_watches_.end())
    return;
  const auto watch = found->second;
  // Each operation is best effort: a navigating frame may already be gone.
  if (connected()) {
    if (!watch.object.empty()) {
      try {
        channel_->call("Runtime.callFunctionOn",
                       {{"objectId", watch.object},
                        {"functionDeclaration", "function(){this.cancel()}"}},
                       watch.session, Milliseconds(100));
      } catch (...) {
      }
      release_object(watch.session, watch.object);
    }
    try {
      channel_->call(
          "Runtime.evaluate",
          {{"expression", "delete globalThis[" + Json(ticket).dump() + "]"},
           {"uniqueContextId", watch.unique_context}},
          watch.session, Milliseconds(100));
    } catch (...) {
    }
    try {
      channel_->call("Runtime.removeBinding", {{"name", ticket}}, watch.session,
                     Milliseconds(100));
    } catch (...) {
    }
  }
  input_watches_.erase(ticket);
}
} // namespace chromerelay
