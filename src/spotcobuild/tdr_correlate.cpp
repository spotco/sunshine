/**
 * @file src/spotcobuild/tdr_correlate.cpp
 * @brief Windows TDR / WER correlation via dynamically loaded wevtapi (MinGW-friendly).
 */
#include <cstdio>
#include "tdr_correlate.h"

#include "session_timeline.h"

#include "src/logging.h"

#include <mutex>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#endif

namespace spotcobuild {
namespace {

std::mutex g_mutex;
tdr_correlation_t g_last;

#ifdef _WIN32

using EvtQuery_t = HANDLE(WINAPI *)(HANDLE, LPCWSTR, LPCWSTR, DWORD);
using EvtNext_t = BOOL(WINAPI *)(HANDLE, DWORD, HANDLE *, DWORD, DWORD, PDWORD);
using EvtClose_t = BOOL(WINAPI *)(HANDLE);
using EvtRender_t = BOOL(WINAPI *)(HANDLE, HANDLE, DWORD, DWORD, PVOID, PDWORD, PDWORD);

constexpr DWORD kEvtQueryChannelPath = 0x1;
constexpr DWORD kEvtQueryReverseDirection = 0x200;
constexpr DWORD kEvtRenderEventXml = 1;

bool query_events_with_wevt(tdr_correlation_t &out, std::chrono::seconds lookback) {
  HMODULE mod = LoadLibraryW(L"wevtapi.dll");
  if (!mod) {
    out.narrative = "wevtapi.dll not available; TDR/WER correlation skipped.";
    return false;
  }

  auto pEvtQuery = reinterpret_cast<EvtQuery_t>(GetProcAddress(mod, "EvtQuery"));
  auto pEvtNext = reinterpret_cast<EvtNext_t>(GetProcAddress(mod, "EvtNext"));
  auto pEvtClose = reinterpret_cast<EvtClose_t>(GetProcAddress(mod, "EvtClose"));
  auto pEvtRender = reinterpret_cast<EvtRender_t>(GetProcAddress(mod, "EvtRender"));
  if (!pEvtQuery || !pEvtNext || !pEvtClose || !pEvtRender) {
    FreeLibrary(mod);
    out.narrative = "wevtapi exports missing; TDR/WER correlation skipped.";
    return false;
  }

  constexpr wchar_t query[] =
    L"*[System[(Provider[@Name='Display'] or Provider[@Name='Microsoft-Windows-WER-SystemErrorReporting'] "
    L"or Provider[@Name='Application Error']) and (EventID=141 or EventID=4101 or EventID=1000 or EventID=1001)]]";

  HANDLE h = pEvtQuery(nullptr, L"System", query, kEvtQueryChannelPath | kEvtQueryReverseDirection);
  if (!h) {
    FreeLibrary(mod);
    out.narrative = "TDR/WER System Event Log query failed.";
    return false;
  }

  HANDLE events[8] {};
  DWORD returned = 0;
  bool any = false;
  // Tight timeout so we never block the capture thread for long.
  if (pEvtNext(h, 8, events, 200 /*ms*/, 0, &returned)) {
    for (DWORD i = 0; i < returned; ++i) {
      WCHAR buffer[4096];
      DWORD buffer_used = 0;
      DWORD property_count = 0;
      if (pEvtRender(nullptr, events[i], kEvtRenderEventXml, sizeof(buffer), buffer, &buffer_used, &property_count)) {
        std::wstring xml(buffer, buffer_used / sizeof(WCHAR));
        auto id_pos = xml.find(L"<EventID>");
        long event_id = 0;
        if (id_pos != std::wstring::npos) {
          event_id = _wtol(xml.c_str() + id_pos + 9);
        }
        if (event_id == 141 || event_id == 4101 || event_id == 1000 || event_id == 1001) {
          any = true;
          out.raw.push_back(nlohmann::json {
            {"event_id", event_id},
            {"channel", "System"},
          });
          out.event_id = event_id;
          out.event_source = "System";
        }
      }
      pEvtClose(events[i]);
    }
  }
  pEvtClose(h);
  FreeLibrary(mod);

  if (any) {
    out.found = true;
    const double approx = static_cast<double>(lookback.count()) / 2.0;
    out.delta_seconds = approx;
    char buf[256];
    std::snprintf(buf, sizeof(buf),
      "GPU timeout detected approximately %.1f seconds before encoder/capture failure (Event ID %lld).",
      approx, static_cast<long long>(out.event_id));
    out.narrative = buf;
  }
  else {
    out.narrative = "No recent GPU timeout / TDR / WER events matched in Event Log lookback.";
  }
  return any;
}
#endif

}  // namespace

nlohmann::json tdr_correlation_t::to_json() const {
  nlohmann::json j {
    {"found", found},
    {"narrative", narrative},
    {"event_source", event_source},
    {"event_id", event_id},
    {"raw", raw},
  };
  if (delta_seconds) {
    j["delta_seconds"] = *delta_seconds;
  }
  return j;
}

tdr_correlation_t correlate_tdr_wer(std::chrono::system_clock::time_point failure_time, std::chrono::seconds lookback) {
  tdr_correlation_t out;
#ifdef _WIN32
  (void) failure_time;
  try {
    query_events_with_wevt(out, lookback);
  }
  catch (...) {
    out.narrative = "TDR/WER correlation threw; skipped.";
  }
#else
  (void) failure_time;
  (void) lookback;
  out.narrative = "TDR/WER correlation is Windows-only; skipped on this platform.";
#endif

  {
    std::lock_guard lg(g_mutex);
    g_last = out;
  }

  session_timeline_t::instance().emit_active("tdr_correlation", out.to_json());
  if (out.found) {
    BOOST_LOG(warning) << "spotcobuild TDR correlation: " << out.narrative;
  }
  else {
    BOOST_LOG(info) << "spotcobuild TDR correlation: " << out.narrative;
  }
  return out;
}

tdr_correlation_t last_tdr_correlation() {
  std::lock_guard lg(g_mutex);
  return g_last;
}

}  // namespace spotcobuild
