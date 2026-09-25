#pragma once
#include <chromerelay/common.hpp>
#include <map>
namespace chromerelay {
struct ActionDefinition {
  std::string name, legacy, description;
  Json schema, presets = Json::object();
  std::string operation;
};
struct ActionInvocation {
  std::string operation;
  Json arguments;
  bool allow_legacy = false;
  bool legacy_name = false;
};
class ActionCatalog {
public:
  ActionCatalog();
  Json list(bool compatibility = false) const;
  bool contains(const std::string &name, bool compatibility = false) const;
  ActionInvocation resolve(const std::string &name, const Json &arguments,
                           bool compatibility = false) const;
  const std::vector<ActionDefinition> &definitions() const {
    return definitions_;
  }

private:
  std::vector<ActionDefinition> definitions_;
  std::map<std::string, std::size_t> canonical_, legacy_;
};
} // namespace chromerelay
