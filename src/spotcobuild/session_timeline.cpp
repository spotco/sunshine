/**
 * @file src/spotcobuild/session_timeline.cpp
 * @brief UUID session timeline + in-memory failure ring buffer.
 */
#include <cstdio>
#include "session_timeline.h"

#include "src/logging.h"
#include "src/platform/common.h"
#include "src/utility.h"
#include "src/uuid.h"

#include <fstream>

namespace spotcobuild {
namespace {

std::string iso8601(std::chrono::system_clock::time_point tp) {
  const auto tt = std::chrono::system_clock::to_time_t(tp);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;
  std::tm tm {};
#if defined(_WIN32)
  gmtime_s(&tm, &tt);
#else
  gmtime_r(&tt, &tm);
#endif
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03lldZ",
    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
    tm.tm_hour, tm.tm_min, tm.tm_sec,
    static_cast<long long>(ms.count()));
  return buf;
}

}  // namespace

session_timeline_t &session_timeline_t::instance() {
  static session_timeline_t inst;
  return inst;
}

void session_timeline_t::set_ring_window(std::chrono::milliseconds window) {
  std::lock_guard lg(mutex_);
  ring_window_ = window;
}

void session_timeline_t::set_enabled(bool enabled) {
  std::lock_guard lg(mutex_);
  enabled_ = enabled;
}

bool session_timeline_t::enabled() const {
  std::lock_guard lg(mutex_);
  return enabled_;
}

void session_timeline_t::ensure_dir_unlocked() {
  if (diag_dir_.empty()) {
    diag_dir_ = platf::appdata() / "diagnostics";
  }
  std::error_code ec;
  std::filesystem::create_directories(diag_dir_, ec);
}

std::filesystem::path session_timeline_t::diagnostics_dir() const {
  std::lock_guard lg(mutex_);
  if (!diag_dir_.empty()) {
    return diag_dir_;
  }
  return platf::appdata() / "diagnostics";
}

std::filesystem::path session_timeline_t::jsonl_path(const std::string &session_id) const {
  return diagnostics_dir() / (session_id + ".jsonl");
}

std::filesystem::path session_timeline_t::ring_dump_path(const std::string &session_id) const {
  return diagnostics_dir() / (session_id + ".ring.jsonl");
}

std::string session_timeline_t::begin_session() {
  auto id = uuid_util::uuid_t::generate().string();
  std::lock_guard lg(mutex_);
  if (!enabled_) {
    active_session_id_ = id;
    return id;
  }
  ensure_dir_unlocked();
  active_session_id_ = id;
  rings_[id] = {};
  last_category_[id] = failure_category::none;

  timeline_event_t ev {
    std::chrono::system_clock::now(),
    std::chrono::steady_clock::now(),
    "session_begin",
    nlohmann::json {{"session_id", id}},
  };
  append_jsonl_unlocked(id, ev);
  rings_[id].push_back(ev);
  BOOST_LOG(info) << "spotcobuild session timeline begin uuid=" << id;
  return id;
}

void session_timeline_t::end_session(const std::string &session_id, std::string_view reason) {
  if (session_id.empty()) {
    return;
  }
  nlohmann::json fields {{"reason", std::string(reason)}};
  emit(session_id, "session_end", std::move(fields));
  std::lock_guard lg(mutex_);
  if (active_session_id_ == session_id) {
    active_session_id_.clear();
  }
}

void session_timeline_t::set_active(const std::string &session_id) {
  std::lock_guard lg(mutex_);
  active_session_id_ = session_id;
}

std::string session_timeline_t::active_session_id() const {
  std::lock_guard lg(mutex_);
  return active_session_id_;
}

void session_timeline_t::prune_ring_unlocked(const std::string &session_id, std::chrono::steady_clock::time_point now) {
  auto it = rings_.find(session_id);
  if (it == rings_.end()) {
    return;
  }
  auto &ring = it->second;
  while (!ring.empty() && (now - ring.front().mono_time) > ring_window_) {
    ring.erase(ring.begin());
  }
}

void session_timeline_t::append_jsonl_unlocked(const std::string &session_id, const timeline_event_t &ev) {
  ensure_dir_unlocked();
  const auto path = diag_dir_ / (session_id + ".jsonl");
  std::ofstream out(path, std::ios::app | std::ios::binary);
  if (!out) {
    return;
  }
  nlohmann::json line {
    {"ts", iso8601(ev.wall_time)},
    {"mono_ms", std::chrono::duration_cast<std::chrono::milliseconds>(ev.mono_time.time_since_epoch()).count()},
    {"session_id", session_id},
    {"type", ev.type},
  };
  if (!ev.fields.is_null()) {
    for (auto it = ev.fields.begin(); it != ev.fields.end(); ++it) {
      line[it.key()] = it.value();
    }
  }
  out << line.dump() << '\n';
}

void session_timeline_t::emit(const std::string &session_id, std::string_view type, nlohmann::json fields) {
  if (session_id.empty()) {
    return;
  }
  std::lock_guard lg(mutex_);
  if (!enabled_) {
    return;
  }
  const auto now_wall = std::chrono::system_clock::now();
  const auto now_mono = std::chrono::steady_clock::now();
  timeline_event_t ev {now_wall, now_mono, std::string(type), std::move(fields)};
  append_jsonl_unlocked(session_id, ev);
  rings_[session_id].push_back(ev);
  prune_ring_unlocked(session_id, now_mono);
}

void session_timeline_t::emit_active(std::string_view type, nlohmann::json fields) {
  std::string id;
  {
    std::lock_guard lg(mutex_);
    id = active_session_id_;
  }
  emit(id, type, std::move(fields));
}

void session_timeline_t::dump_ring_on_failure(const std::string &session_id, failure_category category) {
  if (session_id.empty()) {
    return;
  }
  std::lock_guard lg(mutex_);
  if (!enabled_) {
    return;
  }
  ensure_dir_unlocked();
  last_category_[session_id] = category;
  const auto path = diag_dir_ / (session_id + ".ring.jsonl");
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return;
  }
  nlohmann::json header {
    {"type", "ring_dump"},
    {"session_id", session_id},
    {"category", category_name(category)},
    {"window_ms", ring_window_.count()},
    {"ts", iso8601(std::chrono::system_clock::now())},
  };
  out << header.dump() << '\n';
  auto it = rings_.find(session_id);
  if (it != rings_.end()) {
    for (const auto &ev : it->second) {
      nlohmann::json line {
        {"ts", iso8601(ev.wall_time)},
        {"mono_ms", std::chrono::duration_cast<std::chrono::milliseconds>(ev.mono_time.time_since_epoch()).count()},
        {"type", ev.type},
        {"fields", ev.fields},
      };
      out << line.dump() << '\n';
    }
  }
  BOOST_LOG(warning) << "spotcobuild dumped failure ring for session " << session_id
                     << " category=" << category_name(category) << " path=" << path.string();
}

std::vector<timeline_event_t> session_timeline_t::ring_snapshot(const std::string &session_id) const {
  std::lock_guard lg(mutex_);
  auto it = rings_.find(session_id);
  if (it == rings_.end()) {
    return {};
  }
  return it->second;
}

nlohmann::json session_timeline_t::health_snapshot() const {
  std::lock_guard lg(mutex_);
  nlohmann::json j;
  j["enabled"] = enabled_;
  j["active_session_id"] = active_session_id_;
  j["ring_window_ms"] = ring_window_.count();
  j["diagnostics_dir"] = (diag_dir_.empty() ? (platf::appdata() / "diagnostics") : diag_dir_).string();
  if (!active_session_id_.empty()) {
    auto cat_it = last_category_.find(active_session_id_);
    if (cat_it != last_category_.end()) {
      j["last_category"] = category_name(cat_it->second);
    }
    auto ring_it = rings_.find(active_session_id_);
    j["ring_event_count"] = ring_it == rings_.end() ? 0 : ring_it->second.size();
  }
  return j;
}

}  // namespace spotcobuild
