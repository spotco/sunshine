/**
 * @file src/spotcobuild/network_timeline.cpp
 * @brief Network-side timeline helpers.
 */
#include "network_timeline.h"

#include "error_classify.h"
#include "session_timeline.h"

#include <mutex>
#include <unordered_set>

namespace spotcobuild {
namespace {
std::mutex g_classify_mu;
std::string g_classify_session;
std::unordered_set<std::int64_t> g_classified_codes;
}

void track_network_event(std::string_view type, std::string_view channel, std::int64_t error_code, std::string_view detail) {
  nlohmann::json fields {
    {"channel", std::string(channel)},
    {"detail", std::string(detail)},
  };
  if (error_code != 0) {
    fields["error_code"] = error_code;
  }
  // High-frequency pings: ring-only (coalesced), never JSONL spam.
  const bool persist = !(
    type == "control_ping_received" ||
    type == "video_ping_received" ||
    type == "audio_ping_received"
  );
  session_timeline_t::instance().emit_active(type, fields, persist);

  if (error_code != 0 && (type == "packet_send_fail" || type == "udp_bind_fail" || type == "network_timeout")) {
    bool should_classify = false;
    {
      std::lock_guard lg(g_classify_mu);
      const auto sid = session_timeline_t::instance().active_session_id();
      if (sid != g_classify_session) {
        g_classify_session = sid;
        g_classified_codes.clear();
      }
      if (g_classified_codes.insert(error_code).second) {
        should_classify = true;
      }
    }
    if (should_classify) {
      classify_network_failure(type, error_code);
    }
  }
}

}  // namespace spotcobuild
