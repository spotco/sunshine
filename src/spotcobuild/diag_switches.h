/**
 * @file src/spotcobuild/diag_switches.h
 * @brief Runtime diagnostic / per-session override switches (default: unset = unchanged behavior).
 */
#pragma once

#include <mutex>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace spotcobuild {

struct diag_overrides_t {
  // Empty / nullopt => do not override existing fork behavior.
  std::optional<std::string> force_codec;  // h264 | hevc | av1
  std::optional<bool> disable_hdr;
  std::optional<std::string> force_display;  // physical | virtual
  std::optional<std::string> force_capture;  // ddx | wgc | ...
  std::optional<bool> disable_nvenc_two_pass;
  std::optional<bool> disable_async_encoding;
  std::optional<bool> force_software_encode;

  bool recovery_enabled = false;
  bool recovery_allow_process_restart = false;
  bool recovery_allow_software_encode = false;
  int recovery_backoff_ms = 500;
  int ring_seconds = 30;
  bool timeline_enabled = true;
  bool tdr_correlate_enabled = true;

  nlohmann::json to_json() const;
};

class diag_switches_t {
public:
  static diag_switches_t &instance();

  diag_overrides_t get() const;
  void set(diag_overrides_t o);
  void apply_from_config();

  /** Effective knobs including existing config values for bundles/health. */
  nlohmann::json effective_snapshot() const;

private:
  mutable std::mutex mutex_;
  diag_overrides_t overrides_;
};

}  // namespace spotcobuild
