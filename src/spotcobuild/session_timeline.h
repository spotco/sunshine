/**
 * @file src/spotcobuild/session_timeline.h
 * @brief UUID session timeline + in-memory failure ring buffer.
 */
#pragma once

#include "categories.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace spotcobuild {

// Retention / rotation knobs (constants; config wiring optional later).
inline constexpr std::uintmax_t k_diag_jsonl_max_bytes = 8ull * 1024 * 1024;  // 8 MiB
inline constexpr std::size_t k_diag_retain_sessions = 24;
inline constexpr std::size_t k_diag_retain_ring_dumps = 24;
inline constexpr std::size_t k_diag_retain_bundles = 12;
inline constexpr std::uintmax_t k_diag_dir_max_bytes = 256ull * 1024 * 1024;  // 256 MiB
inline constexpr std::chrono::seconds k_diag_jsonl_coalesce_interval {5};

struct timeline_event_t {
  std::chrono::system_clock::time_point wall_time;
  std::chrono::steady_clock::time_point mono_time;
  std::string type;
  nlohmann::json fields;
};

class session_timeline_t {
public:
  static session_timeline_t &instance();

  std::string begin_session();
  void end_session(const std::string &session_id, std::string_view reason = {});

  void set_active(const std::string &session_id);
  std::string active_session_id() const;

  void emit(const std::string &session_id, std::string_view type, nlohmann::json fields = {}, bool persist = true);
  void emit_active(std::string_view type, nlohmann::json fields = {}, bool persist = true);

  /** Record capture/encode metadata for later first_frame fields (cheap; call from capture_start). */
  void set_capture_meta(int video_format);

  /**
   * Record a successfully encoded/sent frame.
   * Emits first_frame once; updates last_frame_number for session_end / ring dump.
   */
  void note_successful_frame(std::uint64_t frame_number);

  void dump_ring_on_failure(const std::string &session_id, failure_category category = failure_category::unknown);

  /** Delete old session JSONL / ring dumps / bundles per retention policy. */
  void prune_diagnostics_dir();

  std::filesystem::path diagnostics_dir() const;
  std::filesystem::path jsonl_path(const std::string &session_id) const;
  std::filesystem::path ring_dump_path(const std::string &session_id) const;

  std::vector<timeline_event_t> ring_snapshot(const std::string &session_id) const;
  nlohmann::json health_snapshot() const;

  void set_ring_window(std::chrono::milliseconds window);
  void set_enabled(bool enabled);
  bool enabled() const;

private:
  session_timeline_t() = default;

  void ensure_dir_unlocked();
  void append_jsonl_unlocked(const std::string &session_id, const timeline_event_t &ev);
  void write_jsonl_line_unlocked(const std::string &session_id, const timeline_event_t &ev);
  void prune_ring_unlocked(const std::string &session_id, std::chrono::steady_clock::time_point now);
  void prune_diagnostics_dir_unlocked();

  mutable std::mutex mutex_;
  bool enabled_ = true;
  std::chrono::milliseconds ring_window_ {30000};
  std::string active_session_id_;
  std::unordered_map<std::string, std::vector<timeline_event_t>> rings_;
  std::unordered_map<std::string, failure_category> last_category_;
  // session_id -> event type -> last JSONL write mono time (rate-limited coalesced types).
  std::unordered_map<std::string, std::unordered_map<std::string, std::chrono::steady_clock::time_point>> last_jsonl_coalesce_;
  std::unordered_map<std::string, bool> first_frame_seen_;
  std::unordered_map<std::string, std::uint64_t> last_frame_number_;
  std::unordered_map<std::string, int> video_format_;
  std::unordered_map<std::string, bool> last_frame_emitted_;
  std::filesystem::path diag_dir_;
};

}  // namespace spotcobuild
