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

#include <algorithm>
#include <fstream>
#include <vector>

namespace spotcobuild {
namespace {

bool is_ring_coalesced_type(std::string_view type) {
  return type == "control_ping_received" ||
         type == "video_ping_received" ||
         type == "audio_ping_received" ||
         type == "packet_send_fail" ||
         type == "dxgi_error" ||
         type == "nvenc_error" ||
         type == "nvenc_probe_error" ||
         type == "network_error" ||
         type == "display_change" ||
         type == "display_mode_changed" ||
         type == "capture_surface_recreate";
}

/** High-frequency types that may persist to JSONL, but at most ~every 5s while coalesced. */
bool is_jsonl_rate_limited_type(std::string_view type) {
  return type == "packet_send_fail" ||
         type == "dxgi_error" ||
         type == "nvenc_error" ||
         type == "nvenc_probe_error" ||
         type == "network_error" ||
         type == "display_change" ||
         type == "display_mode_changed" ||
         type == "capture_surface_recreate";
}


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

struct diag_file_t {
  std::filesystem::path path;
  std::filesystem::file_time_type mtime {};
  std::uintmax_t size = 0;
};

bool mtime_newer(const diag_file_t &a, const diag_file_t &b) {
  return a.mtime > b.mtime;
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

void session_timeline_t::prune_diagnostics_dir() {
  std::lock_guard lg(mutex_);
  if (!enabled_) {
    return;
  }
  prune_diagnostics_dir_unlocked();
}

void session_timeline_t::prune_diagnostics_dir_unlocked() {
  ensure_dir_unlocked();
  std::error_code ec;

  std::vector<diag_file_t> jsonl_sessions;
  std::vector<diag_file_t> rings;
  std::vector<diag_file_t> bundles;
  std::vector<std::filesystem::path> rotated_ones;

  for (const auto &entry : std::filesystem::directory_iterator(diag_dir_, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_regular_file(ec) || ec) {
      continue;
    }
    const auto name = entry.path().filename().string();
    const auto mtime = entry.last_write_time(ec);
    if (ec) {
      continue;
    }
    const auto size = entry.file_size(ec);
    if (ec) {
      continue;
    }

    if (name.starts_with("sunshine-diag-") && name.ends_with(".zip")) {
      bundles.push_back({entry.path(), mtime, size});
    }
    else if (name.ends_with(".ring.jsonl")) {
      rings.push_back({entry.path(), mtime, size});
    }
    else if (name.ends_with(".jsonl.1")) {
      rotated_ones.push_back(entry.path());
    }
    else if (name.ends_with(".jsonl")) {
      jsonl_sessions.push_back({entry.path(), mtime, size});
    }
  }

  std::sort(jsonl_sessions.begin(), jsonl_sessions.end(), mtime_newer);
  std::sort(rings.begin(), rings.end(), mtime_newer);
  std::sort(bundles.begin(), bundles.end(), mtime_newer);

  auto remove_file = [&](const std::filesystem::path &p) {
    std::error_code rec;
    std::filesystem::remove(p, rec);
  };

  // Keep newest N session JSONL files; delete extras and their .1 rotations together.
  for (std::size_t i = k_diag_retain_sessions; i < jsonl_sessions.size(); ++i) {
    remove_file(jsonl_sessions[i].path);
    remove_file(std::filesystem::path(jsonl_sessions[i].path.string() + ".1"));
  }
  if (jsonl_sessions.size() > k_diag_retain_sessions) {
    jsonl_sessions.resize(k_diag_retain_sessions);
  }

  for (std::size_t i = k_diag_retain_ring_dumps; i < rings.size(); ++i) {
    remove_file(rings[i].path);
  }
  if (rings.size() > k_diag_retain_ring_dumps) {
    rings.resize(k_diag_retain_ring_dumps);
  }

  for (std::size_t i = k_diag_retain_bundles; i < bundles.size(); ++i) {
    remove_file(bundles[i].path);
  }
  if (bundles.size() > k_diag_retain_bundles) {
    bundles.resize(k_diag_retain_bundles);
  }

  // Drop orphaned .jsonl.1 whose base session file is gone.
  for (const auto &p1 : rotated_ones) {
    const auto s = p1.string();
    if (!s.ends_with(".jsonl.1")) {
      continue;
    }
    const auto base = std::filesystem::path(s.substr(0, s.size() - 2));  // strip trailing ".1"
    if (!std::filesystem::exists(base, ec)) {
      remove_file(p1);
    }
  }

  auto matching_total_bytes = [&]() -> std::uintmax_t {
    std::uintmax_t total = 0;
    auto add_if_exists = [&](const std::filesystem::path &p) {
      std::error_code fec;
      if (std::filesystem::exists(p, fec) && !fec) {
        const auto sz = std::filesystem::file_size(p, fec);
        if (!fec) {
          total += sz;
        }
      }
    };
    for (const auto &f : jsonl_sessions) {
      add_if_exists(f.path);
      add_if_exists(std::filesystem::path(f.path.string() + ".1"));
    }
    for (const auto &f : rings) {
      add_if_exists(f.path);
    }
    for (const auto &f : bundles) {
      add_if_exists(f.path);
    }
    return total;
  };

  // Soft size cap: delete oldest bundles first, then oldest jsonl/ring.
  while (matching_total_bytes() > k_diag_dir_max_bytes) {
    if (!bundles.empty()) {
      remove_file(bundles.back().path);
      bundles.pop_back();
      continue;
    }
    if (jsonl_sessions.empty() && rings.empty()) {
      break;
    }
    const bool drop_jsonl = rings.empty() ||
      (!jsonl_sessions.empty() && jsonl_sessions.back().mtime <= rings.back().mtime);
    if (drop_jsonl) {
      remove_file(jsonl_sessions.back().path);
      remove_file(std::filesystem::path(jsonl_sessions.back().path.string() + ".1"));
      jsonl_sessions.pop_back();
    }
    else {
      remove_file(rings.back().path);
      rings.pop_back();
    }
  }
}

std::string session_timeline_t::begin_session() {
  auto id = uuid_util::uuid_t::generate().string();
  std::lock_guard lg(mutex_);
  if (!enabled_) {
    active_session_id_ = id;
    return id;
  }
  ensure_dir_unlocked();
  prune_diagnostics_dir_unlocked();
  active_session_id_ = id;
  rings_[id] = {};
  last_category_[id] = failure_category::none;
  first_frame_seen_[id] = false;
  last_frame_number_[id] = 0;
  video_format_[id] = -1;
  last_frame_emitted_[id] = false;

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
  bool had_frame = false;
  std::uint64_t last_fn = 0;
  {
    std::lock_guard lg(mutex_);
    auto fit = first_frame_seen_.find(session_id);
    had_frame = fit != first_frame_seen_.end() && fit->second;
    auto lit = last_frame_number_.find(session_id);
    if (lit != last_frame_number_.end()) {
      last_fn = lit->second;
    }
  }
  bool already_emitted = false;
  {
    std::lock_guard lg(mutex_);
    auto eit = last_frame_emitted_.find(session_id);
    already_emitted = eit != last_frame_emitted_.end() && eit->second;
  }
  if (had_frame && !already_emitted) {
    emit(session_id, "last_frame", nlohmann::json {{"frame_number", last_fn}});
    std::lock_guard lg(mutex_);
    last_frame_emitted_[session_id] = true;
  }
  nlohmann::json fields {{"reason", std::string(reason)}};
  if (had_frame) {
    fields["last_frame_number"] = last_fn;
    fields["first_frame_seen"] = true;
  }
  emit(session_id, "session_end", std::move(fields));
  std::lock_guard lg(mutex_);
  if (active_session_id_ == session_id) {
    active_session_id_.clear();
  }
  // Keep disk files for the retention window; free per-session memory.
  rings_.erase(session_id);
  last_category_.erase(session_id);
  last_jsonl_coalesce_.erase(session_id);
  first_frame_seen_.erase(session_id);
  last_frame_number_.erase(session_id);
  video_format_.erase(session_id);
  last_frame_emitted_.erase(session_id);
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

void session_timeline_t::write_jsonl_line_unlocked(const std::string &session_id, const timeline_event_t &ev) {
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

void session_timeline_t::append_jsonl_unlocked(const std::string &session_id, const timeline_event_t &ev) {
  ensure_dir_unlocked();
  const auto path = diag_dir_ / (session_id + ".jsonl");

  std::error_code ec;
  if (std::filesystem::exists(path, ec) && !ec) {
    const auto sz = std::filesystem::file_size(path, ec);
    if (!ec && sz >= k_diag_jsonl_max_bytes) {
      const auto rotated = diag_dir_ / (session_id + ".jsonl.1");
      std::filesystem::remove(rotated, ec);
      std::filesystem::rename(path, rotated, ec);
      timeline_event_t marker {
        std::chrono::system_clock::now(),
        std::chrono::steady_clock::now(),
        "jsonl_rotated",
        nlohmann::json {
          {"previous_bytes", sz},
          {"max_bytes", k_diag_jsonl_max_bytes},
        },
      };
      write_jsonl_line_unlocked(session_id, marker);
    }
  }

  write_jsonl_line_unlocked(session_id, ev);
}

void session_timeline_t::emit(const std::string &session_id, std::string_view type, nlohmann::json fields, bool persist) {
  if (session_id.empty()) {
    return;
  }
  std::lock_guard lg(mutex_);
  if (!enabled_) {
    return;
  }
  const auto now_wall = std::chrono::system_clock::now();
  const auto now_mono = std::chrono::steady_clock::now();
  auto &ring = rings_[session_id];
  const bool ring_coalesce = is_ring_coalesced_type(type);
  const bool jsonl_rate_limit = is_jsonl_rate_limited_type(type);

  if (ring_coalesce && !ring.empty() && ring.back().type == type) {
    // Update in place: one live coalesced entry for this high-frequency type.
    ring.back().wall_time = now_wall;
    ring.back().mono_time = now_mono;
    if (!ring.back().fields.is_object()) {
      ring.back().fields = nlohmann::json::object();
    }
    // Merge latest fields (keep count separately).
    if (fields.is_object()) {
      for (auto it = fields.begin(); it != fields.end(); ++it) {
        if (it.key() == "count") {
          continue;
        }
        ring.back().fields[it.key()] = it.value();
      }
    }
    const auto prev = ring.back().fields.value("count", 1);
    ring.back().fields["count"] = prev + 1;

    // Rate-limited JSONL: at most every k_diag_jsonl_coalesce_interval while streak continues.
    // control_ping stays ring-only (persist=false from caller; not in jsonl_rate_limit set).
    if (persist && jsonl_rate_limit) {
      auto &last_map = last_jsonl_coalesce_[session_id];
      auto lit = last_map.find(std::string(type));
      const bool due = (lit == last_map.end()) ||
        ((now_mono - lit->second) >= k_diag_jsonl_coalesce_interval);
      if (due) {
        append_jsonl_unlocked(session_id, ring.back());
        last_map[std::string(type)] = now_mono;
      }
    }

    prune_ring_unlocked(session_id, now_mono);
    return;
  }

  timeline_event_t ev {now_wall, now_mono, std::string(type), std::move(fields)};
  if (ring_coalesce) {
    if (!ev.fields.is_object()) {
      ev.fields = nlohmann::json::object();
    }
    if (!ev.fields.contains("count")) {
      ev.fields["count"] = 1;
    }
  }
  if (persist) {
    append_jsonl_unlocked(session_id, ev);
    if (jsonl_rate_limit) {
      last_jsonl_coalesce_[session_id][std::string(type)] = now_mono;
    }
  }
  ring.push_back(std::move(ev));
  prune_ring_unlocked(session_id, now_mono);
}

void session_timeline_t::emit_active(std::string_view type, nlohmann::json fields, bool persist) {
  std::string id;
  {
    std::lock_guard lg(mutex_);
    id = active_session_id_;
  }
  emit(id, type, std::move(fields), persist);
}

void session_timeline_t::set_capture_meta(int video_format) {
  std::lock_guard lg(mutex_);
  if (active_session_id_.empty()) {
    return;
  }
  video_format_[active_session_id_] = video_format;
}

void session_timeline_t::note_successful_frame(std::uint64_t frame_number) {
  std::string id;
  bool emit_first = false;
  int vf = -1;
  {
    std::lock_guard lg(mutex_);
    if (!enabled_ || active_session_id_.empty()) {
      return;
    }
    id = active_session_id_;
    last_frame_number_[id] = frame_number;
    auto vit = video_format_.find(id);
    if (vit != video_format_.end()) {
      vf = vit->second;
    }
    auto &seen = first_frame_seen_[id];
    if (!seen) {
      seen = true;
      emit_first = true;
    }
  }
  if (emit_first) {
    nlohmann::json fields {{"frame_number", frame_number}};
    if (vf >= 0) {
      fields["videoFormat"] = vf;
    }
    emit(id, "first_frame", std::move(fields));
  }
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
  bool had_frame = false;
  std::uint64_t last_fn = 0;
  {
    auto fit = first_frame_seen_.find(session_id);
    had_frame = fit != first_frame_seen_.end() && fit->second;
    auto lit = last_frame_number_.find(session_id);
    if (lit != last_frame_number_.end()) {
      last_fn = lit->second;
    }
  }
  // Emit last_frame into ring/JSONL once on failure path (not per-frame spam).
  if (had_frame && !last_frame_emitted_[session_id]) {
    timeline_event_t last_ev {
      std::chrono::system_clock::now(),
      std::chrono::steady_clock::now(),
      "last_frame",
      nlohmann::json {{"frame_number", last_fn}},
    };
    append_jsonl_unlocked(session_id, last_ev);
    rings_[session_id].push_back(last_ev);
    last_frame_emitted_[session_id] = true;
  }
  nlohmann::json header {
    {"type", "ring_dump"},
    {"session_id", session_id},
    {"category", category_name(category)},
    {"window_ms", ring_window_.count()},
    {"ts", iso8601(std::chrono::system_clock::now())},
  };
  if (had_frame) {
    header["last_frame_number"] = last_fn;
    header["first_frame_seen"] = true;
  }
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
  prune_diagnostics_dir_unlocked();
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
  j["jsonl_max_bytes"] = k_diag_jsonl_max_bytes;
  j["diag_retain_sessions"] = k_diag_retain_sessions;
  j["diag_dir_max_bytes"] = k_diag_dir_max_bytes;
  if (!active_session_id_.empty()) {
    auto cat_it = last_category_.find(active_session_id_);
    if (cat_it != last_category_.end()) {
      j["last_category"] = category_name(cat_it->second);
    }
    auto ff = first_frame_seen_.find(active_session_id_);
    j["first_frame_seen"] = ff != first_frame_seen_.end() && ff->second;
    auto lf = last_frame_number_.find(active_session_id_);
    if (lf != last_frame_number_.end()) {
      j["last_frame_number"] = lf->second;
    }
    auto ring_it = rings_.find(active_session_id_);
    j["ring_event_count"] = ring_it == rings_.end() ? 0 : ring_it->second.size();
    if (ring_it != rings_.end()) {
      const auto now = std::chrono::steady_clock::now();
      for (auto rit = ring_it->second.rbegin(); rit != ring_it->second.rend(); ++rit) {
        if (rit->type == "control_ping_received") {
          j["control_ping_age_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(now - rit->mono_time).count();
          j["control_ping_count"] = rit->fields.value("count", 1);
          break;
        }
      }
    }
  }
  return j;
}

}  // namespace spotcobuild
