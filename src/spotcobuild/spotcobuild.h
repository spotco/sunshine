/**
 * @file src/spotcobuild/spotcobuild.h
 * @brief Umbrella header for spotcobuild stream diagnostics and recovery.
 */
#pragma once

#include "categories.h"
#include "diag_bundle.h"
#include "diag_switches.h"
#include "display_track.h"
#include "error_classify.h"
#include "health.h"
#include "network_timeline.h"
#include "setup_failsafe.h"
#include "recovery.h"
#include "redact.h"
#include "session_timeline.h"
#include "tdr_correlate.h"

namespace spotcobuild {

void start_udp_probe_idle_listeners();
void stop_udp_probe_idle_listeners();


/** Apply config-backed switches and prepare diagnostics directory. */
inline void init_from_config() {
  start_udp_probe_idle_listeners();
  diag_switches_t::instance().apply_from_config();
}

}  // namespace spotcobuild

