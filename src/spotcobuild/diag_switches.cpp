/**
 * @file src/spotcobuild/diag_switches.cpp
 * @brief Runtime diagnostic / per-session override switches.
 */
#include "diag_switches.h"

#include "session_timeline.h"

#include "src/config.h"

namespace spotcobuild {

nlohmann::json diag_overrides_t::to_json() const {
  nlohmann::json j;
  if (force_codec) j["force_codec"] = *force_codec;
  if (disable_hdr) j["disable_hdr"] = *disable_hdr;
  if (force_display) j["force_display"] = *force_display;
  if (force_capture) j["force_capture"] = *force_capture;
  if (disable_nvenc_two_pass) j["disable_nvenc_two_pass"] = *disable_nvenc_two_pass;
  if (disable_async_encoding) j["disable_async_encoding"] = *disable_async_encoding;
  if (force_software_encode) j["force_software_encode"] = *force_software_encode;
  j["recovery_enabled"] = recovery_enabled;
  j["recovery_allow_process_restart"] = recovery_allow_process_restart;
  j["recovery_allow_software_encode"] = recovery_allow_software_encode;
  j["recovery_backoff_ms"] = recovery_backoff_ms;
  j["ring_seconds"] = ring_seconds;
  j["timeline_enabled"] = timeline_enabled;
  j["tdr_correlate_enabled"] = tdr_correlate_enabled;
  j["require_display_before_launch"] = config::diag.require_display_before_launch;
  j["startup_display_wait_ms"] = config::diag.startup_display_wait_ms;
  j["channel_raise_grace_ms"] = config::diag.channel_raise_grace_ms;
  return j;
}

diag_switches_t &diag_switches_t::instance() {
  static diag_switches_t inst;
  return inst;
}

diag_overrides_t diag_switches_t::get() const {
  std::lock_guard lg(mutex_);
  return overrides_;
}

void diag_switches_t::set(diag_overrides_t o) {
  std::lock_guard lg(mutex_);
  overrides_ = std::move(o);
  session_timeline_t::instance().set_enabled(overrides_.timeline_enabled);
  session_timeline_t::instance().set_ring_window(std::chrono::milliseconds {overrides_.ring_seconds * 1000});
}

void diag_switches_t::apply_from_config() {
  // Populated from config::diag after parse; safe no-op until config wiring lands defaults.
  diag_overrides_t o;
  o.recovery_enabled = config::diag.recovery_enabled;
  o.recovery_allow_process_restart = config::diag.recovery_allow_process_restart;
  o.recovery_allow_software_encode = config::diag.recovery_allow_software_encode;
  o.recovery_backoff_ms = config::diag.recovery_backoff_ms;
  o.ring_seconds = config::diag.ring_seconds;
  o.timeline_enabled = config::diag.timeline_enabled;
  o.tdr_correlate_enabled = config::diag.tdr_correlate_enabled;
  if (!config::diag.force_codec.empty()) o.force_codec = config::diag.force_codec;
  if (config::diag.disable_hdr) o.disable_hdr = true;
  if (!config::diag.force_display.empty()) o.force_display = config::diag.force_display;
  if (!config::diag.force_capture.empty()) o.force_capture = config::diag.force_capture;
  if (config::diag.disable_nvenc_two_pass) o.disable_nvenc_two_pass = true;
  if (config::diag.disable_async_encoding) o.disable_async_encoding = true;
  if (config::diag.force_software_encode) o.force_software_encode = true;
  set(std::move(o));
}

nlohmann::json diag_switches_t::effective_snapshot() const {
  auto o = get();
  nlohmann::json j = o.to_json();
  j["existing"] = {
    {"nvenc_realtime_hags", config::video.nv_realtime_hags},
    {"capture", config::video.capture},
    {"hevc_mode", config::video.hevc_mode},
    {"av1_mode", config::video.av1_mode},
    {"nv_twopass", static_cast<int>(config::video.nv.two_pass)},
    {"min_threads", config::video.min_threads},
  };
  return j;
}

}  // namespace spotcobuild
