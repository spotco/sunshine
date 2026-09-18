/**
 * @file src/spotcobuild/redact.h
 * @brief Credential / secret redaction for diagnostic bundles.
 */
#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace spotcobuild {

constexpr std::string_view kRedacted = "[REDACTED]";

bool looks_like_secret_key(std::string_view key);
std::string redact_text(std::string_view text);
nlohmann::json redact_json(nlohmann::json value);
std::unordered_map<std::string, std::string> redact_config_map(const std::unordered_map<std::string, std::string> &vars);

}  // namespace spotcobuild
