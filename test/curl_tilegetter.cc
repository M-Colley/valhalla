#include "baldr/curl_tilegetter.h"
#include "midgard/pointll.h"
#include "skadi/sample.h"

#include <gtest/gtest.h>

#ifdef ENABLE_HTTP
#include <curl/curl.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#endif

namespace {

#ifdef ENABLE_HTTP
class Curler : public testing::Test {
protected:
  void SetUp() override {
    const auto* version = curl_version_info(CURLVERSION_NOW);
    ASSERT_NE(version, nullptr);
    ASSERT_NE(version->protocols, nullptr);
    for (auto protocol = version->protocols; *protocol; ++protocol) {
      if (std::strcmp(*protocol, "file") == 0)
        return;
    }
    GTEST_SKIP() << "libcurl was built without file protocol support";
  }
};

class CurlFile {
public:
  explicit CurlFile(const std::string& name)
      : path_(std::filesystem::path(VALHALLA_BUILD_DIR) / "test/data" / name) {
    std::filesystem::create_directories(path_.parent_path());
    std::ofstream file(path_, std::ios::binary);
    file << "0123456789";
    if (!file)
      throw std::runtime_error("Could not write curl test file");
  }

  ~CurlFile() {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  }

  std::string url() const {
#ifdef _WIN32
    std::string result = "file:///";
#else
    std::string result = "file://";
#endif
    constexpr char digits[] = "0123456789ABCDEF";
    for (const auto character : std::filesystem::absolute(path_).generic_u8string()) {
      const auto byte = static_cast<unsigned char>(character);
      if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
          (byte >= '0' && byte <= '9') || byte == '/' || byte == ':' || byte == '-' || byte == '_' ||
          byte == '.' || byte == '~') {
        result += static_cast<char>(byte);
      } else {
        result += '%';
        result += digits[byte >> 4];
        result += digits[byte & 0xf];
      }
    }
    return result;
  }

private:
  std::filesystem::path path_;
};

TEST_F(Curler, RangeDoesNotLeakToFullGet) {
  CurlFile file("curler range.bin");
  valhalla::baldr::curler_t curler("", "");
  const auto range = curler.get(file.url(), false, nullptr, 2, 3);
  EXPECT_EQ(std::string(range.bytes_.begin(), range.bytes_.end()), "234");
  const auto full = curler.get(file.url(), false, nullptr, 0, 0);
  EXPECT_EQ(std::string(full.bytes_.begin(), full.bytes_.end()), "0123456789");
}

TEST_F(Curler, InterruptDoesNotLeakToGet) {
  CurlFile file("curler_interrupt_get.bin");
  valhalla::baldr::curler_t curler("", "");
  bool interrupted = false;
  size_t calls = 0;
  const valhalla::baldr::curler_t::interrupt_t interrupt = [&] {
    ++calls;
    if (interrupted)
      throw std::runtime_error("Cancelled request");
  };
  curler.get(file.url(), false, &interrupt, 0, 0);
  ASSERT_GT(calls, 0);
  const auto previous_calls = calls;
  interrupted = true;
  EXPECT_NO_THROW(curler.get(file.url(), false, nullptr, 0, 0));
  EXPECT_EQ(calls, previous_calls);
}

TEST_F(Curler, InterruptDoesNotLeakToHead) {
  CurlFile file("curler_interrupt_head.bin");
  valhalla::baldr::curler_t curler("", "");
  bool interrupted = false;
  size_t calls = 0;
  const valhalla::baldr::curler_t::interrupt_t interrupt = [&] {
    ++calls;
    if (interrupted)
      throw std::runtime_error("Cancelled request");
  };
  curler.get(file.url(), false, &interrupt, 0, 0);
  ASSERT_GT(calls, 0);
  const auto previous_calls = calls;
  interrupted = true;
  EXPECT_NO_THROW(curler.head(file.url(), valhalla::baldr::tile_getter_t::kHeaderNone));
  EXPECT_EQ(calls, previous_calls);
}

TEST_F(Curler, InterruptedTransferCanBeRetried) {
  CurlFile file("curler_interrupted_retry.bin");
  const valhalla::baldr::curler_t::interrupt_t interrupt = [] {
    throw std::runtime_error("Cancelled request");
  };
  for (const bool head : {false, true}) {
    valhalla::baldr::curler_t curler("", "");
    EXPECT_THROW(curler.get(file.url(), false, &interrupt, 0, 0), std::runtime_error);
    if (head) {
      EXPECT_NO_THROW(curler.head(file.url(), valhalla::baldr::tile_getter_t::kHeaderNone));
    } else {
      EXPECT_NO_THROW(curler.get(file.url(), false, nullptr, 0, 0));
    }
  }
}
#endif

struct TestSample : public valhalla::skadi::sample {
  static uint16_t get_tile_index(const valhalla::midgard::PointLL& coord) {
    return valhalla::skadi::sample::get_tile_index(coord);
  }
};

TEST(Sample, test_make_single_point_url) {
  struct test_case {
    std::string url;
    valhalla::midgard::PointLL point;
    std::string result;
  };
  // clang-format off
  std::vector<test_case> tests{{{"http://s3.amazonaws.com/mapzen.valhalla{tilePath}"},
                                {-73.512143, 40.646556},
                                {"http://s3.amazonaws.com/mapzen.valhalla/N40/N40W074.hgt"}},
                               {{"http://s3.amazonaws.com/mapzen.valhalla{tilePath}"},
                                {-78.504476, -0.212297},
                                {"http://s3.amazonaws.com/mapzen.valhalla/S01/S01W079.hgt"}},
                               {{"http://s3.amazonaws.com/mapzen.valhalla{tilePath}"},
                                {152.967888, -27.533658},
                                {"http://s3.amazonaws.com/mapzen.valhalla/S28/S28E152.hgt"}},
                               {{"http://s3.amazonaws.com/mapzen.valhalla{tilePath}"},
                                {139.737345, 35.628096},
                                {"http://s3.amazonaws.com/mapzen.valhalla/N35/N35E139.hgt"}},
                               {{"http://s3.amazonaws.com/mapzen.valhalla{tilePath}"},
                                {4.898484, 52.380697},
                                {"http://s3.amazonaws.com/mapzen.valhalla/N52/N52E004.hgt"}}};

  for (auto&& test : tests) {
    EXPECT_EQ(test.result, valhalla::baldr::make_single_point_url(test.url,
                    valhalla::skadi::get_hgt_file_name(TestSample::get_tile_index(test.point))));
  }
  // clang-format on
}

} // namespace

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
