#pragma once
#include <chromerelay/common.hpp>
namespace chromerelay {
struct KeyStroke {
  Json pressed;
  Json released;
};
// Validate the whole chord before any input. Press in vector order, release in
// reverse order; each release is also suitable for partial-failure cleanup.
std::vector<KeyStroke> plan_keyboard(const std::string &combination);
} // namespace chromerelay
