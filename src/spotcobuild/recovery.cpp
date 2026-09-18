/**
 * @file src/spotcobuild/recovery.cpp
 * @brief In-process capture/encoder recovery with optional codec fallback.
 */
#include "recovery.h"

#include "diag_switches.h"
#include "session_timeline.h"
#include "tdr_correlate.h"

#include "src/logging.h"
#include "src/platform/common.h"

#include <algorithm>
#include <thread>

namespace spotcobuild {

nlohmann::json recovery_result_t::to_json() const {
  return nlohmann::json {
    {"action", recovery_action_name(action)},
    {"attempted", attempted},
    {"success", success},
    {"detail", detail},
  };
}

recovery_controller_t &recovery_controller_t::instance() {
  static recovery_controller_t inst;
  return inst;
}

void recovery_controller_t::reset_for_test() {
  recreate_requested_ = false;
  attempts_in_cluster_ = 0;
  preferred_codec_.clear();
  prefer_software_ = false;
  std::lock_guard lg(mutex_);
  last_ = {};
}

void recovery_controller_t::inject_fake_device_lost_for_test(const std::string &session_id) {
  classified_error_t err;
  err.category = failure_category::encoder_device_lost;
  err.stage = "fault_injection";
  err.message = "fake device lost";
  on_failure(session_id, err, "hevc");
}

bool recovery_controller_t::consume_recreate_request() {
  return recreate_requested_.exchange(false);
}

std::string recovery_controller_t::preferred_codec_override() const {
  std::lock_guard lg(mutex_);
  return preferred_codec_;
}

bool recovery_controller_t::prefer_software_encode() const {
  return prefer_software_;
}

nlohmann::json recovery_controller_t::health_snapshot() const {
  std::lock_guard lg(mutex_);
  nlohmann::json j = last_.to_json();
  j["attempts_in_cluster"] = attempts_in_cluster_.load();
  j["preferred_codec"] = preferred_codec_;
  j["prefer_software"] = prefer_software_;
  j["recreate_pending"] = recreate_requested_.load();
  return j;
}

recovery_result_t recovery_controller_t::on_failure(const std::string &session_id, const classified_error_t &classified, std::string_view current_codec) {
  recovery_result_t result;
  const auto switches = diag_switches_t::instance().get();

  if (!switches.recovery_enabled) {
    result.action = recovery_action::none;
    result.detail = "recovery disabled (diag_recovery_enabled=false); streaming behavior unchanged";
    std::lock_guard lg(mutex_);
    last_ = result;
    session_timeline_t::instance().emit(session_id, "recovery_skipped", result.to_json());
    return result;
  }

  if (!is_recoverable(classified.category) || classified.startup_probe) {
    result.action = recovery_action::give_up;
    result.detail = "failure not recoverable or startup_probe";
    std::lock_guard lg(mutex_);
    last_ = result;
    return result;
  }

  const int attempt = attempts_in_cluster_.fetch_add(1) + 1;
  result.attempted = true;

  session_timeline_t::instance().emit(session_id, "recovery_attempt", nlohmann::json {
    {"attempt", attempt},
    {"category", category_name(classified.category)},
    {"codec", std::string(current_codec)},
  });

  if (attempt == 1) {
    // One automatic in-process recovery: backoff then request recreate of capture+encoder.
    const auto backoff = std::chrono::milliseconds {std::max(0, switches.recovery_backoff_ms)};
    if (backoff.count() > 0) {
      std::this_thread::sleep_for(backoff);
    }
    recreate_requested_ = true;
    result.action = recovery_action::recreate_same_codec;
    result.success = true;  // armed; caller performs recreate via existing reinit path
    result.detail = "armed capture+encoder recreate with same codec after backoff";
  }
  else if (attempt == 2) {
    // Same-codec recreate considered failed if we get here again; fall back HEVC/AV1 → H.264.
    const auto codec = std::string(current_codec);
    if (codec == "hevc" || codec == "av1" || codec == "h265") {
      {
        std::lock_guard lg(mutex_);
        preferred_codec_ = "h264";
      }
      recreate_requested_ = true;
      result.action = recovery_action::fallback_h264;
      result.success = true;
      result.detail = "armed H.264 fallback after same-codec recreate failure";
    }
    else if (switches.recovery_allow_software_encode) {
      prefer_software_ = true;
      recreate_requested_ = true;
      result.action = recovery_action::fallback_software;
      result.success = true;
      result.detail = "armed software encode fallback";
    }
    else if (switches.recovery_allow_process_restart) {
      result.action = recovery_action::process_restart;
      result.detail = "escalating to platf::restart() as last resort";
      session_timeline_t::instance().emit(session_id, "recovery_escalate_process_restart", result.to_json());
      BOOST_LOG(error) << "spotcobuild recovery escalating to process restart: " << result.detail;
      platf::restart();
      result.success = true;
    }
    else {
      result.action = recovery_action::give_up;
      result.detail = "H.264/software/process-restart fallbacks unavailable or exhausted";
    }
  }
  else if (switches.recovery_allow_process_restart) {
    result.action = recovery_action::process_restart;
    result.detail = "recovery cluster exhausted; process restart";
    session_timeline_t::instance().emit(session_id, "recovery_escalate_process_restart", result.to_json());
    platf::restart();
    result.success = true;
  }
  else {
    result.action = recovery_action::give_up;
    result.detail = "recovery attempts exhausted";
  }

  {
    std::lock_guard lg(mutex_);
    last_ = result;
  }
  session_timeline_t::instance().emit(session_id, "recovery_result", result.to_json());
  BOOST_LOG(warning) << "spotcobuild recovery action=" << recovery_action_name(result.action)
                     << " detail=" << result.detail;
  return result;
}

recovery_result_t handle_session_failure(const std::string &session_id, const classified_error_t &classified, std::string_view current_codec) {
  session_timeline_t::instance().dump_ring_on_failure(session_id, classified.category);

  const auto switches = diag_switches_t::instance().get();
  if (switches.tdr_correlate_enabled) {
    // Failure path only; 200ms Event Log timeout inside correlator.
    correlate_tdr_wer(std::chrono::system_clock::now());
  }

  return recovery_controller_t::instance().on_failure(session_id, classified, current_codec);
}

}  // namespace spotcobuild
