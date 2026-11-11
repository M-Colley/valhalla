#include "thor/edgestatus.h"
#include "baldr/graphtile.h"

#include <gtest/gtest.h>

#include <utility>

using namespace std;
using namespace valhalla::baldr;
using namespace valhalla::thor;

namespace {

void TryGet(const EdgeStatus& edgestatus, const GraphId& edgeid, const EdgeSet expected) {
  EdgeStatusInfo r = edgestatus.Get(edgeid);
  EXPECT_EQ(r.set(), expected);
}

struct test_tile : public GraphTile {
  using GraphTile::header_;
};

TEST(EdgeStatus, MoveAssignment) {
  GraphTileHeader header;
  header.set_directededgecount(10);
  test_tile* tt = new test_tile;
  tt->header_ = &header;
  graph_tile_ptr tile{tt};

  const GraphId old_id(555, 1, 1);
  const GraphId new_id(556, 1, 2);
  EdgeStatus target, source;
  target.Set(old_id, EdgeSet::kTemporary, 3, tile);
  source.Set(new_id, EdgeSet::kPermanent, 4, tile);
  target = std::move(source);

  EXPECT_EQ(target.Get(old_id).set(), EdgeSet::kUnreachedOrReset);
  EXPECT_EQ(target.Get(new_id).set(), EdgeSet::kPermanent);
  EXPECT_EQ(target.Get(new_id).index(), 4);
  EXPECT_EQ(source.Get(new_id).set(), EdgeSet::kUnreachedOrReset);

  EdgeStatus moved(std::move(target));
  EXPECT_EQ(target.Get(new_id).set(), EdgeSet::kUnreachedOrReset);
  EXPECT_EQ(moved.Get(new_id).set(), EdgeSet::kPermanent);
  auto& self = moved;
  moved = std::move(self);
  EXPECT_EQ(moved.Get(new_id).set(), EdgeSet::kPermanent);
}

TEST(EdgeStatus, TestStatus) {
  EdgeStatus edgestatus;

  // Dummy tile header
  GraphTileHeader header;
  header.set_directededgecount(200000);
  test_tile* tt = new test_tile;
  tt->header_ = &header;

  graph_tile_ptr tile{tt};

  // Add some edges
  edgestatus.Set(GraphId(555, 1, 100100), EdgeSet::kPermanent, 1, tile);
  edgestatus.Set(GraphId(555, 2, 100100), EdgeSet::kPermanent, 2, tile);
  edgestatus.Set(GraphId(555, 3, 100100), EdgeSet::kPermanent, 3, tile);
  edgestatus.Set(GraphId(555, 1, 55555), EdgeSet::kTemporary, 4, tile);
  edgestatus.Set(GraphId(555, 2, 55555), EdgeSet::kTemporary, 5, tile);
  edgestatus.Set(GraphId(555, 3, 55555), EdgeSet::kTemporary, 6, tile);
  edgestatus.Set(GraphId(555, 1, 1), EdgeSet::kPermanent, 7, tile);
  edgestatus.Set(GraphId(555, 2, 1), EdgeSet::kPermanent, 8, tile);
  edgestatus.Set(GraphId(555, 3, 1), EdgeSet::kPermanent, 9, tile);

  // Test various get
  TryGet(edgestatus, GraphId(555, 1, 100100), EdgeSet::kPermanent);
  TryGet(edgestatus, GraphId(555, 2, 100100), EdgeSet::kPermanent);
  TryGet(edgestatus, GraphId(555, 3, 100100), EdgeSet::kPermanent);
  TryGet(edgestatus, GraphId(555, 1, 55555), EdgeSet::kTemporary);
  TryGet(edgestatus, GraphId(555, 2, 55555), EdgeSet::kTemporary);
  TryGet(edgestatus, GraphId(555, 3, 55555), EdgeSet::kTemporary);
  TryGet(edgestatus, GraphId(555, 1, 1), EdgeSet::kPermanent);
  TryGet(edgestatus, GraphId(555, 2, 1), EdgeSet::kPermanent);
  TryGet(edgestatus, GraphId(555, 3, 1), EdgeSet::kPermanent);

  // Clear and make sure all status are kUnreachedOrReset
  edgestatus.clear();
  TryGet(edgestatus, GraphId(555, 1, 100100), EdgeSet::kUnreachedOrReset);
  TryGet(edgestatus, GraphId(555, 2, 100100), EdgeSet::kUnreachedOrReset);
  TryGet(edgestatus, GraphId(555, 3, 100100), EdgeSet::kUnreachedOrReset);
  TryGet(edgestatus, GraphId(555, 1, 55555), EdgeSet::kUnreachedOrReset);
  TryGet(edgestatus, GraphId(555, 2, 55555), EdgeSet::kUnreachedOrReset);
  TryGet(edgestatus, GraphId(555, 3, 55555), EdgeSet::kUnreachedOrReset);
  TryGet(edgestatus, GraphId(555, 1, 1), EdgeSet::kUnreachedOrReset);
  TryGet(edgestatus, GraphId(555, 2, 1), EdgeSet::kUnreachedOrReset);
  TryGet(edgestatus, GraphId(555, 3, 1), EdgeSet::kUnreachedOrReset);
}

} // namespace

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
