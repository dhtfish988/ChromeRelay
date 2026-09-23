#include <array>
#include <chromerelay/browser.hpp>
#include <cmath>
namespace chromerelay {
namespace {
struct Coordinate {
  double x, y;
};
struct Projection {
  std::array<double, 9> values;
  Coordinate apply(Coordinate point) const {
    const auto &m = values;
    const double divisor = m[6] * point.x + m[7] * point.y + m[8];
    if (!std::isfinite(divisor) || std::abs(divisor) < 1e-12)
      throw RelayError("Frame projection is singular");
    Coordinate result{(m[0] * point.x + m[1] * point.y + m[2]) / divisor,
                      (m[3] * point.x + m[4] * point.y + m[5]) / divisor};
    if (!std::isfinite(result.x) || !std::isfinite(result.y))
      throw RelayError("Frame projection is nonfinite");
    return result;
  }
  Projection inverse() const {
    const auto &m = values;
    std::array<double, 9> result = {
        m[4] * m[8] - m[5] * m[7], m[2] * m[7] - m[1] * m[8],
        m[1] * m[5] - m[2] * m[4], m[5] * m[6] - m[3] * m[8],
        m[0] * m[8] - m[2] * m[6], m[2] * m[3] - m[0] * m[5],
        m[3] * m[7] - m[4] * m[6], m[1] * m[6] - m[0] * m[7],
        m[0] * m[4] - m[1] * m[3]};
    const double determinant =
        m[0] * result[0] + m[1] * result[3] + m[2] * result[6];
    if (std::abs(determinant) < 1e-12)
      throw RelayError("Frame geometry has zero area");
    for (auto &value : result)
      value /= determinant;
    return {result};
  }
};
Projection from_quad(const Json &quad) {
  if (!quad.is_array() || quad.size() != 8)
    throw RelayError("Invalid frame content quad");
  const double x0 = quad[0], y0 = quad[1], x1 = quad[2], y1 = quad[3],
               x2 = quad[4], y2 = quad[5], x3 = quad[6], y3 = quad[7];
  const double ax = x1 - x2, bx = x3 - x2, cx = x0 - x1 + x2 - x3;
  const double ay = y1 - y2, by = y3 - y2, cy = y0 - y1 + y2 - y3;
  double g = 0, h = 0;
  if (std::abs(cx) > 1e-9 || std::abs(cy) > 1e-9) {
    const double determinant = ax * by - bx * ay;
    if (std::abs(determinant) < 1e-12)
      throw RelayError("Frame content quad is degenerate");
    g = (cx * by - bx * cy) / determinant;
    h = (ax * cy - cx * ay) / determinant;
  }
  return {{{x1 - x0 + g * x1, x3 - x0 + h * x3, x0, y1 - y0 + g * y1,
            y3 - y0 + h * y3, y0, g, h, 1}}};
}
Json script_value(const Json &response) {
  if (response.contains("exceptionDetails"))
    throw RelayError("Frame geometry script failed");
  return response.at("result").at("value");
}
} // namespace
Json BrowserWorkspace::frame_viewport(std::size_t index, Milliseconds timeout) {
  const auto &frame = frames_.at(index);
  auto viewport = script_value(
      send("Runtime.evaluate",
           {{"expression", "({width:innerWidth,height:innerHeight})"},
            {"uniqueContextId", frame.unique_context},
            {"returnByValue", true},
            {"timeout", timeout.count()}},
           frame.session, timeout));
  if (viewport.at("width").get<double>() <= 0 ||
      viewport.at("height").get<double>() <= 0)
    throw RelayError("Frame has no viewport");
  return viewport;
}
Json BrowserWorkspace::owner_geometry(std::size_t index, Milliseconds timeout) {
  auto &frame = frames_.at(index);
  frame.owner_node = send("DOM.getFrameOwner", {{"frameId", frame.frame}},
                          frame.owner_session, timeout)
                         .at("backendNodeId");
  return send("DOM.getBoxModel", {{"backendNodeId", frame.owner_node}},
              frame.owner_session, timeout)
      .at("model")
      .at("content");
}
bool BrowserWorkspace::owner_hit(std::size_t index, double x, double y,
                                 Milliseconds timeout) {
  const auto &scope = frames_.at(index);
  Json request = {{"backendNodeId", scope.owner_node}};
  if (index > 0)
    request["executionContextId"] = *frames_[index - 1].context;
  const auto object =
      send("DOM.resolveNode", request, scope.owner_session, timeout)
          .at("object")
          .at("objectId")
          .get<std::string>();
  try {
    const auto result =
        script_value(
            send("Runtime.callFunctionOn",
                 {{"objectId", object},
                  {"functionDeclaration",
                   "function(x,y){const "
                   "hit=this.getRootNode().elementFromPoint(x,y);return "
                   "this.isConnected && !!hit && (hit===this || "
                   "this.contains(hit))}"},
                  {"arguments", Json::array({{{"value", x}}, {{"value", y}}})},
                  {"returnByValue", true}},
                 scope.owner_session, timeout))
            .get<bool>();
    release_object(scope.owner_session, object);
    return result;
  } catch (...) {
    release_object(scope.owner_session, object);
    throw;
  }
}
void BrowserWorkspace::reveal_frames(Milliseconds timeout) {
  current_session();
  prepare_frames();
  for (std::size_t i = 0; i < frames_.size(); ++i) {
    auto &frame = frames_[i];
    frame.owner_node = send("DOM.getFrameOwner", {{"frameId", frame.frame}},
                            frame.owner_session, timeout)
                           .at("backendNodeId");
    send("DOM.scrollIntoViewIfNeeded", {{"backendNodeId", frame.owner_node}},
         frame.owner_session, timeout);
  }
}
void BrowserWorkspace::settle_layout(Milliseconds timeout) {
  const auto root = current_session();
  send("Page.bringToFront", Json::object(), root, timeout);
  const std::string tick = "new "
                           "Promise(resolve=>requestAnimationFrame(()=>"
                           "requestAnimationFrame(()=>resolve(true))))";
  send("Runtime.evaluate",
       {{"expression", tick},
        {"returnByValue", true},
        {"awaitPromise", true},
        {"timeout", timeout.count()}},
       root, timeout);
  if (!frames_.empty())
    evaluate(tick, timeout);
}
Json BrowserWorkspace::project_point(Json point, Milliseconds timeout,
                                     bool hit_test) {
  if (frames_.empty())
    return point;
  const auto root = current_session();
  prepare_frames();
  Coordinate position{point.at("x").get<double>(), point.at("y").get<double>()};
  bool hit = point.value("hit", true);
  bool inside = true;
  // CDP quads are relative to the local process root, while DOM coordinates are
  // relative to the selected frame. Normalize each step back to the immediate
  // parent viewport to avoid applying same-process offsets twice.
  for (std::size_t i = frames_.size(); i-- > 0;) {
    const auto viewport = frame_viewport(i, timeout);
    inside = inside && position.x >= 0 && position.y >= 0 &&
             position.x <= viewport.at("width").get<double>() &&
             position.y <= viewport.at("height").get<double>();
    const auto map = from_quad(owner_geometry(i, timeout));
    position = map.apply({position.x / viewport.at("width").get<double>(),
                          position.y / viewport.at("height").get<double>()});
    if (i > 0 &&
        frames_[i - 1].frame != session_roots_.at(frames_[i].owner_session)) {
      const auto parent_viewport = frame_viewport(i - 1, timeout);
      position =
          from_quad(owner_geometry(i - 1, timeout)).inverse().apply(position);
      position.x *= parent_viewport.at("width").get<double>();
      position.y *= parent_viewport.at("height").get<double>();
    }
    if (hit_test)
      hit = hit && owner_hit(i, position.x, position.y, timeout);
  }
  {
    const auto viewport = script_value(
        send("Runtime.evaluate",
             {{"expression", "({width:innerWidth,height:innerHeight})"},
              {"returnByValue", true}},
             root, timeout));
    inside = inside && position.x >= 0 && position.y >= 0 &&
             position.x <= viewport.at("width").get<double>() &&
             position.y <= viewport.at("height").get<double>();
  }
  if (hit_test)
    hit = hit && inside;
  point["withinViewport"] = inside;
  point["x"] = position.x;
  point["y"] = position.y;
  point["hit"] = hit;
  return point;
}
} // namespace chromerelay
