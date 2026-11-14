#include "gurka.h"

#include <gtest/gtest.h>

#include <random>
#include <unordered_map>

using namespace valhalla;

class SimpleRestrictions : public ::testing::Test {
protected:
  static gurka::map map;

  static void SetUpTestSuite() {

    const std::string ascii_map = R"(
    A---1B----C
    |    |
    D----E----F
    |
    G----H---2I)";

    // generate random 64bit ids for these ways
    std::mt19937_64 gen(1);
    std::uniform_int_distribution<uint64_t> dist(100, std::numeric_limits<uint64_t>::max());
    std::vector<std::string> ids{
        "121", "122", "123", "124", "6472927700900931484", "125",
    };
    for (int i = 0; i < 6; ++i) {
      ids.push_back(std::to_string(dist(gen)));
    }

    const gurka::ways ways = {
        {"AB", {{"highway", "primary"}, {"osm_id", ids[0]}}},
        {"BC", {{"highway", "primary"}, {"osm_id", ids[1]}}},
        {"DEF", {{"highway", "primary"}, {"osm_id", ids[2]}}},
        {"GHI", {{"highway", "primary"}, {"osm_id", ids[3]}}},
        {"ADG", {{"highway", "primary"}, {"osm_id", ids[4]}}},
        {"BE", {{"highway", "primary"}, {"osm_id", ids[5]}}},
    };

    const gurka::relations relations = {
        {{
             {gurka::way_member, "BC", "from"},
             {gurka::way_member, "BE", "to"},
             {gurka::node_member, "B", "via"},
         },
         {
             {"type", "restriction"},
             {"restriction", "no_left_turn"},
         }},
        {{
             {gurka::way_member, "GHI", "from"},
             {gurka::way_member, "AB", "to"},
             {gurka::way_member, "ADG", "via"},
         },
         {
             {"type", "restriction"},
             {"restriction", "no_entry"},
         }},
    };

    const auto layout = gurka::detail::map_to_coordinates(ascii_map, 100);
    map = gurka::buildtiles(layout, ways, {}, relations, "test/data/simple_restrictions",
                            {{"mjolnir.hierarchy", "false"}, {"mjolnir.concurrency", "1"}});
  }
};

gurka::map SimpleRestrictions::map = {};

/*************************************************************/
TEST_F(SimpleRestrictions, ForceDetour) {
  auto result = gurka::do_action(valhalla::Options::route, map, {"C", "F"}, "auto");
  gurka::assert::osrm::expect_steps(result, {"BC", "ADG", "DEF"});
  gurka::assert::raw::expect_path(result, {"BC", "AB", "ADG", "DEF", "DEF"});
}
TEST_F(SimpleRestrictions, NoDetourWhenReversed) {
  auto result = gurka::do_action(valhalla::Options::route, map, {"F", "C"}, "auto");
  gurka::assert::osrm::expect_steps(result, {"DEF", "BE", "BC"});
  gurka::assert::raw::expect_path(result, {"DEF", "BE", "BC"});
}
TEST_F(SimpleRestrictions, NoDetourFromDifferentStart) {
  auto result = gurka::do_action(valhalla::Options::route, map, {"1", "F"}, "auto");
  gurka::assert::osrm::expect_steps(result, {"AB", "BE", "DEF"});
  gurka::assert::raw::expect_path(result, {"AB", "BE", "DEF"});
}
TEST_F(SimpleRestrictions, ForceDetourComplex) {
  // this test fails if you remove the time dependence because
  // bidirectional a* has a problem with complex restrictions
  auto result = gurka::do_action(valhalla::Options::route, map,
                                 R"({"locations":[{"lon":0.008084,"lat":-0.003593},
    {"lon":0.00359,"lat":0.0}],"costing":"auto","date_time":{"type":0}})");
  gurka::assert::raw::expect_path(result, {"GHI", "ADG", "DEF", "BE", "AB"});
}
TEST_F(SimpleRestrictions, IgnoreRestriction) {
  auto result = gurka::do_action(valhalla::Options::route, map, {"C", "F"}, "auto",
                                 {{"/costing_options/auto/ignore_restrictions", "1"}});
  gurka::assert::osrm::expect_steps(result, {"BC", "BE", "DEF"});
  gurka::assert::raw::expect_path(result, {"BC", "BE", "DEF"});
}
TEST_F(SimpleRestrictions, IgnoreRestrictionPedestrian) {
  auto result = gurka::do_action(valhalla::Options::route, map, {"C", "F"}, "pedestrian");
  gurka::assert::osrm::expect_steps(result, {"BC", "BE", "DEF"});
  gurka::assert::raw::expect_path(result, {"BC", "BE", "DEF"});
}

TEST_F(SimpleRestrictions, IgnoreRestrictionMatching) {
  auto result =
      gurka::do_action(valhalla::Options::trace_route, map, {"C", "B", "E", "F"}, "auto",
                       {{"/costing_options/auto/ignore_restrictions", "1"}}, {}, nullptr, "break");
  gurka::assert::osrm::expect_steps(result, {"BC", "BE", "DEF"}, true, "matchings");
  gurka::assert::raw::expect_path(result, {"BC", "BE", "DEF"});
}

TEST(NoEntryRestrictions, MultipleFrom) {
  const std::string ascii_map = R"(
    A---1---B---2---C
    |       |       |
    G-------H-------I
            |
            3
            D
  )";
  const gurka::ways ways = {
      {"AB", {{"highway", "primary"}}}, {"CB", {{"highway", "primary"}}},
      {"BH", {{"highway", "primary"}}}, {"AG", {{"highway", "primary"}}},
      {"GH", {{"highway", "primary"}}}, {"CI", {{"highway", "primary"}}},
      {"IH", {{"highway", "primary"}}}, {"HD", {{"highway", "primary"}}},
  };
  const auto layout = gurka::detail::map_to_coordinates(ascii_map, 50);
  for (const bool except_bicycle : {false, true}) {
    SCOPED_TRACE(except_bicycle ? "except bicycle" : "all vehicles");
    gurka::relation restriction{{{gurka::way_member, "AB", "from"},
                                 {gurka::way_member, "CB", "from"},
                                 {gurka::node_member, "B", "via"},
                                 {gurka::way_member, "BH", "to"}},
                                {{"type", "restriction"}, {"restriction", "no_entry"}}};
    if (except_bicycle)
      restriction.tags["except"] = "bicycle";
    const auto map =
        gurka::buildtiles(layout, ways, {}, {restriction},
                          std::string(VALHALLA_BUILD_DIR "test/data/no_entry_multiple_from_") +
                              (except_bicycle ? "except" : "simple"),
                          {{"mjolnir.hierarchy", "false"}, {"mjolnir.concurrency", "1"}});
    const std::vector<std::unordered_map<std::string, std::string>> algorithms = {
        {},
        {{"/date_time/type", "0"}},
        {{"/date_time/type", "2"}, {"/date_time/value", "2026-01-05T09:00"}},
    };
    for (const auto& options : algorithms) {
      SCOPED_TRACE(options.empty() ? "bidirectional" : options.at("/date_time/type"));
      auto result = gurka::do_action(Options::route, map, {"1", "3"}, "auto", options);
      gurka::assert::raw::expect_path(result, {"AB", "AG", "GH", "HD"});
      result = gurka::do_action(Options::route, map, {"2", "3"}, "auto", options);
      gurka::assert::raw::expect_path(result, {"CB", "CI", "IH", "HD"});
      result = gurka::do_action(Options::route, map, {"3", "1"}, "auto", options);
      gurka::assert::raw::expect_path(result, {"HD", "BH", "AB"});

      result = gurka::do_action(Options::route, map, {"1", "3"}, "bicycle", options);
      gurka::assert::raw::expect_path(result, except_bicycle
                                                  ? std::vector<std::string>{"AB", "BH", "HD"}
                                                  : std::vector<std::string>{"AB", "AG", "GH", "HD"});
      result = gurka::do_action(Options::route, map, {"2", "3"}, "bicycle", options);
      gurka::assert::raw::expect_path(result, except_bicycle
                                                  ? std::vector<std::string>{"CB", "BH", "HD"}
                                                  : std::vector<std::string>{"CB", "CI", "IH", "HD"});
    }
  }
}
