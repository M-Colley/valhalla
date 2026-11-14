#include "../src/thor/triplegbuilder_utils.h"

#include <boost/property_tree/ptree.hpp>
#include <gtest/gtest.h>

using namespace valhalla;
using namespace valhalla::baldr;

namespace {

// A platform with ordinary stop metadata and an optional matching departure.
// These in-memory records exercise the builder without requiring a tile dataset.
struct TransitPlatformTile : GraphTile {
  explicit TransitPlatformTile(bool has_departure) {
    header.set_graphid(GraphId(0, 3, 0));
    header.set_nodecount(1);
    header.set_directededgecount(1);
    header.set_stopcount(1);
    header.set_departurecount(has_departure ? 1 : 0);
    header.set_schedulecount(1);
    header.set_date_created(
        DateTime::days_from_pivot_date(DateTime::get_formatted_date("2026-11-14T06:00")));
    header_ = &header;
    nodes_ = &platform;
    directededges_ = &edge;
    transit_stops_ = &stop;
    departures_ = &departure;
    transit_schedules_ = &schedule;
    textlist_ = names;
    textlist_size_ = sizeof(names);

    platform.set_type(NodeType::kMultiUseTransitPlatform);
    platform.set_edge_index(0);
    platform.set_edge_count(1);
    platform.set_stop_index(0);
    platform.set_timezone(DateTime::get_tz_db().to_index("Etc/UTC"));
    platform.set_latlng(header.base_ll(), midgard::PointLL(0.01, 0.02));
    edge.set_use(Use::kBus);
    edge.set_lineid(7);
  }

  GraphTileHeader header;
  NodeInfo platform;
  DirectedEdge edge;
  TransitStop stop{1, 11, false, static_cast<uint32_t>(Traversability::kBoth)};
  TransitDeparture departure{7, 99, 0, 123, 0, 22500, 180, 0, true, true};
  TransitSchedule schedule{1, 127, 0};
  char names[28] = "\0test-stop\0Sample platform\0";
};

TripLeg_Node build_platform(bool has_departure, const std::string& origin_time) {
  Location origin;
  origin.set_date_time(origin_time);
  auto time_info =
      TimeInfo::make(*origin.mutable_date_time(), DateTime::get_tz_db().to_index("Etc/UTC"));
  MultimodalBuilder builder(origin, time_info);
  builder.arrival_time = "2026-11-14T05:58+00:00";
  builder.assumed_schedule = true;

  auto* platform_tile = new TransitPlatformTile(has_departure);
  graph_tile_ptr tile(platform_tile);
  GraphReader reader(boost::property_tree::ptree{});
  AttributesController controller;
  controller.set_all(true);
  sif::mode_costing_t costings{};
  TripLeg_Node node;
  builder.Build(&node, 99, &platform_tile->platform, GraphId(0, 3, 0), &platform_tile->edge,
                GraphId(0, 3, 0), tile, tile, costings, controller, reader);
  return node;
}

TEST(MultimodalBuilder, MissingDepartureKeepsPlatformMetadata) {
  const auto node = build_platform(false, "2026-11-14T06:00");
  ASSERT_TRUE(node.has_transit_platform_info());
  const auto& platform = node.transit_platform_info();
  EXPECT_EQ(platform.type(), TransitPlatformInfo_Type_kStop);
  EXPECT_EQ(platform.onestop_id(), "test-stop");
  EXPECT_EQ(platform.name(), "Sample platform");
  ASSERT_TRUE(platform.has_ll());
  EXPECT_NEAR(platform.ll().lng(), 0.01, 1e-6);
  EXPECT_NEAR(platform.ll().lat(), 0.02, 1e-6);
  EXPECT_EQ(platform.arrival_date_time(), "2026-11-14T05:58+00:00");
  EXPECT_TRUE(platform.departure_date_time().empty());
  EXPECT_FALSE(platform.assumed_schedule());
}

TEST(MultimodalBuilder, MissingDepartureClearsFollowingTransitState) {
  Location origin;
  origin.set_date_time("2026-11-15T06:00");
  const auto timezone = DateTime::get_tz_db().to_index("Etc/UTC");
  auto time_info = TimeInfo::make(*origin.mutable_date_time(), timezone);
  MultimodalBuilder builder(origin, time_info);
  auto* platform_tile = new TransitPlatformTile(true);
  graph_tile_ptr tile(platform_tile);
  GraphReader reader(boost::property_tree::ptree{});
  AttributesController controller;
  controller.set_all(true);
  sif::mode_costing_t costings{};
  auto build = [&](TripLeg_Node& node, uint32_t trip_id) {
    builder.Build(&node, trip_id, &platform_tile->platform, GraphId(0, 3, 0), &platform_tile->edge,
                  GraphId(0, 3, 0), tile, tile, costings, controller, reader);
  };

  TripLeg_Node preceding;
  build(preceding, 99);
  ASSERT_EQ(builder.block_id, 123);
  ASSERT_EQ(builder.arrival_time, "2026-11-15T06:18+00:00");
  ASSERT_TRUE(builder.assumed_schedule);
  time_info = time_info.forward(1080, timezone);

  // The tile has a departure for trip 99, but none for this platform's trip 100.
  TripLeg_Node current;
  build(current, 100);
  ASSERT_TRUE(current.has_transit_platform_info());
  EXPECT_EQ(current.transit_platform_info().arrival_date_time(), "2026-11-15T06:18+00:00");
  EXPECT_TRUE(current.transit_platform_info().departure_date_time().empty());
  EXPECT_FALSE(current.transit_platform_info().assumed_schedule());
  // TripLegBuilder passes this block_id to AddTripEdge for the current edge.
  EXPECT_EQ(builder.block_id, 0);
  EXPECT_TRUE(builder.arrival_time.empty());
  EXPECT_FALSE(builder.assumed_schedule);

  TripLeg_Node next;
  build(next, 0);
  ASSERT_TRUE(next.has_transit_platform_info());
  EXPECT_TRUE(next.transit_platform_info().arrival_date_time().empty());
  EXPECT_FALSE(next.transit_platform_info().assumed_schedule());
}

TEST(MultimodalBuilder, FixedDepartureSetsTimes) {
  const auto node = build_platform(true, "2026-11-14T06:00");
  ASSERT_TRUE(node.has_transit_platform_info());
  const auto& platform = node.transit_platform_info();
  EXPECT_EQ(platform.name(), "Sample platform");
  EXPECT_EQ(platform.departure_date_time(), "2026-11-14T06:15+00:00");
  EXPECT_FALSE(platform.assumed_schedule());
}

TEST(MultimodalBuilder, FixedDepartureOutsideScheduleIsAssumed) {
  const auto node = build_platform(true, "2026-11-15T06:00");
  ASSERT_TRUE(node.has_transit_platform_info());
  const auto& platform = node.transit_platform_info();
  EXPECT_EQ(platform.departure_date_time(), "2026-11-15T06:15+00:00");
  EXPECT_TRUE(platform.assumed_schedule());
}

} // namespace

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
