/**
 * @file src/spotcobuild/tdr_correlate.h
 * @brief Windows TDR / WER correlation (stubbed on non-Windows).
 */
#pragma once

#include <chrono>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace spotcobuild {

struct tdr_correlation_t {
  bool found = false;
  std::string narrative;
  std::optional<double> delta_seconds;
  std::string event_source;
  std::int64_t event_id = 0;
  nlohmann::json raw = nlohmann::json::array();

  nlohmann::json to_json() const;
};

/**
 * @brief Correlate nearby GPU timeout / TDR / WER events.
 * Non-blocking intent: callers should invoke from failure path / worker, not capture hot path.
 * On non-Windows, returns a clean no-op result.
 */
tdr_correlation_t correlate_tdr_wer(std::chrono::system_clock::time_point failure_time,
  std::chrono::seconds lookback = std::chrono::seconds {30});

/** Last correlation result (for health / bundle). */
tdr_correlation_t last_tdr_correlation();

}  // namespace spotcobuild
