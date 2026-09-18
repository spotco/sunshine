/**
 * @file src/spotcobuild/recovery.h
 * @brief In-process capture/encoder recovery with optional codec fallback.
 */
#pragma once

#include "categories.h"
#include "error_classify.h"

#include <atomic>
#include <mutex>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace spotcobuild {

enum class recovery_action {
  none,
  recreate_same_codec,
  fallback_h264,
  fallback_software,
  process_restart,
  give_up,
};

struct recovery_result_t {
  recovery_action action = recovery_action::none;
  bool attempted = false;
  bool success = false;
  std::string detail;
  nlohmann::json to_json() const;
};

inline constexpr const char *recovery_action_name(recovery_action a) {
  switch (a) {
    case recovery_action::none: return "none";
    case recovery_action::recreate_same_codec: return "recreate_same_codec";
    case recovery_action::fallback_h264: return "fallback_h264";
    case recovery_action::fallback_software: return "fallback_software";
    case recovery_action::process_restart: return "process_restart";
    case recovery_action::give_up: return "give_up";
  }
  return "none";
}

class recovery_controller_t {
public:
  static recovery_controller_t &instance();

  /** Decide and optionally arm recovery for a classified failure. Returns action for caller. */
  recovery_result_t on_failure(const std::string &session_id, const classified_error_t &classified, std::string_view current_codec);

  /** Capture/encode loop should check this and treat as reinit request. */
  bool consume_recreate_request();

  /** Preferred codec after fallback decision (empty = unchanged). */
  std::string preferred_codec_override() const;

  bool prefer_software_encode() const;

  nlohmann::json health_snapshot() const;

  /** Fault-injection hook for tests. */
  void reset_for_test();
  void inject_fake_device_lost_for_test(const std::string &session_id);

private:
  std::atomic<bool> recreate_requested_ {false};
  std::atomic<int> attempts_in_cluster_ {0};
  std::string preferred_codec_;
  bool prefer_software_ = false;
  recovery_result_t last_;
  mutable std::mutex mutex_;
};

/** Run TDR correlate + ring dump + recovery decision on session failure. */
recovery_result_t handle_session_failure(const std::string &session_id, const classified_error_t &classified, std::string_view current_codec);

}  // namespace spotcobuild
