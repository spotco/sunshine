/**
 * @file src/spotcobuild/error_classify.cpp
 * @brief DXGI / NVENC error classification into stable categories.
 */
#include "error_classify.h"

#include "session_timeline.h"

#include "src/logging.h"

#ifdef _WIN32
  #include <d3d11.h>
  #include <dxgi.h>
#endif

namespace spotcobuild {
namespace {

#ifdef _WIN32
constexpr std::int64_t DXGI_ERROR_DEVICE_HUNG_V = static_cast<std::int64_t>(0x887A0006);
constexpr std::int64_t DXGI_ERROR_DEVICE_REMOVED_V = static_cast<std::int64_t>(0x887A0005);
constexpr std::int64_t DXGI_ERROR_DEVICE_RESET_V = static_cast<std::int64_t>(0x887A0007);
constexpr std::int64_t DXGI_ERROR_DRIVER_INTERNAL_ERROR_V = static_cast<std::int64_t>(0x887A0020);
constexpr std::int64_t DXGI_ERROR_ACCESS_LOST_V = static_cast<std::int64_t>(0x887A0026);
constexpr std::int64_t DXGI_ERROR_ACCESS_DENIED_V = static_cast<std::int64_t>(0x887A002B);
constexpr std::int64_t DXGI_ERROR_INVALID_CALL_V = static_cast<std::int64_t>(0x887A0001);
constexpr std::int64_t DXGI_STATUS_MODE_CHANGE_IN_PROGRESS_V = static_cast<std::int64_t>(0x087A0001);
#endif

bool looks_like_device_lost_status(std::string_view status) {
  const auto s = std::string(status);
  return s.find("DEVICE") != std::string::npos ||
         s.find("REMOVED") != std::string::npos ||
         s.find("RESET") != std::string::npos ||
         s.find("INVALID_DEVICE") != std::string::npos ||
         s.find("OUT_OF_MEMORY") != std::string::npos ||
         s.find("ENCODER_NOT_FOUND") != std::string::npos;
}

}  // namespace

nlohmann::json classified_error_t::to_json() const {
  nlohmann::json j {
    {"category", category_name(category)},
    {"stage", stage},
    {"message", message},
    {"startup_probe", startup_probe},
  };
  if (hresult) j["hresult"] = *hresult;
  if (device_removed_reason) j["device_removed_reason"] = *device_removed_reason;
  if (nvenc_status) j["nvenc_status"] = *nvenc_status;
  if (api_call) j["api_call"] = *api_call;
  if (frame_number) j["frame_number"] = *frame_number;
  return j;
}

classified_error_t classify_dxgi_failure(
  std::int64_t hresult,
  std::string_view stage,
  std::optional<std::int64_t> device_removed_reason,
  bool startup_probe,
  std::optional<std::uint64_t> frame_number
) {
  classified_error_t out;
  out.hresult = hresult;
  out.stage = std::string(stage);
  out.startup_probe = startup_probe;
  out.frame_number = frame_number;
  out.device_removed_reason = device_removed_reason;

#ifdef _WIN32
  const auto hr = static_cast<std::uint32_t>(hresult);
  const auto reason = device_removed_reason ? static_cast<std::uint32_t>(*device_removed_reason) : 0u;
  if (hr == static_cast<std::uint32_t>(DXGI_ERROR_ACCESS_LOST_V) ||
      hr == static_cast<std::uint32_t>(DXGI_ERROR_DEVICE_REMOVED_V) ||
      hr == static_cast<std::uint32_t>(DXGI_ERROR_DEVICE_RESET_V) ||
      hr == static_cast<std::uint32_t>(DXGI_ERROR_DEVICE_HUNG_V) ||
      reason == static_cast<std::uint32_t>(DXGI_ERROR_DEVICE_REMOVED_V) ||
      reason == static_cast<std::uint32_t>(DXGI_ERROR_DEVICE_RESET_V) ||
      reason == static_cast<std::uint32_t>(DXGI_ERROR_DEVICE_HUNG_V)) {
    out.category = failure_category::capture_device_lost;
    out.message = "DXGI capture device lost/hung/reset";
  }
  else if (hr == static_cast<std::uint32_t>(DXGI_ERROR_DRIVER_INTERNAL_ERROR_V) ||
           reason == static_cast<std::uint32_t>(DXGI_ERROR_DRIVER_INTERNAL_ERROR_V)) {
    out.category = failure_category::driver_internal_error;
    out.message = "DXGI driver internal error";
  }
  else if (hr == static_cast<std::uint32_t>(DXGI_STATUS_MODE_CHANGE_IN_PROGRESS_V) ||
           hr == static_cast<std::uint32_t>(DXGI_ERROR_ACCESS_DENIED_V)) {
    out.category = failure_category::display_mode_changed;
    out.message = "Display mode change / access denied during capture";
  }
  else {
    out.category = failure_category::unknown;
    out.message = "Unclassified DXGI failure";
  }
#else
  (void) hresult;
  out.category = failure_category::unknown;
  out.message = "DXGI classification only available on Windows";
#endif

  auto fields = out.to_json();
  session_timeline_t::instance().emit_active("dxgi_error", std::move(fields));
  return out;
}

classified_error_t classify_nvenc_failure(
  std::string_view nvenc_status,
  std::string_view api_call,
  std::string_view encoder_state,
  bool startup_probe,
  std::optional<std::uint64_t> frame_number
) {
  classified_error_t out;
  out.nvenc_status = std::string(nvenc_status);
  out.api_call = std::string(api_call);
  out.stage = std::string(encoder_state);
  out.startup_probe = startup_probe;
  out.frame_number = frame_number;
  out.message = std::string(api_call) + ": " + std::string(nvenc_status);

  if (looks_like_device_lost_status(nvenc_status)) {
    out.category = failure_category::encoder_device_lost;
  }
  else if (nvenc_status.find("ERROR") != std::string_view::npos) {
    out.category = failure_category::driver_internal_error;
  }
  else {
    out.category = failure_category::unknown;
  }

  auto fields = out.to_json();
  session_timeline_t::instance().emit_active(startup_probe ? "nvenc_probe_error" : "nvenc_error", std::move(fields));
  return out;
}

classified_error_t classify_network_failure(std::string_view stage, std::int64_t error_code) {
  classified_error_t out;
  out.category = failure_category::network_timeout;
  out.stage = std::string(stage);
  out.hresult = error_code;
  out.message = "Network failure during " + out.stage;
  auto fields = out.to_json();
  session_timeline_t::instance().emit_active("network_error", std::move(fields));
  return out;
}

classified_error_t classify_display_mode_change(std::string_view detail) {
  classified_error_t out;
  out.category = failure_category::display_mode_changed;
  out.stage = "display";
  out.message = std::string(detail);
  auto fields = out.to_json();
  session_timeline_t::instance().emit_active("display_mode_changed", std::move(fields));
  return out;
}

std::optional<std::int64_t> query_device_removed_reason(void *d3d_or_dxgi_device) {
#ifdef _WIN32
  if (!d3d_or_dxgi_device) {
    return std::nullopt;
  }
  // GetDeviceRemovedReason is on ID3D11Device (not IDXGIDevice).
  auto *unk = static_cast<IUnknown *>(d3d_or_dxgi_device);
  ID3D11Device *d3d = nullptr;
  if (SUCCEEDED(unk->QueryInterface(__uuidof(ID3D11Device), reinterpret_cast<void **>(&d3d))) && d3d) {
    const HRESULT reason = d3d->GetDeviceRemovedReason();
    d3d->Release();
    return static_cast<std::int64_t>(static_cast<std::uint32_t>(reason));
  }
#else
  (void) d3d_or_dxgi_device;
#endif
  return std::nullopt;
}

}  // namespace spotcobuild
