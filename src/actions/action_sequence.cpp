#include <algorithm>
#include <chromerelay/workflow.hpp>
#include <thread>
namespace chromerelay {
namespace {
constexpr std::size_t maximum_output = 64 * 1024 * 1024;
class StepWindow {
public:
  StepWindow(BrowserWorkspace &browser, Milliseconds allowance)
      : browser_(browser) {
    const auto until = browser.bounded_deadline(allowance);
    prior_ = browser_.exchange_deadline(until);
  }
  ~StepWindow() { browser_.exchange_deadline(prior_); }

private:
  BrowserWorkspace &browser_;
  BrowserWorkspace::Deadline prior_;
};
bool truthy(const Json &value) {
  if (value.is_null())
    return false;
  if (value.is_boolean())
    return value.get<bool>();
  if (value.is_number())
    return value.get<double>() != 0;
  if (value.is_string())
    return !value.get_ref<const std::string &>().empty();
  return true; // JS arrays and objects are truthy, including empty ones.
}
std::string shorten(const std::string &text, std::size_t characters = 150) {
  std::size_t end = 0;
  while (end < text.size() && characters--) {
    const auto lead = static_cast<unsigned char>(text[end]);
    const auto width = lead < 0x80   ? 1U
                       : lead < 0xe0 ? 2U
                       : lead < 0xf0 ? 3U
                                     : 4U;
    if (end + width > text.size())
      break;
    end += width;
  }
  return text.substr(0, end);
}
std::string describe(const Json &value) {
  return value.is_string() ? value.get<std::string>() : value.dump();
}
std::optional<std::string> brief(const std::string &operation,
                                 const Json &value) {
  if (!value.is_object())
    return {};
  if (operation == "fill" || operation == "type")
    return value.contains("currentValue")
               ? shorten("value=" + value.at("currentValue").dump())
               : "ok";
  if (operation == "click") {
    std::string result =
        value.contains("elementText") ? value.at("elementText").dump() : "";
    if (value.contains("url"))
      result += (result.empty() ? "" : " → ") + describe(value.at("url"));
    return result.empty() ? "ok" : shorten(result);
  }
  if (operation == "navigate")
    return shorten(
        value.value("title", std::string()) + " (" +
            value.value("finalUrl", value.value("navigated", std::string())) +
            ")",
        120);
  if (operation == "get_page") {
    for (const auto *key : {"url", "title", "text"})
      if (value.contains(key))
        return shorten(describe(value.at(key)), 100);
    return shorten(value.dump(), 100);
  }
  if (operation == "get")
    return shorten(value.dump());
  if (operation == "eval")
    return shorten(describe(value.value("result", Json(nullptr))));
  if (operation == "find")
    return "found=" + value.value("found", Json(0)).dump() + "/" +
           value.value("total", Json(0)).dump();
  if (operation == "check") {
    for (const auto &[key, state] : value.items())
      if (state.is_boolean())
        return key + "=" + state.dump();
    return shorten(value.dump());
  }
  if (operation == "assert")
    return value.value("passed", false)
               ? "passed"
               : shorten("failed: " +
                             value.value("error",
                                         value.value("message", std::string())),
                         80);
  if (operation == "wait")
    return "waited " + value.value("waited", Json(0)).dump() + "ms";
  if (operation == "select")
    return value.contains("selected") ? shorten(describe(value.at("selected")))
                                      : "ok";
  if (operation == "status")
    return value.value("connected", false)
               ? "connected, " + value.value("tabCount", Json(0)).dump() +
                     " tabs"
               : "disconnected";
  if (operation == "list_tabs")
    return value.value("count", Json(0)).dump() + " tabs";
  if (operation == "switch_tab")
    return "tab " + value.value("switched", Json(nullptr)).dump();
  if (operation == "screenshot")
    return "captured";
  if (operation == "press_key" || operation == "hotkey")
    return shorten(value.value("pressed", std::string("ok")));
  return {};
}
Json label(const Json &row) {
  return row.is_object() && row.contains("tool") && row.at("tool").is_string()
             ? row.at("tool")
             : Json("?");
}
bool flag(const Json &row, const std::string &key) {
  return row.is_object() && row.contains(key) && row.at(key).is_boolean() &&
         row.at(key).get<bool>();
}
Milliseconds duration(const Json &row, const std::string &key,
                      double fallback = 0) {
  return Milliseconds(static_cast<std::int64_t>(row.value(key, fallback)));
}
} // namespace
bool ActionSequence::supports(const std::string &operation) {
  return operation == "batch" || operation == "retry" ||
         operation == "run_steps";
}
bool ActionSequence::expired() const {
  try {
    browser_.time_left(Milliseconds(1));
    return false;
  } catch (const DeadlineExceeded &) {
    return true;
  }
}
void ActionSequence::pause(Milliseconds amount) {
  if (amount.count() < 0)
    throw RelayError("Workflow delay must not be negative");
  if (browser_.time_left(amount) < amount)
    throw DeadlineExceeded(
        "Workflow deadline cannot accommodate the requested delay");
  const auto until = std::chrono::steady_clock::now() + amount;
  while (std::chrono::steady_clock::now() < until) {
    const auto left = std::chrono::duration_cast<Milliseconds>(
        until - std::chrono::steady_clock::now());
    browser_.pump();
    interruptible_pause(std::min(
        {left, Milliseconds(10), browser_.time_left(Milliseconds(10))}));
  }
}
void ActionSequence::append(Json &rows, Json value) {
  const auto size = value.dump().size();
  if (size > maximum_output - output_bytes_)
    throw ActionLimitExceeded("Workflow result exceeds 64 MiB");
  output_bytes_ += size;
  rows.push_back(std::move(value));
}
Json ActionSequence::row(const Json &input,
                         const std::string &operation) const {
  if (!input.is_object())
    throw RelayError("Each action must be an object");
  for (const auto &definition : catalog_.definitions())
    if (definition.operation == operation)
      return normalize_arguments(
          input, definition.schema.at("properties")
                     .at(operation == "batch" ? "actions" : "steps")
                     .at("items"));
  throw RelayError("Workflow row schema is unavailable");
}
Json ActionSequence::child(const std::string &name, const Json &arguments,
                           std::optional<Milliseconds> timeout) {
  const auto invocation = catalog_.resolve(name, arguments, allow_legacy_);
  if (timeout) {
    StepWindow window(browser_, *timeout);
    return dispatch_(invocation);
  }
  return dispatch_(invocation);
}
Json ActionSequence::run(const ActionInvocation &invocation) {
  Json result;
  if (invocation.operation == "batch")
    result = batch(invocation.arguments);
  else if (invocation.operation == "retry")
    result = retry(invocation.arguments);
  else if (invocation.operation == "run_steps")
    result = steps(invocation.arguments);
  else
    throw RelayError("Unknown workflow operation");
  if (result.dump().size() > maximum_output)
    throw ActionLimitExceeded("Workflow result exceeds 64 MiB");
  return result;
}
Json ActionSequence::batch(const Json &arguments) {
  Json rows = Json::array();
  bool timed_out = false;
  for (const auto &input : arguments.at("actions")) {
    if (expired()) {
      timed_out = true;
      break;
    }
    Json record = {{"tool", label(input)}, {"success", false}};
    try {
      const auto metadata = row(input, "batch");
      record["result"] =
          child(metadata.at("tool"), metadata.value("args", Json::object()));
      record["success"] = true;
    } catch (const ActionLimitExceeded &) {
      throw;
    } catch (const RequestCancelled &) {
      throw;
    } catch (const std::exception &error) {
      record["error"] = error.what();
    }
    const bool success = record.at("success");
    append(rows, std::move(record));
    if (expired()) {
      timed_out = true;
      break;
    }
    if (!success && flag(input, "stopOnError"))
      break;
  }
  Json result = {{"executed", rows.size()}, {"results", std::move(rows)}};
  if (timed_out)
    result["timed_out"] = true;
  return result;
}
Json ActionSequence::retry(const Json &arguments) {
  const auto name = arguments.at("tool").get<std::string>();
  const auto args = arguments.value("args", Json::object());
  // Validate even when the requested number of attempts is zero.
  catalog_.resolve(name, args, allow_legacy_);
  const auto limit = arguments.value("max_retries", 3);
  const auto key = arguments.value("success_check", std::string());
  Json response = {{"success", false}, {"attempts", 0}};
  for (int attempt = 0; attempt < limit; ++attempt) {
    if (expired()) {
      response["timed_out"] = true;
      break;
    }
    response["attempts"] = attempt + 1;
    try {
      auto result = child(name, args);
      response.erase("error");
      if (key.empty() || (result.is_object() && result.contains(key) &&
                          truthy(result.at(key))))
        return {{"success", true},
                {"attempts", attempt + 1},
                {"result", std::move(result)}};
    } catch (const ActionLimitExceeded &) {
      throw;
    } catch (const RequestCancelled &) {
      throw;
    } catch (const std::exception &error) {
      response["error"] = error.what();
    }
    if (expired()) {
      response["timed_out"] = true;
      break;
    }
    if (attempt + 1 < limit) {
      try {
        pause(duration(arguments, "delay_ms", 1000));
      } catch (const DeadlineExceeded &error) {
        response["timed_out"] = true;
        response["error"] = error.what();
        break;
      }
    }
  }
  return response;
}
Json ActionSequence::steps(const Json &arguments) {
  const auto &input_rows = arguments.at("steps");
  if (input_rows.empty() || input_rows.size() > 50)
    throw RelayError("steps must contain between 1 and 50 operations");
  const auto started = std::chrono::steady_clock::now();
  const bool stop = arguments.value("stop_on_error", true),
             intermediate = arguments.value("return_intermediate", false);
  const int attempts = arguments.value("retry_on_fail", true)
                           ? 1 + arguments.value("max_step_retries", 2)
                           : 1;
  Json rows = Json::array(), last = nullptr;
  bool timed_out = false;
  for (std::size_t index = 0; index < input_rows.size(); ++index) {
    if (expired()) {
      timed_out = true;
      break;
    }
    const auto &input = input_rows[index];
    Json record = {{"step", index}, {"tool", label(input)}, {"success", false}};
    Json metadata;
    ActionInvocation inspected;
    try {
      metadata = row(input, "run_steps");
      inspected = catalog_.resolve(metadata.at("tool"),
                                   metadata.value("args", Json::object()),
                                   allow_legacy_);
      if (supports(inspected.operation))
        throw RelayError(metadata.at("tool").get<std::string>() +
                         " is not allowed within run_steps");
    } catch (const RequestCancelled &) {
      throw;
    } catch (const std::exception &error) {
      record["error"] = error.what();
      append(rows, std::move(record));
      if (stop && !flag(input, "optional"))
        break;
      continue;
    }
    try {
      pause(std::min(duration(metadata, "wait_before"), Milliseconds(5000)));
    } catch (const DeadlineExceeded &error) {
      record["attempts"] = 0;
      record["error"] = error.what();
      append(rows, std::move(record));
      timed_out = true;
      break;
    }
    const auto per_attempt = duration(metadata, "timeout").count() > 0
                                 ? duration(metadata, "timeout")
                                 : duration(arguments, "step_timeout", 10000);
    Json value;
    for (int attempt = 0; attempt < attempts; ++attempt) {
      record["attempts"] = attempt + 1;
      try {
        value = child(metadata.at("tool"),
                      metadata.value("args", Json::object()), per_attempt);
        record["success"] = true;
        record.erase("error");
        break;
      } catch (const ActionLimitExceeded &) {
        throw;
      } catch (const RequestCancelled &) {
        throw;
      } catch (const std::exception &error) {
        record["error"] = error.what();
      }
      if (expired()) {
        timed_out = true;
        break;
      }
      if (attempt + 1 < attempts && arguments.value("auto_wait", true)) {
        try {
          pause(Milliseconds(500));
        } catch (const DeadlineExceeded &error) {
          record["error"] = error.what();
          timed_out = true;
          break;
        }
      }
    }
    const bool success = record.at("success");
    if (success) {
      last = value;
      if (const auto summary = brief(inspected.operation, value))
        record["brief"] = *summary;
      if (intermediate)
        record["result"] = std::move(value);
    }
    append(rows, std::move(record));
    if (timed_out || expired()) {
      timed_out = true;
      break;
    }
    if (!success && stop && !metadata.value("optional", false))
      break;
    try {
      pause(std::min(duration(metadata, "wait_after"), Milliseconds(5000)));
    } catch (const DeadlineExceeded &) {
      timed_out = true;
      break;
    }
  }
  const auto succeeded =
      std::count_if(rows.begin(), rows.end(), [](const auto &record) {
        return record.at("success") == true;
      });
  Json result = {{"total_steps", input_rows.size()},
                 {"executed", rows.size()},
                 {"succeeded", succeeded},
                 {"failed", rows.size() - static_cast<std::size_t>(succeeded)},
                 {"steps", std::move(rows)},
                 {"last_result", std::move(last)}};
  if (timed_out)
    result["timed_out"] = true;
  if (browser_.connected() && !expired()) {
    try {
      const auto page = browser_.page_call(
          "Runtime.evaluate",
          {{"expression", "({url:location.href,title:document.title})"},
           {"returnByValue", true}},
          Milliseconds(1000));
      if (page.at("result").contains("value"))
        result["page"] = page.at("result").at("value");
    } catch (const RequestCancelled &) {
      throw;
    } catch (const std::exception &) {
    }
  }
  result["total_time_ms"] = std::chrono::duration_cast<Milliseconds>(
                                std::chrono::steady_clock::now() - started)
                                .count();
  return result;
}
} // namespace chromerelay
