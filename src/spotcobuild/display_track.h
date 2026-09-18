/**
 * @file src/spotcobuild/display_track.h
 * @brief Display / capture state tracking helpers.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace spotcobuild {

void track_display_change(std::string_view kind, std::string_view display_id, int width, int height,
  double refresh_hz, bool hdr, std::string_view adapter_luid = {}, std::string_view virtual_provider = {});

void track_capture_surface_recreate(std::string_view backend, std::string_view detail);
void track_desktop_duplication_error(std::int64_t hresult, std::string_view stage, bool startup_probe = false);

}  // namespace spotcobuild
