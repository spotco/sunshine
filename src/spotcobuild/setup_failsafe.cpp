/**
 * @file src/spotcobuild/setup_failsafe.cpp
 * @brief Startup readiness + sticky-BUSY / incomplete-raise fail-safe.
 */
#include "setup_failsafe.h"

#include "session_timeline.h"

#include "src/config.h"
#include "src/globals.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/process.h"
#include "src/rtsp.h"
#include "src/task_pool.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace spotcobuild {
namespace {

std::mutex g_mu;
bool g_audio_raised = false;
bool g_video_raised = false;
bool g_first_frame = false;
bool g_session_active = false;
bool g_control_connected = false;
bool g_display_ready = true;
bool g_encoder_ready = true;
std::string g_last_busy_clear_reason;
std::string g_last_channel_incomplete;
std::chrono::steady_clock::time_point g_last_raise_audio {};
std::chrono::steady_clock::time_point g_last_raise_video {};
std::chrono::steady_clock::time_point g_last_session_end {};
std::uint64_t g_raise_generation = 0;
thread_pool_util::ThreadPool::task_id_t g_grace_task {};
bool g_grace_armed = false;

int grace_ms() {
  return config::diag.channel_raise_grace_ms > 0 ? config::diag.channel_raise_grace_ms : 15000;
}

int startup_wait_ms() {
  return config::diag.startup_display_wait_ms > 0 ? config::diag.startup_display_wait_ms : 30000;
}

bool require_display() {
  return config::diag.require_display_before_launch;
}

bool displays_present() {
  try {
    auto names = platf::display_names(platf::mem_type_e::dxgi);
    return !names.empty();
  }
  catch (...) {
    return false;
  }
}

void clear_busy_unlocked(std::string_view reason, bool tear_down_sessions) {
  const int running = proc::proc.running();
  if (running <= 0) {
    return;
  }
  BOOST_LOG(warning) << "spotcobuild: clearing sticky BUSY after setup failure reason="sv << reason
                     << " app_id="sv << running
                     << " tear_down_sessions="sv << (tear_down_sessions ? "true" : "false");
  // IMPORTANT: clear placebo FIRST. Calling terminate_sessions() first can block for
  // the full video recv_ping timeout ("Waiting for video to end...") and leave BUSY
  // stuck while Moonlight only sees a silent fail /resume.
  if (proc::proc.running() > 0) {
    proc::proc.terminate();
  }
  g_last_busy_clear_reason = std::string(reason);
  session_timeline_t::instance().emit_active("busy_auto_cleared", nlohmann::json {
    {"reason", std::string(reason)},
    {"app_id", running},
  });
  // Sessions are best-effort after BUSY is clear; never block the grace/watchdog path on join.
  if (tear_down_sessions) {
    try {
      rtsp_stream::terminate_sessions();
    }
    catch (...) {
      BOOST_LOG(error) << "spotcobuild: terminate_sessions threw during busy clear"sv;
    }
  }
}

void grace_fire(std::uint64_t generation) {
  std::string reason;
  {
    std::lock_guard lg(g_mu);
    if (generation != g_raise_generation) {
      return;
    }
    if (g_video_raised || g_first_frame || !g_session_active || !g_control_connected) {
      // No abort until control is up — video ping only starts after ENet control succeeds.
      g_grace_armed = false;
      return;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - g_last_raise_audio).count();
    g_last_channel_incomplete = "audio_without_video";
    reason = "channel_raise_incomplete";
    session_timeline_t::instance().emit_active("channel_raise_incomplete", nlohmann::json {
      {"audio_raised", true},
      {"video_raised", false},
      {"elapsed_ms", elapsed},
      {"grace_ms", grace_ms()},
    });
    g_grace_armed = false;
  }
  BOOST_LOG(error) << "spotcobuild: channel raise incomplete (audio without video); aborting session"sv;
  // Do not hold g_mu across terminate/session teardown (can deadlock with session_end).
  clear_busy_unlocked(reason, true);
}

void arm_grace_unlocked() {
  // VideoPing starts only after control ENet connects. Arming on early audio RAISE
  // alone races the client and tears down control mid-handshake (Moonlight Error 2).
  if (g_grace_armed || g_video_raised || g_first_frame || !g_control_connected) {
    return;
  }
  g_grace_armed = true;
  const auto gen = g_raise_generation;
  const auto delay = std::chrono::milliseconds {grace_ms()};
  g_grace_task = task_pool.pushDelayed([gen]() { grace_fire(gen); }, delay).task_id;
}

void busy_watchdog_tick();
void arm_busy_watchdog() {
  task_pool.pushDelayed([]() {
    busy_watchdog_tick();
    arm_busy_watchdog();
  }, 10s);
}

void busy_watchdog_tick() {
  std::lock_guard lg(g_mu);
  if (g_session_active) {
    return;
  }
  if (proc::proc.running() <= 0) {
    return;
  }
  if (g_last_session_end.time_since_epoch().count() == 0) {
    return;
  }
  const auto idle = std::chrono::steady_clock::now() - g_last_session_end;
  if (idle < std::chrono::seconds {20}) {
    return;
  }
  clear_busy_unlocked("busy_watchdog_idle", false);
}

}  // namespace

void setup_failsafe_init() {
  BOOST_LOG(info) << "spotcobuild: setup failsafe init require_display="sv
                  << (require_display() ? "true" : "false")
                  << " startup_wait_ms="sv << startup_wait_ms()
                  << " channel_raise_grace_ms="sv << grace_ms();

  const auto boot_id = session_timeline_t::instance().begin_session();
  session_timeline_t::instance().emit(boot_id, "startup_display_wait", nlohmann::json {
    {"require_display", require_display()},
    {"wait_ms", startup_wait_ms()},
  }, true);

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds {startup_wait_ms()};
  bool ready = displays_present();
  while (!ready && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds {500});
    ready = displays_present();
  }

  {
    std::lock_guard lg(g_mu);
    g_display_ready = ready || !require_display();
    g_encoder_ready = ready;
  }

  if (ready) {
    BOOST_LOG(info) << "spotcobuild: startup display ready"sv;
    session_timeline_t::instance().emit(boot_id, "startup_display_ready", nlohmann::json {
      {"display_count_nonzero", true},
    });
  }
  else {
    BOOST_LOG(warning) << "spotcobuild: startup display timeout / empty enumeration"sv;
    session_timeline_t::instance().emit(boot_id, "startup_display_timeout", nlohmann::json {
      {"require_display", require_display()},
    });
  }
  session_timeline_t::instance().end_session(boot_id, "startup");

  arm_busy_watchdog();
}

void note_channel_raised(std::string_view channel) {
  std::lock_guard lg(g_mu);
  const auto now = std::chrono::steady_clock::now();
  if (channel == "audio") {
    g_audio_raised = true;
    g_last_raise_audio = now;
    arm_grace_unlocked();
  }
  else if (channel == "video") {
    g_video_raised = true;
    g_last_raise_video = now;
    if (g_grace_armed) {
      task_pool.cancel(g_grace_task);
      g_grace_armed = false;
    }
  }
}

void note_control_connected() {
  std::lock_guard lg(g_mu);
  g_control_connected = true;
  BOOST_LOG(info) << "spotcobuild: control connected; audio_raised="sv << (g_audio_raised ? "true" : "false")
                  << " video_raised="sv << (g_video_raised ? "true" : "false");
  session_timeline_t::instance().emit_active("control_connected", nlohmann::json {
    {"audio_raised", g_audio_raised},
    {"video_raised", g_video_raised},
  });
  if (g_audio_raised && !g_video_raised && !g_first_frame) {
    arm_grace_unlocked();
  }
}

void note_setup_first_frame() {
  std::lock_guard lg(g_mu);
  g_first_frame = true;
  if (g_grace_armed) {
    task_pool.cancel(g_grace_task);
    g_grace_armed = false;
  }
}

void note_setup_session_begin() {
  std::lock_guard lg(g_mu);
  ++g_raise_generation;
  g_audio_raised = false;
  g_video_raised = false;
  g_first_frame = false;
  g_control_connected = false;
  g_session_active = true;
  if (g_grace_armed) {
    task_pool.cancel(g_grace_task);
    g_grace_armed = false;
  }
}

void note_setup_session_end(std::string_view reason) {
  std::lock_guard lg(g_mu);
  g_session_active = false;
  g_last_session_end = std::chrono::steady_clock::now();
  if (g_grace_armed) {
    task_pool.cancel(g_grace_task);
    g_grace_armed = false;
  }
  if (!g_first_frame) {
    // Already inside session::join — do not terminate_sessions (would re-enter join).
    clear_busy_unlocked(std::string(reason).empty() ? "setup_incomplete" : std::string(reason), false);
  }
}

void clear_busy_after_setup_failure(std::string_view reason) {
  std::lock_guard lg(g_mu);
  if (g_first_frame) {
    return;
  }
  g_session_active = false;
  g_last_session_end = std::chrono::steady_clock::now();
  clear_busy_unlocked(reason, true);
}

bool is_launch_display_ready() {
  std::lock_guard lg(g_mu);
  if (!require_display()) {
    return true;
  }
  return g_display_ready;
}

bool ensure_launch_ready(bool wait_with_backoff) {
  if (!require_display()) {
    std::lock_guard lg(g_mu);
    g_display_ready = true;
    return true;
  }

  bool ready = displays_present();
  if (!ready && wait_with_backoff) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds {5};
    while (!ready && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds {250});
      ready = displays_present();
    }
  }

  {
    std::lock_guard lg(g_mu);
    g_display_ready = ready;
    g_encoder_ready = ready;
  }

  if (!ready) {
    BOOST_LOG(warning) << "spotcobuild: launch blocked — no display outputs enumerated"sv;
  }
  return ready;
}

nlohmann::json setup_failsafe_health_snapshot() {
  std::lock_guard lg(g_mu);
  const auto now = std::chrono::steady_clock::now();
  auto ms_since = [&](std::chrono::steady_clock::time_point tp) -> std::int64_t {
    if (tp.time_since_epoch().count() == 0) {
      return -1;
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - tp).count();
  };
  nlohmann::json j;
  j["display_ready"] = g_display_ready;
  j["encoder_ready"] = g_encoder_ready;
  j["require_display_before_launch"] = require_display();
  j["server_busy"] = proc::proc.running() > 0;
  j["placebo_or_running_app_id"] = proc::proc.running();
  j["session_active"] = g_session_active;
  j["audio_raised"] = g_audio_raised;
  j["video_raised"] = g_video_raised;
  j["first_frame"] = g_first_frame;
  j["last_busy_clear_reason"] = g_last_busy_clear_reason;
  j["last_channel_raise_incomplete"] = g_last_channel_incomplete;
  j["ms_since_last_raise_audio"] = ms_since(g_last_raise_audio);
  j["ms_since_last_raise_video"] = ms_since(g_last_raise_video);
  j["ms_since_last_session_end"] = ms_since(g_last_session_end);
  j["channel_raise_grace_ms"] = grace_ms();
  return j;
}

}  // namespace spotcobuild


