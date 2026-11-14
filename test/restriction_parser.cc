#include "baldr/timedomain.h"
#include "midgard/sequence.h"
#include "mjolnir/pbfgraphparser.h"

#include <boost/property_tree/ptree.hpp>
#include <gtest/gtest.h>
#include <osmium/builder/attr.hpp>
#include <osmium/io/pbf_output.hpp>
#include <osmium/io/writer.hpp>

#include <filesystem>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace valhalla::baldr;
using namespace valhalla::midgard;
using namespace valhalla::mjolnir;

namespace {

constexpr uint64_t kFirstFrom = 101;
constexpr uint64_t kSecondFrom = (uint64_t{1} << 33) + 102;
constexpr uint64_t kTo = 103;
constexpr uint64_t kVia = 104;

struct ParsedRestriction {
  OSMData data;
  std::vector<OSMRestriction> from;
  std::vector<OSMRestriction> to;
};

ParsedRestriction
parse_restriction(const std::vector<std::pair<std::string, std::string>>& tags,
                  const std::vector<uint64_t>& from_ids,
                  bool via_way = false,
                  const std::vector<osmium::builder::attr::member_type>& extra_vias = {},
                  uint64_t to_id = kTo) {
  const auto directory = std::filesystem::path(VALHALLA_BUILD_DIR "test/data/restriction_parser") /
                         ::testing::UnitTest::GetInstance()->current_test_info()->name();
  std::filesystem::create_directories(directory);
  const auto pbf = (directory / "restriction.osm.pbf").string();
  const auto from_file = (directory / "from.bin").string();
  const auto to_file = (directory / "to.bin").string();

  std::vector<osmium::builder::attr::member_type> members;
  for (const auto id : from_ids)
    members.push_back({osmium::item_type::way, static_cast<osmium::object_id_type>(id), "from"});
  members.push_back({via_way ? osmium::item_type::way : osmium::item_type::node, kVia, "via"});
  members.insert(members.end(), extra_vias.begin(), extra_vias.end());
  members.push_back({osmium::item_type::way, static_cast<osmium::object_id_type>(to_id), "to"});
  osmium::memory::Buffer buffer(1024, osmium::memory::Buffer::auto_grow::yes);
  osmium::builder::add_relation(buffer, osmium::builder::attr::_id(1),
                                osmium::builder::attr::_version(1),
                                osmium::builder::attr::_members(members),
                                osmium::builder::attr::_tags(tags));
  osmium::io::Writer writer(pbf, osmium::io::overwrite::allow);
  writer(std::move(buffer));
  writer.close();

  ParsedRestriction result{};
  result.data.initialized = true;
  boost::property_tree::ptree config;
  config.put("tile_dir", directory.string());
  config.put("concurrency", 1);
  PBFGraphParser::ParseRelations(config, {pbf}, from_file, to_file, result.data);
  for (const auto& restriction : sequence<OSMRestriction>(from_file, false))
    result.from.push_back(restriction);
  for (const auto& restriction : sequence<OSMRestriction>(to_file, false))
    result.to.push_back(restriction);
  return result;
}

TEST(RestrictionParser, NoEntrySimpleMultipleFrom) {
  const auto result = parse_restriction({{"type", "restriction"}, {"restriction", "no_entry"}},
                                        {kFirstFrom, kSecondFrom});
  ASSERT_EQ(result.data.restrictions.size(), 2);
  for (const auto from : {kFirstFrom, kSecondFrom}) {
    ASSERT_EQ(result.data.restrictions.count(from), 1);
    const auto& restriction = result.data.restrictions.find(from)->second;
    EXPECT_EQ(restriction.type(), RestrictionType::kNoEntry);
    EXPECT_EQ(restriction.to(), kTo);
    EXPECT_EQ(restriction.via(), kVia);
  }
  EXPECT_TRUE(result.from.empty());
  EXPECT_TRUE(result.to.empty());
}

TEST(RestrictionParser, NoEntryDoesNotCreateZeroFrom) {
  for (const auto& from_ids :
       {std::vector<uint64_t>{0, kSecondFrom}, std::vector<uint64_t>{kSecondFrom, 0}}) {
    const auto result =
        parse_restriction({{"type", "restriction"}, {"restriction", "no_entry"}}, from_ids);
    EXPECT_EQ(result.data.restrictions.size(), 1);
    EXPECT_EQ(result.data.restrictions.count(kSecondFrom), 1);
  }
  const auto invalid = parse_restriction({{"type", "restriction"}, {"restriction", "no_entry"}}, {0});
  EXPECT_TRUE(invalid.data.restrictions.empty());
}

TEST(RestrictionParser, InvalidViaMembersRemainRejected) {
  for (const auto* type : {"no_entry", "no_left_turn"}) {
    SCOPED_TRACE(type);
    const std::vector<std::pair<std::string, std::string>> tags = {{"type", "restriction"},
                                                                   {"restriction", type}};
    const auto node_then_way = parse_restriction(tags, {kFirstFrom, kSecondFrom}, false,
                                                 {{osmium::item_type::way, kVia + 1, "via"}});
    const auto way_then_node = parse_restriction(tags, {kFirstFrom, kSecondFrom}, true,
                                                 {{osmium::item_type::node, kVia + 1, "via"}});
    std::vector<osmium::builder::attr::member_type> extra_vias;
    for (size_t i = 0; i < kMaxViasPerRestriction; ++i)
      extra_vias.push_back(
          {osmium::item_type::way, static_cast<osmium::object_id_type>(kVia + i + 1), "via"});
    const auto too_many = parse_restriction(tags, {kFirstFrom, kSecondFrom}, true, extra_vias);
    for (const auto* result : {&node_then_way, &way_then_node, &too_many}) {
      EXPECT_TRUE(result->data.restrictions.empty());
      EXPECT_TRUE(result->from.empty());
      EXPECT_TRUE(result->to.empty());
    }
  }
}

void expect_complex_froms(const ParsedRestriction& result, bool bicycle_restricted) {
  EXPECT_TRUE(result.data.restrictions.empty());
  ASSERT_EQ(result.from.size(), 2);
  ASSERT_EQ(result.to.size(), 2);
  std::set<uint64_t> froms, reverse_tos;
  for (const auto& restriction : result.from) {
    froms.insert(restriction.from());
    EXPECT_EQ(restriction.to(), kTo);
    EXPECT_TRUE(restriction.modes() & kAutoAccess);
    EXPECT_EQ(static_cast<bool>(restriction.modes() & kBicycleAccess), bicycle_restricted);
  }
  for (const auto& restriction : result.to) {
    EXPECT_EQ(restriction.from(), kTo);
    reverse_tos.insert(restriction.to());
  }
  EXPECT_EQ(froms, (std::set<uint64_t>{kFirstFrom, kSecondFrom}));
  EXPECT_EQ(reverse_tos, froms);
}

TEST(RestrictionParser, NoEntryMultipleFromWithException) {
  const auto result =
      parse_restriction({{"type", "restriction"}, {"restriction", "no_entry"}, {"except", "bicycle"}},
                        {kFirstFrom, kSecondFrom});
  expect_complex_froms(result, false);
  for (const auto& restriction : result.from)
    EXPECT_EQ(restriction.vias(), std::vector<uint64_t>{kTo});
}

TEST(RestrictionParser, NoEntryMultipleFromWithViaWay) {
  const auto result = parse_restriction({{"type", "restriction"}, {"restriction", "no_entry"}},
                                        {kFirstFrom, kSecondFrom}, true);
  expect_complex_froms(result, true);
  for (const auto& restriction : result.from)
    EXPECT_EQ(restriction.vias(), std::vector<uint64_t>{kVia});
}

TEST(RestrictionParser, NoEntryConditionalMultipleFrom) {
  const auto result =
      parse_restriction({{"type", "restriction"},
                         {"restriction:conditional", "no_entry @ (Mo-Fr 08:00-10:00)"}},
                        {kFirstFrom, kSecondFrom});
  expect_complex_froms(result, true);
  for (const auto& restriction : result.from)
    EXPECT_NE(restriction.time_domain(), 0);
}

TEST(RestrictionParser, NoEntryProbableMultipleFrom) {
  const auto result = parse_restriction({{"type", "restriction"},
                                         {"restriction:probable", "no_entry @ probability=50"}},
                                        {kFirstFrom, kSecondFrom});
  expect_complex_froms(result, true);
  for (const auto& restriction : result.from) {
    EXPECT_EQ(restriction.type(), RestrictionType::kNoProbable);
    EXPECT_EQ(restriction.probability(), 50);
  }
}

TEST(RestrictionParser, OrdinaryTurnRestriction) {
  const auto result =
      parse_restriction({{"type", "restriction"}, {"restriction", "no_left_turn"}}, {kFirstFrom});
  ASSERT_EQ(result.data.restrictions.size(), 1);
  const auto& restriction = result.data.restrictions.find(kFirstFrom)->second;
  EXPECT_EQ(restriction.type(), RestrictionType::kNoLeftTurn);
  EXPECT_EQ(restriction.to(), kTo);
  EXPECT_EQ(restriction.via(), kVia);
}

TEST(RestrictionParser, LaneConnectivityPreservesWayIds) {
  for (const auto [from, to] : {std::pair<uint64_t, uint64_t>{101, 103},
                                {kSecondFrom, 103},
                                {101, kSecondFrom},
                                {uint64_t{1} << 32, uint64_t{1} << 33}}) {
    SCOPED_TRACE(::testing::Message() << from << " -> " << to);
    const auto result =
        parse_restriction({{"type", "connectivity"}, {"from:lanes", "1|2"}, {"to:lanes", "2|3"}},
                          {from}, false, {}, to);
    ASSERT_EQ(result.data.lane_connectivity_map.size(), 1);
    const auto found = result.data.lane_connectivity_map.find(to);
    ASSERT_NE(found, result.data.lane_connectivity_map.end());
    EXPECT_EQ(found->second.to_way_id, to);
    EXPECT_EQ(found->second.from_way_id, from);
    EXPECT_EQ(result.data.name_offset_map.name(found->second.from_lanes_index), "1|2");
    EXPECT_EQ(result.data.name_offset_map.name(found->second.to_lanes_index), "2|3");
  }
}

TEST(RestrictionParser, ConditionalViaWayPreservesTimeDomains) {
  const auto result = parse_restriction({{"type", "restriction"},
                                         {"restriction:conditional",
                                          "no_left_turn @ (Mo-Fr 08:00-10:00,16:00-18:00)"},
                                         {"except", "bus"}},
                                        {kFirstFrom}, true);
  EXPECT_TRUE(result.data.restrictions.empty());
  ASSERT_EQ(result.from.size(), 2);
  ASSERT_EQ(result.to.size(), 1);
  std::set<unsigned> begin_hours, end_hours;
  for (const auto& restriction : result.from) {
    EXPECT_EQ(restriction.type(), RestrictionType::kNoLeftTurn);
    EXPECT_EQ(restriction.from(), kFirstFrom);
    EXPECT_EQ(restriction.to(), kTo);
    EXPECT_EQ(restriction.vias(), std::vector<uint64_t>{kVia});
    EXPECT_TRUE(restriction.modes() & kAutoAccess);
    EXPECT_FALSE(restriction.modes() & kBusAccess);
    EXPECT_NE(restriction.time_domain(), 0);
    const TimeDomain domain(restriction.time_domain());
    begin_hours.insert(domain.begin_hrs());
    end_hours.insert(domain.end_hrs());
  }
  EXPECT_EQ(begin_hours, (std::set<unsigned>{8, 16}));
  EXPECT_EQ(end_hours, (std::set<unsigned>{10, 18}));
  EXPECT_EQ(result.to.front().from(), kTo);
  EXPECT_EQ(result.to.front().to(), kFirstFrom);
  EXPECT_EQ(result.to.front().modes(), result.from.front().modes());
  for (const auto way : {kFirstFrom, kVia, kTo})
    EXPECT_TRUE(result.data.via_set.contains(way));
}

TEST(RestrictionParser, ConditionalNoEntryViaWayMultipleFrom) {
  const auto result =
      parse_restriction({{"type", "restriction"},
                         {"restriction:conditional", "no_entry @ (Mo-Fr 08:00-10:00)"}},
                        {kFirstFrom, kSecondFrom}, true);
  expect_complex_froms(result, true);
  for (const auto& restriction : result.from) {
    EXPECT_NE(restriction.time_domain(), 0);
    EXPECT_EQ(restriction.type(), RestrictionType::kNoEntry);
    EXPECT_EQ(restriction.vias(), std::vector<uint64_t>{kVia});
  }
  for (const auto way : {kFirstFrom, kSecondFrom, kVia, kTo})
    EXPECT_TRUE(result.data.via_set.contains(way));
}

TEST(RestrictionParser, DeprecatedTimedViaWayPreservesTimeDomain) {
  const auto result = parse_restriction({{"type", "restriction"},
                                         {"restriction:bus", "no_left_turn"},
                                         {"hour_on", "07:00"},
                                         {"hour_off", "09:00"}},
                                        {kFirstFrom}, true);
  ASSERT_EQ(result.from.size(), 1);
  const TimeDomain domain(result.from.front().time_domain());
  EXPECT_NE(domain.td_value(), 0);
  EXPECT_EQ(domain.begin_hrs(), 7);
  EXPECT_EQ(domain.end_hrs(), 9);
  EXPECT_TRUE(result.from.front().modes() & kBusAccess);
  EXPECT_FALSE(result.from.front().modes() & kAutoAccess);
}

TEST(RestrictionParser, InvalidConditionalViaWayStaysRejected) {
  for (const bool via_way : {false, true}) {
    const auto result = parse_restriction({{"type", "restriction"},
                                           {"restriction:conditional", "no_left_turn @ invalid"}},
                                          {kFirstFrom}, via_way);
    EXPECT_TRUE(result.from.empty());
    EXPECT_TRUE(result.to.empty());
    EXPECT_TRUE(result.data.restrictions.empty());
  }
}

} // namespace

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
