/**
 * @file src/spotcobuild/zip_writer.cpp
 * @brief Minimal ZIP (store-only) writer for diagnostic bundles.
 */
#include "zip_writer.h"

#include <fstream>

namespace spotcobuild {
namespace {

std::uint32_t crc32_update(std::uint32_t crc, const unsigned char *buf, std::size_t len) {
  crc = ~crc;
  for (std::size_t i = 0; i < len; ++i) {
    crc ^= buf[i];
    for (int k = 0; k < 8; ++k) {
      const std::uint32_t mask = -(crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

void write_u16(std::string &out, std::uint16_t v) {
  out.push_back(static_cast<char>(v & 0xff));
  out.push_back(static_cast<char>((v >> 8) & 0xff));
}

void write_u32(std::string &out, std::uint32_t v) {
  out.push_back(static_cast<char>(v & 0xff));
  out.push_back(static_cast<char>((v >> 8) & 0xff));
  out.push_back(static_cast<char>((v >> 16) & 0xff));
  out.push_back(static_cast<char>((v >> 24) & 0xff));
}

}  // namespace

void zip_writer_t::add_file(std::string archive_name, std::string_view data) {
  entry_t e;
  e.name = std::move(archive_name);
  e.data.assign(data.data(), data.size());
  e.crc32 = crc32_update(0, reinterpret_cast<const unsigned char *>(e.data.data()), e.data.size());
  entries_.push_back(std::move(e));
}

void zip_writer_t::add_filesystem_file(std::string archive_name, const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return;
  }
  std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  add_file(std::move(archive_name), data);
}

bool zip_writer_t::write(const std::filesystem::path &out_path) const {
  std::string blob;
  std::string central;
  std::uint32_t offset = 0;
  for (const auto &e : entries_) {
    const auto local_header_offset = offset;
    std::string local;
    write_u32(local, 0x04034b50u);
    write_u16(local, 20);  // version needed
    write_u16(local, 0);  // flags
    write_u16(local, 0);  // method store
    write_u16(local, 0);  // time
    write_u16(local, 0);  // date
    write_u32(local, e.crc32);
    write_u32(local, static_cast<std::uint32_t>(e.data.size()));
    write_u32(local, static_cast<std::uint32_t>(e.data.size()));
    write_u16(local, static_cast<std::uint16_t>(e.name.size()));
    write_u16(local, 0);  // extra
    local += e.name;
    local += e.data;
    blob += local;
    offset += static_cast<std::uint32_t>(local.size());

    write_u32(central, 0x02014b50u);
    write_u16(central, 20);
    write_u16(central, 20);
    write_u16(central, 0);
    write_u16(central, 0);
    write_u16(central, 0);
    write_u16(central, 0);
    write_u32(central, e.crc32);
    write_u32(central, static_cast<std::uint32_t>(e.data.size()));
    write_u32(central, static_cast<std::uint32_t>(e.data.size()));
    write_u16(central, static_cast<std::uint16_t>(e.name.size()));
    write_u16(central, 0);
    write_u16(central, 0);
    write_u16(central, 0);
    write_u16(central, 0);
    write_u32(central, 0);
    write_u32(central, local_header_offset);
    central += e.name;
  }

  const auto central_offset = offset;
  blob += central;
  std::string end;
  write_u32(end, 0x06054b50u);
  write_u16(end, 0);
  write_u16(end, 0);
  write_u16(end, static_cast<std::uint16_t>(entries_.size()));
  write_u16(end, static_cast<std::uint16_t>(entries_.size()));
  write_u32(end, static_cast<std::uint32_t>(central.size()));
  write_u32(end, central_offset);
  write_u16(end, 0);
  blob += end;

  std::error_code ec;
  std::filesystem::create_directories(out_path.parent_path(), ec);
  std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  out.write(blob.data(), static_cast<std::streamsize>(blob.size()));
  return static_cast<bool>(out);
}

}  // namespace spotcobuild
