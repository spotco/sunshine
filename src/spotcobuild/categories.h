/**
 * @file src/spotcobuild/categories.h
 * @brief Stable failure categories for stream diagnostics.
 */
#pragma once

#include <string_view>

namespace spotcobuild {

enum class failure_category {
  none = 0,
  capture_device_lost,
  encoder_device_lost,
  driver_internal_error,
  display_mode_changed,
  network_timeout,
  process_crash,
  unknown,
};

inline constexpr std::string_view category_name(failure_category c) {
  switch (c) {
    case failure_category::none: return "NONE";
    case failure_category::capture_device_lost: return "CAPTURE_DEVICE_LOST";
    case failure_category::encoder_device_lost: return "ENCODER_DEVICE_LOST";
    case failure_category::driver_internal_error: return "DRIVER_INTERNAL_ERROR";
    case failure_category::display_mode_changed: return "DISPLAY_MODE_CHANGED";
    case failure_category::network_timeout: return "NETWORK_TIMEOUT";
    case failure_category::process_crash: return "PROCESS_CRASH";
    case failure_category::unknown: return "UNKNOWN";
  }
  return "UNKNOWN";
}

inline failure_category category_from_name(std::string_view name) {
  if (name == "CAPTURE_DEVICE_LOST") return failure_category::capture_device_lost;
  if (name == "ENCODER_DEVICE_LOST") return failure_category::encoder_device_lost;
  if (name == "DRIVER_INTERNAL_ERROR") return failure_category::driver_internal_error;
  if (name == "DISPLAY_MODE_CHANGED") return failure_category::display_mode_changed;
  if (name == "NETWORK_TIMEOUT") return failure_category::network_timeout;
  if (name == "PROCESS_CRASH") return failure_category::process_crash;
  if (name == "NONE") return failure_category::none;
  return failure_category::unknown;
}

inline bool is_recoverable(failure_category c) {
  switch (c) {
    case failure_category::capture_device_lost:
    case failure_category::encoder_device_lost:
    case failure_category::driver_internal_error:
    case failure_category::display_mode_changed:
      return true;
    default:
      return false;
  }
}

}  // namespace spotcobuild
