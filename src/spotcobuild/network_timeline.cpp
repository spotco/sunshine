/**
 * @file src/spotcobuild/network_timeline.cpp
 * @brief Network-side timeline helpers.
 */
#include "network_timeline.h"

#include "error_classify.h"
#include "session_timeline.h"

namespace spotcobuild {

void track_network_event(std::string_view type, std::string_view channel, std::int64_t error_code, std::string_view detail) {
  nlohmann::json fields {
    {"channel", std::string(channel)},
    {"detail", std::string(detail)},
  };
  if (error_code != 0) {
    fields["error_code"] = error_code;
  }
  session_timeline_t::instance().emit_active(type, fields);
  if (error_code != 0 && (type == "packet_send_fail" || type == "udp_bind_fail" || type == "network_timeout")) {
    classify_network_failure(type, error_code);
  }
}

}  // namespace spotcobuild
