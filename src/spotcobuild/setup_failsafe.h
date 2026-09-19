/**
 * @file src/spotcobuild/setup_failsafe.h
 * @brief Startup readiness + sticky-BUSY / incomplete-raise fail-safe.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace spotcobuild {

void setup_failsafe_init();
void note_channel_raised(std::string_view channel);
/** Call when ENet control peer is assigned — arms audio-without-video grace only after this. */
void note_control_connected();
void note_setup_first_frame();
void note_setup_session_begin();
void note_setup_session_end(std::string_view reason);
void clear_busy_after_setup_failure(std::string_view reason);
bool is_launch_display_ready();
bool ensure_launch_ready(bool wait_with_backoff = false);
nlohmann::json setup_failsafe_health_snapshot();

}  // namespace spotcobuild
