#include "baldr/graphreader.h"
#include "gurka.h"
#include "mjolnir/osmdata.h"

#include <gtest/gtest.h>

#include <array>
#include <fstream>
#include <stdexcept>

using namespace valhalla;

TEST(LaneConnectivity, LargeWayIdsSurviveStagedBuild) {
  constexpr uint64_t from = (uint64_t{1} << 33) + 101;
  constexpr uint64_t to = (uint64_t{1} << 33) + 103;
  const auto layout = gurka::detail::map_to_coordinates("A----B----C", 100);
  const gurka::ways ways = {
      {"AB", {{"highway", "primary"}, {"oneway", "yes"}, {"osm_id", std::to_string(from)}}},
      {"BC", {{"highway", "primary"}, {"oneway", "yes"}, {"osm_id", std::to_string(to)}}},
  };
  const gurka::relations relations = {
      {{{gurka::way_member, "AB", "from"}, {gurka::way_member, "BC", "to"}},
       {{"type", "connectivity"}, {"from:lanes", "1|2"}, {"to:lanes", "2|3"}}},
  };
  const std::string workdir = VALHALLA_BUILD_DIR "test/data/lane_connectivity_large_ids";
  const std::unordered_map<std::string, std::string> options = {{"mjolnir.hierarchy", "false"},
                                                                {"mjolnir.concurrency", "1"}};
  gurka::buildtiles(layout, ways, {}, relations, workdir, options, mjolnir::BuildStage::kInitialize,
                    mjolnir::BuildStage::kParseNodes);
  const auto map =
      gurka::buildtiles(layout, ways, {}, relations, workdir, options,
                        mjolnir::BuildStage::kConstructEdges, mjolnir::BuildStage::kValidate);

  baldr::GraphReader reader(map.config.get_child("mjolnir"));
  const auto [edge_id, edge] = gurka::findEdgeByNodes(reader, map.nodes, "B", "C");
  ASSERT_NE(edge, nullptr);
  EXPECT_TRUE(edge->laneconnectivity());
  const auto tile = reader.GetGraphTile(edge_id);
  const auto lanes = tile->GetLaneConnectivity(edge_id.id());
  ASSERT_EQ(lanes.size(), 1);
  EXPECT_EQ(lanes.front().from(), from);
  EXPECT_EQ(lanes.front().from_lanes(), "1|2");
  EXPECT_EQ(lanes.front().to_lanes(), "2|3");

  // Intermediate records must be regenerated when their layout changes.
  {
    std::ofstream file(workdir + "/osmdata_lane_connectivity.bin", std::ios::binary);
    ASSERT_TRUE(file.is_open());
    const uint32_t count = 1;
    const std::array<uint64_t, 3> old_record{};
    file.write(reinterpret_cast<const char*>(&count), sizeof(count));
    file.write(reinterpret_cast<const char*>(old_record.data()), sizeof(old_record));
    file.close();
    ASSERT_TRUE(file);
  }
  mjolnir::OSMData incompatible{};
  try {
    incompatible.read_from_temp_files(workdir);
    FAIL() << "Expected incompatible lane connectivity data to be rejected";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find("Incompatible lane connectivity temporary file"),
              std::string::npos);
    EXPECT_NE(std::string(error.what()).find("Restart tile building from parsing"),
              std::string::npos);
  }
}
