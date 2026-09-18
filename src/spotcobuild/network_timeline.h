/**
 * @file src/spotcobuild/network_timeline.h
 * @brief Network-side timeline helpers.
 */
#pragma once

#include <cstdint>
#include <string_view>

namespace spotcobuild {

void track_network_event(std::string_view type, std::string_view channel = {}, std::int64_t error_code = 0,
  std::string_view detail = {});

}  // namespace spotcobuild
