#include "midgard/sequence.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using valhalla::midgard::tar;

namespace {

void set_checksum(tar::header_t& header, bool terminated = true) {
  std::memset(header.chksum, ' ', sizeof(header.chksum));
  unsigned int sum = 0;
  const auto* bytes = reinterpret_cast<const unsigned char*>(&header);
  for (size_t i = 0; i < sizeof(header); ++i) {
    sum += bytes[i];
  }
  char checksum[12];
  if (terminated) {
    std::snprintf(checksum, sizeof(checksum), "%06o", sum);
    std::memcpy(header.chksum, checksum, 6);
    header.chksum[6] = '\0';
    header.chksum[7] = ' ';
  } else {
    std::snprintf(checksum, sizeof(checksum), "%08o", sum);
    std::memcpy(header.chksum, checksum, sizeof(header.chksum));
  }
}

tar::header_t make_header(const std::string& name, size_t size) {
  tar::header_t header{};
  std::memcpy(header.name, name.data(), std::min(name.size(), sizeof(header.name)));
  char size_field[24];
  std::snprintf(size_field, sizeof(size_field), "%011llo", static_cast<unsigned long long>(size));
  std::memcpy(header.size, size_field, sizeof(header.size));
  header.typeflag = '0';
  std::memcpy(header.magic, "ustar", 5);
  set_checksum(header);
  return header;
}

std::string archive_path(const std::string& name) {
  const auto path = std::filesystem::path(VALHALLA_BUILD_DIR "test/data") / name;
  std::filesystem::create_directories(path.parent_path());
  return path.string();
}

TEST(Tar, OctalFieldWidth) {
  const char bytes[] = {'1', '2', '7', '7', '\0'};
  EXPECT_EQ(tar::header_t::octal_to_int(bytes, 3), 0127);
  const char padded[] = {' ', ' ', '1', '2', '3', '\0'};
  EXPECT_EQ(tar::header_t::octal_to_int(padded, sizeof(padded)), 0123);
}

TEST(Tar, InvalidOctalField) {
  const char invalid[] = {'0', '9', '\0'};
  EXPECT_THROW(tar::header_t::octal_to_int(invalid, sizeof(invalid)), std::runtime_error);
}

TEST(Tar, FullChecksumField) {
  auto header = make_header("index.bin", 0);
  std::memcpy(header.linkname, "123", 3);
  set_checksum(header, false);
  EXPECT_TRUE(header.verify());
}

TEST(Tar, FileSizeRepresentation) {
  auto header = make_header("index.bin", 0);
  std::memcpy(header.size, "40000000000", sizeof(header.size));
  if (sizeof(size_t) < sizeof(uint64_t)) {
    EXPECT_THROW(header.get_file_size(), std::runtime_error);
  } else {
    EXPECT_EQ(header.get_file_size(), uint64_t{1} << 32);
  }
}

TEST(Tar, FullNameField) {
  const auto path = archive_path("tar_full_name.tar");
  const std::string name(100, 'x');
  auto header = make_header(name, 0);
  std::memset(header.mode, '0', sizeof(header.mode));
  set_checksum(header);
  {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
  }
  {
    tar archive(path);
    size_t entries = 0;
    EXPECT_EQ(archive.for_each([&](const std::string& entry, const char*, size_t size) {
      ++entries;
      EXPECT_EQ(entry, name);
      EXPECT_EQ(size, 0);
      return true;
    }),
              0);
    EXPECT_EQ(entries, 1);
  }
  std::filesystem::remove(path);
}

TEST(Tar, TruncatedPayload) {
  const auto path = archive_path("tar_truncated.tar");
  const auto header = make_header("index.bin", 512);
  {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
  }
  {
    tar archive(path);
    bool visited = false;
    EXPECT_EQ(archive.for_each([&](const std::string&, const char*, size_t) {
      visited = true;
      return true;
    }),
              1);
    EXPECT_FALSE(visited);
  }
  std::filesystem::remove(path);
}

TEST(Tar, EntriesAndPadding) {
  const auto path = archive_path("tar_entries.tar");
  const auto first = make_header("index.bin", 3);
  const auto second = make_header("empty", 0);
  const tar::header_t padding{};
  {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(&first), sizeof(first));
    std::string data(512, '\0');
    data.replace(0, 3, "abc");
    file.write(data.data(), data.size());
    file.write(reinterpret_cast<const char*>(&padding), sizeof(padding));
    file.write(reinterpret_cast<const char*>(&second), sizeof(second));
  }
  {
    tar archive(path);
    std::vector<std::string> names;
    EXPECT_EQ(archive.for_each([&](const std::string& name, const char* data, size_t size) {
      names.push_back(name);
      EXPECT_EQ(std::string(data, size), name == "index.bin" ? "abc" : "");
      return true;
    }),
              0);
    EXPECT_EQ(names, (std::vector<std::string>{"index.bin", "empty"}));
  }
  std::filesystem::remove(path);
}

} // namespace
