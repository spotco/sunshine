/**
 * @file src/spotcobuild/health.cpp
 * @brief Health snapshot, CLI diagnostics, idle self-test.
 */
#include <cstdio>
#include "health.h"

#include "diag_bundle.h"
#include "diag_switches.h"
#include "recovery.h"
#include "session_timeline.h"
#include "tdr_correlate.h"
#include "udp_probe.h"

#include "src/logging.h"
#include "src/platform/common.h"


#include <chrono>
#include <iostream>

namespace spotcobuild {
namespace {

std::chrono::steady_clock::time_point g_start = std::chrono::steady_clock::now();

}  // namespace

nlohmann::json health_snapshot() {
  const auto uptime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - g_start).count();
  nlohmann::json j;
  j["version"] = PROJECT_VERSION;
  j["uptime_seconds"] = uptime;
  j["timeline"] = session_timeline_t::instance().health_snapshot();
  j["switches"] = diag_switches_t::instance().effective_snapshot();
  j["recovery"] = recovery_controller_t::instance().health_snapshot();
  j["tdr"] = last_tdr_correlation().to_json();
  j["active_session_id"] = session_timeline_t::instance().active_session_id();
  j["udp_probe"] = udp_probe_health_snapshot();
  return j;
}

int run_diagnostics_cli() {
  auto snap = health_snapshot();
  std::cout << snap.dump(2) << std::endl;
  auto bundle = export_diagnostic_bundle();
  if (bundle.ok) {
    std::cout << "bundle=" << bundle.path.string() << std::endl;
    return 0;
  }
  std::cout << "bundle_error=" << bundle.error << std::endl;
  return 1;
}

nlohmann::json run_idle_self_test() {
  nlohmann::json result;
  const auto active = session_timeline_t::instance().active_session_id();
  if (!active.empty()) {
    result["ok"] = false;
    result["error"] = "refusing idle self-test while session is active";
    result["active_session_id"] = active;
    return result;
  }

  // Lightweight self-test: timeline emit + ring dump + classify path without touching GPU.
  const auto id = session_timeline_t::instance().begin_session();
  session_timeline_t::instance().emit(id, "self_test_begin", {{"mode", "idle"}});
  recovery_controller_t::instance().reset_for_test();
  recovery_controller_t::instance().inject_fake_device_lost_for_test(id);
  session_timeline_t::instance().dump_ring_on_failure(id, failure_category::encoder_device_lost);
  session_timeline_t::instance().end_session(id, "self_test");

  result["ok"] = true;
  result["session_id"] = id;
  result["recovery"] = recovery_controller_t::instance().health_snapshot();
  result["note"] = "Logical self-test only; GPU capture/encode probe is best-effort on host.";
  return result;
}

}  // namespace spotcobuild
