/**
 * @file src/spotcobuild/display_track.cpp
 * @brief Display / capture state tracking helpers.
 */
#include "display_track.h"

#include "error_classify.h"
#include "session_timeline.h"

namespace spotcobuild {

void track_display_change(std::string_view kind, std::string_view display_id, int width, int height,
  double refresh_hz, bool hdr, std::string_view adapter_luid, std::string_view virtual_provider) {
  nlohmann::json fields {
    {"kind", std::string(kind)},
    {"display_id", std::string(display_id)},
    {"width", width},
    {"height", height},
    {"refresh_hz", refresh_hz},
    {"hdr", hdr},
    {"adapter_luid", std::string(adapter_luid)},
    {"virtual_provider", std::string(virtual_provider)},
  };
  session_timeline_t::instance().emit_active("display_change", std::move(fields));
  if (kind == "mode_change" || kind == "hotplug") {
    classify_display_mode_change(kind);
  }
}

void track_capture_surface_recreate(std::string_view backend, std::string_view detail) {
  session_timeline_t::instance().emit_active("capture_surface_recreate", nlohmann::json {
    {"backend", std::string(backend)},
    {"detail", std::string(detail)},
  });
}

void track_desktop_duplication_error(std::int64_t hresult, std::string_view stage, bool startup_probe) {
  classify_dxgi_failure(hresult, stage, std::nullopt, startup_probe);
}

}  // namespace spotcobuild
