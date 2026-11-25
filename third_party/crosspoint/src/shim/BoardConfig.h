// HeySticky shim: the parts of freeink BoardConfig the reader engine reads.
#pragma once
#include <stdint.h>

namespace BoardConfig {
enum class DisplayController : uint8_t { Unknown };
struct ViewableInsets {
  uint8_t top = 0, right = 0, bottom = 0, left = 0;
};
struct Profile {
  ViewableInsets viewableInsets = {};
  uint8_t cornerRadius = 0;
};
inline const Profile ACTIVE{};
inline bool hasTouch() { return true; }
}  // namespace BoardConfig
