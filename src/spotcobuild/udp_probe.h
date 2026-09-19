/**
 * @file src/spotcobuild/udp_probe.h
 * @brief Magic UDP probe detect + echo for video/audio/control diagnostics.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>

#include <boost/asio.hpp>
#include <nlohmann/json.hpp>

namespace spotcobuild {

inline constexpr std::string_view k_udp_probe_prefix = "SPOTCO_UDP_PROBE_V1";
inline constexpr std::string_view k_udp_probe_ack_prefix = "SPOTCO_UDP_PROBE_ACK";

enum class udp_probe_channel_e {
  video,
  audio,
  control,
};

/**
 * If payload starts with SPOTCO_UDP_PROBE_V1: log (never rate-limit), bump counters,
 * and send SPOTCO_UDP_PROBE_ACK[+nonce] back on the same socket to the datagram source.
 * @return true when the datagram was a probe (caller must not RAISE as stream ping).
 */
bool handle_udp_probe_datagram(udp_probe_channel_e channel, boost::asio::ip::udp::socket &sock,
  const boost::asio::ip::udp::endpoint &peer, const char *data, std::size_t bytes);

/** Idle listeners so probes work while SERVER_FREE (no broadcast sockets). */
void start_udp_probe_idle_listeners();
void stop_udp_probe_idle_listeners();

nlohmann::json udp_probe_health_snapshot();

}  // namespace spotcobuild
