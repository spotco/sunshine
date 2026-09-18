/**
 * @file src/spotcobuild/redact.cpp
 * @brief Credential / secret redaction for diagnostic bundles.
 */
#include "redact.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace spotcobuild {
namespace {

std::string lower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

}  // namespace

bool looks_like_secret_key(std::string_view key) {
  const auto k = lower(key);
  static constexpr const char *needles[] = {
    "password", "passwd", "secret", "token", "pkey", "private", "cert", "certificate",
    "key", "pin", "credential", "salt", "csrf", "gcm_key", "rikey", "clientcert",
  };
  for (auto n : needles) {
    if (k.find(n) != std::string::npos) {
      // Avoid redacting harmless keys like "packetsize" — require stronger matches for bare "key"
      if (std::string_view(n) == "key") {
        if (k == "key" || k.ends_with("_key") || k.starts_with("key_") || k.find("pkey") != std::string::npos ||
            k.find("apikey") != std::string::npos || k.find("api_key") != std::string::npos) {
          return true;
        }
        continue;
      }
      return true;
    }
  }
  return false;
}

std::string redact_text(std::string_view text) {
  std::string out(text);
  // PEM blocks
  static const std::regex pem(R"(-----BEGIN [^-]+-----[\s\S]*?-----END [^-]+-----)");
  out = std::regex_replace(out, pem, std::string(kRedacted));
  // password=... style
  static const std::regex kv(R"((?i)(password|passwd|secret|token|pin|pkey|salt)\s*[:=]\s*\S+)");
  out = std::regex_replace(out, kv, std::string("$1=") + std::string(kRedacted));
  return out;
}

nlohmann::json redact_json(nlohmann::json value) {
  if (value.is_object()) {
    nlohmann::json out = nlohmann::json::object();
    for (auto it = value.begin(); it != value.end(); ++it) {
      if (looks_like_secret_key(it.key())) {
        out[it.key()] = std::string(kRedacted);
      }
      else {
        out[it.key()] = redact_json(it.value());
      }
    }
    return out;
  }
  if (value.is_array()) {
    nlohmann::json out = nlohmann::json::array();
    for (auto &el : value) {
      out.push_back(redact_json(el));
    }
    return out;
  }
  if (value.is_string()) {
    return redact_text(value.get<std::string>());
  }
  return value;
}

std::unordered_map<std::string, std::string> redact_config_map(const std::unordered_map<std::string, std::string> &vars) {
  std::unordered_map<std::string, std::string> out;
  for (const auto &[k, v] : vars) {
    out[k] = looks_like_secret_key(k) ? std::string(kRedacted) : redact_text(v);
  }
  return out;
}

}  // namespace spotcobuild
