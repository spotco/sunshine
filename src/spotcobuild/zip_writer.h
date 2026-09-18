/**
 * @file src/spotcobuild/zip_writer.h
 * @brief Minimal ZIP (store-only) writer for diagnostic bundles.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace spotcobuild {

class zip_writer_t {
public:
  void add_file(std::string archive_name, std::string_view data);
  void add_filesystem_file(std::string archive_name, const std::filesystem::path &path);
  bool write(const std::filesystem::path &out_path) const;

private:
  struct entry_t {
    std::string name;
    std::string data;
    std::uint32_t crc32 = 0;
  };
  std::vector<entry_t> entries_;
};

}  // namespace spotcobuild
