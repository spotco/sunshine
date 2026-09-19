/**
 * @file src/spotcobuild/udp_probe.h
 * @brief Magic UDP probe detect + echo for video/audio/control diagnostics.
 *
 * Do NOT include this from the umbrella header into windows.h TUs.
 * Include from stream.cpp / health.cpp / udp_probe.cpp after platform networking headers.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <boost/asio/ip/udp.hpp>
#include <nlohmann/json.hpp>

namespace spotcobuild {

inline constexpr std::string_view k_udp_probe_prefix = "SPOTCO_UDP_PROBE_V1";
inline constexpr std::string_view k_udp_probe_ack_prefix = "SPOTCO_UDP_PROBE_ACK";

enum class udp_probe_channel_e {
  video,
  audio,
  control,
};

bool handle_udp_probe_datagram(udp_probe_channel_e channel, boost::asio::ip::udp::socket &sock,
  const boost::asio::ip::udp::endpoint &peer, const char *data, std::size_t bytes);

void start_udp_probe_idle_listeners();
void stop_udp_probe_idle_listeners();

nlohmann::json udp_probe_health_snapshot();

}  // namespace spotcobuild
