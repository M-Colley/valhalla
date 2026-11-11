#include "midgard/elevation_encoding.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

using namespace valhalla::midgard;

TEST(ElevationEncoding, FixedPrecision) {
  const std::vector<std::pair<double, int32_t>> cases = {{-100.0, -400}, {-1.25, -5}, {-0.25, -1},
                                                         {-0.125, -1},   {0.0, 0},    {0.125, 1},
                                                         {0.25, 1},      {1.25, 5}};
  for (const auto& [elevation, expected] : cases) {
    EXPECT_EQ(to_fixed_precision(elevation), expected) << elevation;
  }
}

TEST(ElevationEncoding, RoundTripAcrossSeaLevel) {
  const std::vector<double> heights = {-1.25, -1.0, -0.75, -0.5, -0.25, 0.0, 0.25, 0.5};
  bool error = false;
  auto encoded = encode_elevation(heights, error);
  EXPECT_FALSE(error);
  const auto decoded = decode_elevation(encoded, heights.front(), heights.back(), true);
  const auto reversed = decode_elevation(encoded, heights.back(), heights.front(), false);
  ASSERT_EQ(decoded.size(), heights.size());
  ASSERT_EQ(reversed.size(), heights.size());
  for (size_t i = 0; i < heights.size(); ++i) {
    EXPECT_EQ(decoded[i], heights[i]);
    EXPECT_EQ(reversed[i], heights[heights.size() - i - 1]);
  }
}

TEST(ElevationEncoding, ShortProfiles) {
  for (const auto& heights : {std::vector<double>{}, {1.0}, {1.0, 2.0}}) {
    bool error = false;
    EXPECT_TRUE(encode_elevation(heights, error).empty());
    EXPECT_FALSE(error);
  }
}

TEST(ElevationEncoding, SampleCount) {
  for (uint32_t length = 0; length < 4096; ++length) {
    EXPECT_EQ(encoded_elevation_count(length), length / kMaxEdgeElevationSampling) << length;
  }
  EXPECT_EQ(encoded_elevation_count(std::numeric_limits<uint32_t>::max()),
            std::numeric_limits<uint32_t>::max() / kMaxEdgeElevationSampling);
}
