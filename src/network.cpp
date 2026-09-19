/**
 * @file src/network.cpp
 * @brief Definitions for networking related functions.
 */
// standard includes
#include <algorithm>
#include <atomic>
#include <sstream>

// local includes
#include "config.h"
#include "logging.h"
#include "network.h"
#include "utility.h"

using namespace std::literals;

namespace ip = boost::asio::ip;

namespace net {
  /**
   * @brief Pc ips v4.
   */
  std::vector<ip::network_v4> pc_ips_v4 {
    ip::make_network_v4("127.0.0.0/8"sv),
  };
  /**
   * @brief Lan ips v4.
   */
  std::vector<ip::network_v4> lan_ips_v4 {
    ip::make_network_v4("192.168.0.0/16"sv),
    ip::make_network_v4("172.16.0.0/12"sv),
    ip::make_network_v4("10.0.0.0/8"sv),
    ip::make_network_v4("100.64.0.0/10"sv),
    ip::make_network_v4("169.254.0.0/16"sv),
  };

  /**
   * @brief Pc ips v6.
   */
  std::vector<ip::network_v6> pc_ips_v6 {
    ip::make_network_v6("::1/128"sv),
  };
  /**
   * @brief Lan ips v6.
   */
  std::vector<ip::network_v6> lan_ips_v6 {
    ip::make_network_v6("fc00::/7"sv),
    ip::make_network_v6("fe80::/64"sv),
  };

  /**
   * @brief Convert configuration text to a network enum value.
   */
  net_e from_enum_string(const std::string_view &view) {
    if (view == "wan") {
      return WAN;
    }
    if (view == "lan") {
      return LAN;
    }

    return PC;
  }

  /**
   * @brief Convert a Boost address family to Sunshine network enum value.
   */
  net_e from_address(const std::string_view &view) {
    auto addr = normalize_address(ip::make_address(view));

    if (addr.is_v6()) {
      for (auto &range : pc_ips_v6) {
        if (range.hosts().find(addr.to_v6()) != range.hosts().end()) {
          return PC;
        }
      }

      for (auto &range : lan_ips_v6) {
        if (range.hosts().find(addr.to_v6()) != range.hosts().end()) {
          return LAN;
        }
      }
    } else {
      for (auto &range : pc_ips_v4) {
        if (range.hosts().find(addr.to_v4()) != range.hosts().end()) {
          return PC;
        }
      }

      for (auto &range : lan_ips_v4) {
        if (range.hosts().find(addr.to_v4()) != range.hosts().end()) {
          return LAN;
        }
      }
    }

    return WAN;
  }

  /**
   * @brief Convert a network enum value to configuration text.
   */
  std::string_view to_enum_string(net_e net) {
    switch (net) {
      case PC:
        return "pc"sv;
      case LAN:
        return "lan"sv;
      case WAN:
        return "wan"sv;
    }

    // avoid warning
    return "wan"sv;
  }

  af_e af_from_enum_string(const std::string_view &view) {
    if (view == "ipv4") {
      return IPV4;
    }
    if (view == "both") {
      return BOTH;
    }

    // avoid warning
    return BOTH;
  }

  std::string_view af_to_any_address_string(const af_e af) {
    switch (af) {
      case IPV4:
        return "0.0.0.0"sv;
      case BOTH:
        return "::"sv;
    }

    // avoid warning
    return "::"sv;
  }

  std::string get_bind_address(const af_e af) {
    // If bind_address is configured, use it
    if (!config::sunshine.bind_address.empty()) {
      return config::sunshine.bind_address;
    }

    // Otherwise use the wildcard address for the given address family
    return std::string(af_to_any_address_string(af));
  }

  boost::asio::ip::address normalize_address(boost::asio::ip::address address) {
    // Convert IPv6-mapped IPv4 addresses into regular IPv4 addresses
    if (address.is_v6()) {
      auto v6 = address.to_v6();
      if (v6.is_v4_mapped()) {
        return boost::asio::ip::make_address_v4(boost::asio::ip::v4_mapped, v6);
      }
    }

    return address;
  }

  std::string addr_to_normalized_string(boost::asio::ip::address address) {
    return normalize_address(address).to_string();
  }

  std::string addr_to_url_escaped_string(boost::asio::ip::address address) {
    address = normalize_address(address);
    if (address.is_v6()) {
      std::stringstream ss;
      ss << '[' << address.to_string() << ']';
      return ss.str();
    } else {
      return address.to_string();
    }
  }

  int encryption_mode_for_address(boost::asio::ip::address address) {
    auto nettype = net::from_address(address.to_string());
    if (nettype == net::net_e::PC || nettype == net::net_e::LAN) {
      return config::stream.lan_encryption_mode;
    } else {
      return config::stream.wan_encryption_mode;
    }
  }

  /**
   * @brief Create an ENet host with the requested address family.
   */

  // spotcobuild: dump first few ENet RX datagrams (CONNECT diagnose). Return 0 = continue normal handling.
  static int spotcobuild_enet_rx_intercept(ENetHost *host, ENetEvent * /*event*/) {
    static std::atomic<int> dumps {0};
    const int n = dumps.fetch_add(1, std::memory_order_relaxed);
    if (n >= 8) {
      return 0;
    }
    if (!host || !host->receivedData || host->receivedDataLength <= 0) {
      return 0;
    }
    const auto len = (size_t) host->receivedDataLength;
    std::string hex;
    hex.reserve(len * 2);
    static const char *kHex = "0123456789ABCDEF";
    for (size_t i = 0; i < len && i < 64; ++i) {
      const auto b = (unsigned char) host->receivedData[i];
      hex.push_back(kHex[b >> 4]);
      hex.push_back(kHex[b & 0xF]);
    }
    const auto cmd = len > 4 ? (unsigned) (host->receivedData[4] & 0x0F) : 0u;
    BOOST_LOG(info) << "spotcobuild: enet RX#"sv << n
                    << " len="sv << len
                    << " cmd_lo_nibble="sv << cmd
                    << " (2=CONNECT) hex="sv << hex
                    << (len > 64 ? "..." : "");
    return 0;
  }

  host_t host_create(af_e af, ENetAddress &addr, std::uint16_t port) {
    static std::once_flag enet_init_flag;
    std::call_once(enet_init_flag, []() {
      enet_initialize();
    });

    const auto bind_addr = net::get_bind_address(af);
    enet_address_set_host(&addr, bind_addr.c_str());
    enet_address_set_port(&addr, port);

    // Maximum of 128 clients, which should be enough for anyone
    auto host = host_t {enet_host_create(af == IPV4 ? AF_INET : AF_INET6, &addr, 128, 0, 0, 0)};

    if (!host) {
      BOOST_LOG(error) << "spotcobuild: ENet host_create FAILED addr="sv << bind_addr << " port="sv << port;
      return host;
    }

    // spotcobuild: disable QoS/ECN on Win10+ for LAN reliability (ECN-marked ENet can be dropped on path)
    enet_socket_set_option(host->socket, ENET_SOCKOPT_QOS, 0);
    // spotcobuild: wildcardBind makes VERIFY_CONNECT replies use WSASendMsg+IP_PKTINFO with the
    // recv localAddress. When that is 0.0.0.0 / missing pktinfo, WSASendMsg fails (10022), the
    // client never gets VERIFY, keeps retransmitting CONNECT (6x52B), and the host never reaches
    // ENET_EVENT_TYPE_CONNECT. Force plain sendto like Moonlight's ENet host path.
    host->wildcardBind = 0;
    host->intercept = spotcobuild_enet_rx_intercept;
    BOOST_LOG(info) << "spotcobuild: ENet host_create bind addr="sv << bind_addr
                    << " port="sv << port
                    << " socket_ok="sv << (host->socket != ENET_SOCKET_NULL)
                    << " qos=0 wildcardBind=0 intercept=1"sv;

    return host;
  }

  /**
   * @brief Destroy an ENet host allocated by host_create().
   */
  void free_host(ENetHost *host) {
    std::for_each(host->peers, host->peers + host->peerCount, [](ENetPeer &peer_ref) {
      ENetPeer *peer = &peer_ref;

      if (peer) {
        enet_peer_disconnect_now(peer, 0);
      }
    });

    enet_host_destroy(host);
  }

  std::uint16_t map_port(int port) {
    // calculate the port from the config port
    auto mapped_port = (std::uint16_t) ((int) config::sunshine.port + port);

    // Ensure port is in the range of 1024-65535
    if (mapped_port < 1024 || mapped_port > 65535) {
      BOOST_LOG(warning) << "Port out of range: "sv << mapped_port;
    }

    return mapped_port;
  }

  /**
   * @brief Returns a string for use as the instance name for mDNS.
   * @param hostname The hostname to use for instance name generation.
   * @return Hostname-based instance name or "Sunshine" if hostname is invalid.
   */
  std::string mdns_instance_name(const std::string_view &hostname) {
    // Start with the unmodified hostname
    std::string instancename {hostname.data(), hostname.size()};

    // Truncate to 63 characters per RFC 6763 section 7.2.
    if (instancename.size() > 63) {
      instancename.resize(63);
    }

    for (auto i = 0; i < instancename.size(); i++) {
      // Replace any spaces with dashes
      if (instancename[i] == ' ') {
        instancename[i] = '-';
      } else if (!std::isalnum(instancename[i]) && instancename[i] != '-') {
        // Stop at the first invalid character
        instancename.resize(i);
        break;
      }
    }

    return !instancename.empty() ? instancename : "Sunshine";
  }
}  // namespace net

