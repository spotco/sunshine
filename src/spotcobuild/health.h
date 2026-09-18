/**
 * @file src/spotcobuild/health.h
 * @brief Health snapshot, CLI diagnostics, idle self-test.
 */
#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace spotcobuild {

nlohmann::json health_snapshot();

/** Print diagnostics to stdout/log; return process exit code. */
int run_diagnostics_cli();

/**
 * Idle-only short capture/encode self-test.
 * Refuses when a stream session is active.
 */
nlohmann::json run_idle_self_test();

}  // namespace spotcobuild
