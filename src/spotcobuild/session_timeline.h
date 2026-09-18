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

  void dump_ring_on_failure(const std::string &session_id, failure_category category = failure_category::unknown);

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
  void prune_ring_unlocked(const std::string &session_id, std::chrono::steady_clock::time_point now);

  mutable std::mutex mutex_;
  bool enabled_ = true;
  std::chrono::milliseconds ring_window_ {30000};
  std::string active_session_id_;
  std::unordered_map<std::string, std::vector<timeline_event_t>> rings_;
  std::unordered_map<std::string, failure_category> last_category_;
  std::filesystem::path diag_dir_;
};

}  // namespace spotcobuild
