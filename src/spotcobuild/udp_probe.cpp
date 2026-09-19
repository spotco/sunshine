/**
 * @file src/spotcobuild/udp_probe.cpp
 * @brief Magic UDP probe detect + echo + idle listeners + health counters.
 */
#include "udp_probe.h"

#include "session_timeline.h"

#include "src/logging.h"
#include "src/network.h"
#include "src/platform/common.h"

#include <atomic>
#include <algorithm>
#include <filesystem>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace spotcobuild {
namespace {

using udp = boost::asio::ip::udp;

std::atomic<std::uint64_t> g_video_rx {0};
std::atomic<std::uint64_t> g_audio_rx {0};
std::atomic<std::uint64_t> g_control_rx {0};
std::atomic<std::int64_t> g_last_video_ms {0};
std::atomic<std::int64_t> g_last_audio_ms {0};
std::atomic<std::int64_t> g_last_control_ms {0};
std::string g_last_video_from;
std::string g_last_audio_from;
std::string g_last_control_from;
std::mutex g_from_mu;

std::int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch())
    .count();
}

void note_from(udp_probe_channel_e channel, const udp::endpoint &peer) {
  const auto s = peer.address().to_string() + ":" + std::to_string(peer.port());
  const auto ms = now_ms();
  std::lock_guard lg(g_from_mu);
  switch (channel) {
    case udp_probe_channel_e::video:
      g_video_rx.fetch_add(1, std::memory_order_relaxed);
      g_last_video_ms.store(ms, std::memory_order_relaxed);
      g_last_video_from = s;
      break;
    case udp_probe_channel_e::audio:
      g_audio_rx.fetch_add(1, std::memory_order_relaxed);
      g_last_audio_ms.store(ms, std::memory_order_relaxed);
      g_last_audio_from = s;
      break;
    case udp_probe_channel_e::control:
      g_control_rx.fetch_add(1, std::memory_order_relaxed);
      g_last_control_ms.store(ms, std::memory_order_relaxed);
      g_last_control_from = s;
      break;
  }
}

const char *channel_name(udp_probe_channel_e channel) {
  switch (channel) {
    case udp_probe_channel_e::video:
      return "video";
    case udp_probe_channel_e::audio:
      return "audio";
    case udp_probe_channel_e::control:
      return "control";
  }
  return "unknown";
}

void append_diag_line(udp_probe_channel_e channel, const udp::endpoint &peer, std::size_t bytes, bool echoed) {
  try {
    const auto dir = session_timeline_t::instance().diagnostics_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::ofstream out(dir / "udp_probe.log", std::ios::app);
    if (!out) {
      return;
    }
    out << now_ms() << " channel=" << channel_name(channel)
        << " from=" << peer.address().to_string() << ':' << peer.port()
        << " bytes=" << bytes << " echoed=" << (echoed ? 1 : 0) << '\n';
  } catch (...) {
  }
}

std::vector<char> build_ack(const char *data, std::size_t bytes) {
  std::vector<char> ack;
  ack.reserve(k_udp_probe_ack_prefix.size() + 8);
  ack.insert(ack.end(), k_udp_probe_ack_prefix.begin(), k_udp_probe_ack_prefix.end());
  if (bytes > k_udp_probe_prefix.size()) {
    const auto nonce_len = std::min<std::size_t>(8, bytes - k_udp_probe_prefix.size());
    ack.insert(ack.end(), data + k_udp_probe_prefix.size(), data + k_udp_probe_prefix.size() + nonce_len);
  }
  return ack;
}

bool payload_is_probe(const char *data, std::size_t bytes) {
  return bytes >= k_udp_probe_prefix.size() &&
         std::memcmp(data, k_udp_probe_prefix.data(), k_udp_probe_prefix.size()) == 0;
}

// --- idle listeners (active while broadcast is down) ---
std::mutex g_idle_mu;
std::atomic<bool> g_idle_running {false};
std::unique_ptr<boost::asio::io_context> g_idle_io;
std::unique_ptr<std::thread> g_idle_thread;
std::unique_ptr<udp::socket> g_idle_video;
std::unique_ptr<udp::socket> g_idle_audio;
std::unique_ptr<udp::socket> g_idle_control;

void idle_recv_loop(udp::socket &sock, udp_probe_channel_e channel, std::shared_ptr<std::array<char, 2048>> buf) {
  auto peer = std::make_shared<udp::endpoint>();
  sock.async_receive_from(boost::asio::buffer(*buf), *peer,
    [&sock, channel, buf, peer](const boost::system::error_code &ec, std::size_t bytes) {
      if (!g_idle_running || ec == boost::asio::error::operation_aborted) {
        return;
      }
      if (!ec && bytes) {
        handle_udp_probe_datagram(channel, sock, *peer, buf->data(), bytes);
      }
      if (g_idle_running && sock.is_open()) {
        idle_recv_loop(sock, channel, buf);
      }
    });
}

bool open_idle_sock(std::unique_ptr<udp::socket> &out, std::uint16_t port, const char *label) {
  boost::system::error_code ec;
  auto protocol = udp::v4();
  out = std::make_unique<udp::socket>(*g_idle_io);
  out->open(protocol, ec);
  if (ec) {
    BOOST_LOG(error) << "udp_probe idle: open " << label << " failed: " << ec.message();
    out.reset();
    return false;
  }
  boost::asio::socket_base::reuse_address reuse(true);
  out->set_option(reuse, ec);
  const auto bind_addr = boost::asio::ip::make_address("0.0.0.0", ec);
  out->bind(udp::endpoint(bind_addr, port), ec);
  if (ec) {
    BOOST_LOG(warning) << "udp_probe idle: bind " << label << " port " << port << " failed: " << ec.message();
    out->close();
    out.reset();
    return false;
  }
  BOOST_LOG(info) << "udp_probe idle: listening on " << label << " UDP " << port;
  return true;
}

}  // namespace

bool handle_udp_probe_datagram(udp_probe_channel_e channel, udp::socket &sock, const udp::endpoint &peer,
  const char *data, std::size_t bytes) {
  if (!payload_is_probe(data, bytes)) {
    return false;
  }

  note_from(channel, peer);
  BOOST_LOG(info) << "udp_probe: " << channel_name(channel) << " from "
                   << peer.address().to_string() << ':' << peer.port() << " bytes=" << bytes;

  auto ack = build_ack(data, bytes);
  bool echoed = false;
  boost::system::error_code ec;
  sock.send_to(boost::asio::buffer(ack), peer, 0, ec);
  if (ec) {
    // Fallback via platf::send (Windows path used by stream senders)
    auto peer_address = peer.address();
    auto local_address = sock.local_endpoint(ec).address();
    if (!ec) {
      auto send_info = platf::send_info_t {
        nullptr,
        0,
        ack.data(),
        ack.size(),
        (uintptr_t) sock.native_handle(),
        peer_address,
        peer.port(),
        local_address,
      };
      echoed = platf::send(send_info);
    }
    if (!echoed) {
      BOOST_LOG(warning) << "udp_probe: echo send failed on " << channel_name(channel) << ": " << ec.message();
    }
  } else {
    echoed = true;
  }

  append_diag_line(channel, peer, bytes, echoed);
  return true;
}

void stop_udp_probe_idle_listeners() {
  std::lock_guard lg(g_idle_mu);
  if (!g_idle_running.load()) {
    return;
  }
  g_idle_running.store(false);
  if (g_idle_io) {
    g_idle_io->stop();
  }
  auto close_sock = [](std::unique_ptr<udp::socket> &s) {
    if (s && s->is_open()) {
      boost::system::error_code ec;
      s->close(ec);
    }
    s.reset();
  };
  close_sock(g_idle_video);
  close_sock(g_idle_audio);
  close_sock(g_idle_control);
  if (g_idle_thread && g_idle_thread->joinable()) {
    g_idle_thread->join();
  }
  g_idle_thread.reset();
  g_idle_io.reset();
  BOOST_LOG(info) << "udp_probe idle: stopped";
}

void start_udp_probe_idle_listeners() {
  std::lock_guard lg(g_idle_mu);
  if (g_idle_running.load()) {
    return;
  }
  g_idle_io = std::make_unique<boost::asio::io_context>();
  g_idle_running.store(true);

  // GameStream offsets from HTTP base (same as stream::VIDEO/CONTROL/AUDIO_STREAM_PORT).
  const auto video_port = net::map_port(9);
  const auto audio_port = net::map_port(11);
  const auto control_port = net::map_port(10);

  open_idle_sock(g_idle_video, video_port, "video");
  open_idle_sock(g_idle_audio, audio_port, "audio");
  open_idle_sock(g_idle_control, control_port, "control");

  if (g_idle_video) {
    idle_recv_loop(*g_idle_video, udp_probe_channel_e::video, std::make_shared<std::array<char, 2048>>());
  }
  if (g_idle_audio) {
    idle_recv_loop(*g_idle_audio, udp_probe_channel_e::audio, std::make_shared<std::array<char, 2048>>());
  }
  if (g_idle_control) {
    idle_recv_loop(*g_idle_control, udp_probe_channel_e::control, std::make_shared<std::array<char, 2048>>());
  }

  g_idle_thread = std::make_unique<std::thread>([]() {
    platf::set_thread_name("udp_probe_idle");
    if (g_idle_io) {
      g_idle_io->run();
    }
  });
}

nlohmann::json udp_probe_health_snapshot() {
  nlohmann::json j;
  j["video_rx"] = g_video_rx.load(std::memory_order_relaxed);
  j["audio_rx"] = g_audio_rx.load(std::memory_order_relaxed);
  j["control_rx"] = g_control_rx.load(std::memory_order_relaxed);
  j["last_probe_ms"] = std::max({g_last_video_ms.load(), g_last_audio_ms.load(), g_last_control_ms.load()});
  {
    std::lock_guard lg(g_from_mu);
    j["last_video_from"] = g_last_video_from;
    j["last_audio_from"] = g_last_audio_from;
    j["last_control_from"] = g_last_control_from;
  }
  j["last_video_ms"] = g_last_video_ms.load(std::memory_order_relaxed);
  j["last_audio_ms"] = g_last_audio_ms.load(std::memory_order_relaxed);
  j["last_control_ms"] = g_last_control_ms.load(std::memory_order_relaxed);
  j["idle_listeners"] = g_idle_running.load();
  return j;
}

}  // namespace spotcobuild
