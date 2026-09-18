/**
 * @file src/spotcobuild/diag_bundle.cpp
 * @brief One-click redacted diagnostic ZIP export.
 */
#include <cstdio>
#include "diag_bundle.h"

#include "diag_switches.h"
#include "redact.h"
#include "session_timeline.h"
#include "tdr_correlate.h"
#include "zip_writer.h"
#include "recovery.h"

#include "src/config.h"
#include "src/file_handler.h"
#include "src/logging.h"
#include "src/platform/common.h"


#include <chrono>
#include <fstream>
#include <sstream>

namespace spotcobuild {
namespace {

std::string read_file_limited(const std::filesystem::path &path, std::size_t max_bytes = 2 * 1024 * 1024) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (data.size() > max_bytes) {
    data.resize(max_bytes);
    data += "\n/* truncated */\n";
  }
  return data;
}

std::string stamp() {
  const auto now = std::chrono::system_clock::now();
  const auto tt = std::chrono::system_clock::to_time_t(now);
  std::tm tm {};
#if defined(_WIN32)
  gmtime_s(&tm, &tt);
#else
  gmtime_r(&tt, &tm);
#endif
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d%02d%02d",
    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  return buf;
}

}  // namespace

nlohmann::json bundle_manifest() {
  nlohmann::json j;
  j["generator"] = "spotcobuild-diag-bundle";
  j["project_version"] = PROJECT_VERSION;
  j["timeline"] = session_timeline_t::instance().health_snapshot();
  j["switches"] = diag_switches_t::instance().effective_snapshot();
  j["tdr"] = last_tdr_correlation().to_json();
  j["recovery"] = recovery_controller_t::instance().health_snapshot();
  j["os"] = {
#ifdef _WIN32
    {"family", "windows"},
#elif defined(__APPLE__)
    {"family", "macos"},
#else
    {"family", "linux"},
#endif
  };
  return redact_json(std::move(j));
}

bundle_result_t export_diagnostic_bundle(std::optional<std::filesystem::path> out_path) {
  bundle_result_t result;
  const auto diag_dir = session_timeline_t::instance().diagnostics_dir();
  std::error_code ec;
  std::filesystem::create_directories(diag_dir, ec);

  const auto zip_path = out_path.value_or(diag_dir / ("sunshine-diag-" + stamp() + ".zip"));
  zip_writer_t zip;

  zip.add_file("manifest.json", bundle_manifest().dump(2));

  // Config with secrets redacted
  try {
    const auto conf_path = std::filesystem::path(config::sunshine.config_file);
    auto raw = read_file_limited(conf_path);
    zip.add_file("config/sunshine.conf.redacted", redact_text(raw));
  }
  catch (...) {
  }

  // Recent log (redacted)
  try {
    auto log = read_file_limited(std::filesystem::path(config::sunshine.log_file));
    zip.add_file("logs/sunshine.log.redacted", redact_text(log));
  }
  catch (...) {
  }

  // JSONL + ring dumps
  try {
    for (const auto &entry : std::filesystem::directory_iterator(diag_dir)) {
      if (!entry.is_regular_file()) continue;
      const auto name = entry.path().filename().string();
      if (name.ends_with(".jsonl") || name.ends_with(".ring.jsonl")) {
        zip.add_filesystem_file("timeline/" + name, entry.path());
      }
    }
  }
  catch (...) {
  }

  // TDR snapshot
  zip.add_file("tdr/last_correlation.json", last_tdr_correlation().to_json().dump(2));

  // Crash dump path note only (not bytes)
  nlohmann::json crash_note {
    {"note", "Crash dump bytes are opt-in and not included by default. Attach manually if needed."},
    {"mingw_guidance", "Prefer gdb with RelWithDebInfo DWARF in the fat exe; MSVC PDB workflow is separate."},
  };
  zip.add_file("crash/README.json", crash_note.dump(2));

  if (!zip.write(zip_path)) {
    result.error = "failed to write zip";
    return result;
  }

  result.ok = true;
  result.path = zip_path;
  BOOST_LOG(info) << "spotcobuild diagnostic bundle written: " << zip_path.string();
  return result;
}

}  // namespace spotcobuild
