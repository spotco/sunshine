/**
 * @file src/spotcobuild/error_classify.h
 * @brief DXGI / NVENC error classification into stable categories.
 */
#pragma once

#include "categories.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace spotcobuild {

struct classified_error_t {
  failure_category category = failure_category::unknown;
  std::string stage;
  std::string message;
  std::optional<std::int64_t> hresult;
  std::optional<std::int64_t> device_removed_reason;
  std::optional<std::string> nvenc_status;
  std::optional<std::string> api_call;
  bool startup_probe = false;
  std::optional<std::uint64_t> frame_number;

  nlohmann::json to_json() const;
};

classified_error_t classify_dxgi_failure(
  std::int64_t hresult,
  std::string_view stage,
  std::optional<std::int64_t> device_removed_reason = std::nullopt,
  bool startup_probe = false,
  std::optional<std::uint64_t> frame_number = std::nullopt
);

classified_error_t classify_nvenc_failure(
  std::string_view nvenc_status,
  std::string_view api_call,
  std::string_view encoder_state,
  bool startup_probe = false,
  std::optional<std::uint64_t> frame_number = std::nullopt
);

classified_error_t classify_network_failure(std::string_view stage, std::int64_t error_code);
classified_error_t classify_display_mode_change(std::string_view detail);

/** Query IDXGIDevice::GetDeviceRemovedReason when a raw D3D/DXGI device pointer is available. */
std::optional<std::int64_t> query_device_removed_reason(void *d3d_or_dxgi_device);

}  // namespace spotcobuild
