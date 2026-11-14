#include "baldr/tilehierarchy.h"
#include "../src/bindings/shared/tile_id_utils.h"
#include "baldr/graphid.h"
#include "midgard/pointll.h"

#include <gtest/gtest.h>

#include <limits>

using namespace std;
using namespace valhalla::baldr;
using namespace valhalla::midgard;

namespace {

TEST(TileHierarchy, Parse) {
  EXPECT_EQ(TileHierarchy::levels().size(), 3) << "Incorrect number of hierarchy levels";
  EXPECT_EQ((++TileHierarchy::levels().begin())->name, "arterial")
      << "Middle hierarchy should be named arterial";
  EXPECT_EQ(TileHierarchy::levels().begin()->level, 0) << "Top hierarchy should have level 0";
  EXPECT_EQ(TileHierarchy::levels().rbegin()->tiles.TileSize(), .25f)
      << "Bottom hierarchy should have tile size of .25f";
  EXPECT_EQ(TileHierarchy::levels()[0].level, 0);
  EXPECT_EQ(TileHierarchy::levels()[1].level, 1);
  EXPECT_EQ(TileHierarchy::levels()[2].level, 2);
  GraphId id = TileHierarchy::GetGraphId(PointLL(0, 0), 34);

  EXPECT_FALSE(id.is_valid()) << "GraphId should be invalid as the level doesn't exist";

  // there are 1440 cols and 720 rows, this spot lands on col 414 and row 522
  id = TileHierarchy::GetGraphId(PointLL(-76.5, 40.5), 2);
  EXPECT_EQ(id.level(), 2);
  EXPECT_EQ(id.tileid(), (522 * 1440) + 414);
  EXPECT_EQ(id.id(), 0);

  EXPECT_EQ(TileHierarchy::levels().begin()->importance, RoadClass::kPrimary)
      << "Importance should be set to primary";
  EXPECT_EQ((++TileHierarchy::levels().begin())->importance, RoadClass::kTertiary)
      << "Importance should be set to tertiary";
  EXPECT_EQ(TileHierarchy::levels().rbegin()->importance, RoadClass::kServiceOther)
      << "Importance should be set to service/other";
}

TEST(TileHierarchy, Tiles) {

  // there are 1440 cols and 720 rows, this spot lands on col 414 and row 522
  AABB2<PointLL> bbox{{-76.49, 40.51}, {-76.48, 40.52}};
  auto ids = TileHierarchy::GetGraphIds(bbox, 2);
  EXPECT_EQ(ids.size(), 1) << "Should have only found one result.";

  auto id = ids[0];
  EXPECT_EQ(id.level(), 2);
  EXPECT_EQ(id.tileid(), (522 * 1440) + 414);
  EXPECT_EQ(id.id(), 0);

  bbox = AABB2<PointLL>{{-76.51, 40.49}, {-76.49, 40.51}};
  ids = TileHierarchy::GetGraphIds(bbox, 2);
  EXPECT_EQ(ids.size(), 4) << "Should have found 4 results.";
}

TEST(TileHierarchy, parent) {
  GraphId id(1440 * 16 + 16, 3, 0);
  auto level2 = TileHierarchy::parent(id);
  EXPECT_EQ(level2, GraphId(id.tileid(), 2, 0));
  auto level1 = TileHierarchy::parent(level2);
  EXPECT_EQ(level1, GraphId(360 * 4 + 4, 1, 0));
  auto level0 = TileHierarchy::parent(level1);
  EXPECT_EQ(level0, GraphId(90 * 1 + 1, 0, 0));
  auto invalid = TileHierarchy::parent(level0);
  EXPECT_EQ(invalid, GraphId(kInvalidGraphId));
}

TEST(TileHierarchy, RingAtWorldBoundary) {
  for (const auto& ring : std::vector<std::vector<PointLL>>{{{0, 90}, {1, 90}, {2, 90}},
                                                            {{180, 0}, {180, 1}, {180, 2}}}) {
    EXPECT_TRUE(valhalla::bindings::get_tile_ids_from_ring(ring, {1}).empty());
  }
}

TEST(TileHierarchy, InvalidBindingCoordinates) {
  const auto nan = std::numeric_limits<double>::quiet_NaN();
  const auto infinity = std::numeric_limits<double>::infinity();
  for (const auto value : {nan, infinity, -infinity}) {
    EXPECT_THROW(valhalla::bindings::check_coord(value, 0, value, 0), std::invalid_argument);
    EXPECT_THROW(valhalla::bindings::check_coord(0, value, 0, value), std::invalid_argument);
    EXPECT_THROW(valhalla::bindings::get_tile_ids_from_ring({{0, 0}, {1, 0}, {value, 1}}, {1}),
                 std::invalid_argument);
  }
}

TEST(TileHierarchy, ReversedOutOfBoundsCoordinates) {
  const std::vector<AABB2<PointLL>> boxes = {{200, 0, 100, 1},
                                             {-100, 0, -200, 1},
                                             {0, 100, 1, 80},
                                             {0, -80, 1, -100}};
  for (const auto& box : boxes) {
    EXPECT_THROW(valhalla::bindings::check_coord(box.minx(), box.miny(), box.maxx(), box.maxy()),
                 std::invalid_argument);
  }
  EXPECT_NO_THROW(valhalla::bindings::check_coord(179, -1, -179, 1));
}

} // namespace

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
