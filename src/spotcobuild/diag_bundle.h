/**
 * @file src/spotcobuild/diag_bundle.h
 * @brief One-click redacted diagnostic ZIP export.
 */
#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace spotcobuild {

struct bundle_result_t {
  bool ok = false;
  std::filesystem::path path;
  std::string error;
};

/** Export redacted diagnostic ZIP under diagnostics/ or custom path. */
bundle_result_t export_diagnostic_bundle(std::optional<std::filesystem::path> out_path = std::nullopt);

nlohmann::json bundle_manifest();

}  // namespace spotcobuild
