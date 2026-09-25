#pragma once
#include <chromerelay/channel.hpp>
#include <deque>
#include <map>
#include <optional>
#include <set>
namespace chromerelay {
struct FrameScope {
  std::string frame, session;
  std::optional<std::int64_t> context;
  std::string unique_context, owner_session;
  std::int64_t owner_node = 0;
};
class BrowserWorkspace {
public:
  // Bind a multi-command action on its first page access. A closed page may be
  // replaced in the tab inventory, but the pending action must not follow it.
  // Nested scopes share their outer binding; pure delays need no connection.
  class PageScope {
  public:
    explicit PageScope(BrowserWorkspace &browser);
    ~PageScope();
    PageScope(const PageScope &) = delete;
    PageScope &operator=(const PageScope &) = delete;

  private:
    BrowserWorkspace &browser_;
    std::string target_;
    std::string *previous_;
  };
  using Deadline = std::optional<std::chrono::steady_clock::time_point>;
  explicit BrowserWorkspace(unsigned port) : port_(port) {}
  ~BrowserWorkspace() { disconnect(); }
  void connect();
  void disconnect();
  bool connected() const;
  Json tabs();
  Json status();
  Json create_tab(const std::string &url = "about:blank");
  Json activate_tab(std::size_t index);
  Json close_tab(std::optional<std::size_t> index = {});
  Json evaluate(const std::string &expression,
                Milliseconds timeout = Milliseconds(10000),
                bool by_value = true);
  bool test_condition(const std::string &expression, Milliseconds timeout);
  Json evaluate_legacy(std::string expression, Milliseconds timeout);
  Json page_call(const std::string &method,
                 const Json &parameters = Json::object(),
                 Milliseconds timeout = Milliseconds(10000));
  Json context_call(const std::string &method,
                    const Json &parameters = Json::object(),
                    Milliseconds timeout = Milliseconds(10000));
  // Remote object IDs belong to their creating session, independently of the
  // current tab/frame selection. Never reroute or replay these calls.
  Json session_call(const std::string &session, const std::string &method,
                    const Json &parameters, Milliseconds timeout);
  Json browser_call(const std::string &method,
                    const Json &parameters = Json::object(),
                    Milliseconds timeout = Milliseconds(10000));
  Json navigate(const std::string &url, const std::string &readiness = "load",
                Milliseconds timeout = Milliseconds(30000));
  Json reload(Milliseconds timeout = Milliseconds(30000));
  Json history(int direction, Milliseconds timeout = Milliseconds(30000));
  void wait_ready(const std::string &readiness, Milliseconds timeout);
  Json console_messages(std::size_t maximum = 100, bool clear = false);
  void pump();
  std::string current_session();
  const std::vector<FrameScope> &frames() const { return frames_; }
  std::vector<FrameScope> &frames() { return frames_; }
  std::string current_target() const { return current_; }
  Deadline exchange_deadline(Deadline limit);
  std::chrono::steady_clock::time_point
  bounded_deadline(Milliseconds requested) const;
  Milliseconds time_left(Milliseconds requested) const;
  std::string context_session() const;
  void release_object(const std::string &session,
                      const std::string &identity) noexcept;
  void release_input(const std::string &method,
                     const Json &parameters) noexcept;
  std::string observe_input(const std::string &element,
                            const std::string &event, int count,
                            Milliseconds timeout);
  void await_input(const std::string &ticket, Milliseconds timeout);
  void release_input_signal(const std::string &ticket) noexcept;
  Json enter_frame(const std::string &object, Milliseconds timeout);
  Json list_frames();
  Json project_point(Json point, Milliseconds timeout, bool hit_test = true);
  void reveal_frames(Milliseconds timeout);
  void settle_layout(Milliseconds timeout);
  Json manage_cookies(const Json &arguments);
  Json manage_storage(const Json &arguments);
  Json accessibility_snapshot();
  Json arm_dialog(const Json &arguments);
  Json pointer_position();
  void press_keyboard(const std::string &combination, Milliseconds timeout);
  void dispatch_pointer(const std::string &type, double x, double y,
                        const std::string &button, int count,
                        Milliseconds timeout, const std::string &expected_target = {});
  void cancel_pointer() noexcept;

private:
  std::string *bound_target_ = nullptr;
  Json run_script(const std::string &expression, Milliseconds timeout,
                  bool by_value, bool retry_replaced);
  Json await_navigation(const std::string &session,
                        const std::string &expected_loader,
                        const std::string &replaced_loader,
                        std::optional<int> history_entry,
                        const std::string &readiness,
                        std::chrono::steady_clock::time_point deadline);
  void refresh();
  void attach();
  void enable_session(const std::string &session);
  void discard_session(const std::string &session) noexcept;
  std::string frame_session(const std::string &frame,
                            const std::string &parent);
  void prepare_frames();
  Json frame_viewport(std::size_t index, Milliseconds timeout);
  Json owner_geometry(std::size_t index, Milliseconds timeout);
  bool owner_hit(std::size_t index, double x, double y, Milliseconds timeout);
  Json send(const std::string &method, const Json &parameters = Json::object(),
            const std::string &session = {},
            Milliseconds timeout = Milliseconds(10000),
            std::function<void()> observe = {});
  unsigned port_;
  std::unique_ptr<DevToolsChannel> channel_;
  std::vector<Json> targets_;
  std::map<std::string, std::string> sessions_;
  std::map<std::string, std::string> frame_sessions_, session_roots_;
  std::map<std::string, std::string> session_pages_;
  std::map<std::string, Json> dialog_rules_;
  struct PointerState {
    double x = 0, y = 0;
    int buttons = 0;
    bool intercept = false, entered = false;
    std::string session;
    Json drag;
  };
  std::map<std::string, PointerState> pointers_;
  std::map<std::string, std::map<std::string, Json>> documents_;
  struct InputWatch {
    std::string session, frame, unique_context, object;
    std::int64_t context = 0;
    bool received = false, settled = false;
  };
  std::map<std::string, InputWatch> input_watches_;
  std::string current_, context_;
  bool context_selected_ = false;
  Deadline deadline_;
  std::vector<FrameScope> frames_;
  std::deque<Json> console_;
  struct RequestScope {
    std::string frame, loader;
  };
  std::map<std::string, std::map<std::string, RequestScope>> requests_;
  std::map<std::string, std::chrono::steady_clock::time_point> changed_;
};
} // namespace chromerelay
