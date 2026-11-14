#include "gurka.h"

#include <gtest/gtest.h>

#if !defined(VALHALLA_SOURCE_DIR)
#define VALHALLA_SOURCE_DIR
#endif

using namespace valhalla;

const std::unordered_map<std::string, std::string> build_config{
    {"mjolnir.admin", {VALHALLA_SOURCE_DIR "test/data/netherlands_admin.sqlite"}}};

const std::vector<std::string>& costing = {"auto",    "taxi",          "bus",        "truck",
                                           "bicycle", "motor_scooter", "motorcycle", "pedestrian"};

TEST(Standalone, Oneway) {
  constexpr double gridsize_metres = 100;

  const std::string ascii_map = R"(
               
        A---B---C---D---E
                    |   |
        J---I---H---G---F
    )";

  const gurka::ways ways = {
      {"AB", {{"highway", "unclassified"}}},
      {"BC", {{"highway", "unclassified"}}},
      {"CD", {{"highway", "unclassified"}}},
      {"DE", {{"highway", "unclassified"}}},
      {"EF", {{"highway", "unclassified"}}},
      {"FG", {{"highway", "unclassified"}}},
      {"DG",
       {{"highway", "unclassified"},
        {"oneway", "yes"},
        {"oneway:bicycle", "yes"},
        {"oneway:foot", "yes"}}},
      {"GH", {{"highway", "unclassified"}}},
      {"HI", {{"highway", "unclassified"}}},
      {"IJ", {{"highway", "unclassified"}}},

  };

  const auto layout =
      gurka::detail::map_to_coordinates(ascii_map, gridsize_metres, {5.1079374, 52.0887174});
  auto map =
      gurka::buildtiles(layout, ways, {}, {}, "test/data/gurka_oneway_unclassified", build_config);
  for (auto& c : costing) {
    auto result = gurka::do_action(valhalla::Options::route, map, {"A", "J"}, c);
    gurka::assert::raw::expect_path(result, {"AB", "BC", "CD", "DG", "GH", "HI", "IJ"});
  }

  for (auto const& c : costing) {
    auto result = gurka::do_action(valhalla::Options::route, map, {"J", "A"}, c);
    gurka::assert::raw::expect_path(result, {"IJ", "HI", "GH", "FG", "EF", "DE", "CD", "BC", "AB"});
  }
}

TEST(Standalone, OnewayFootway) {
  constexpr double gridsize_metres = 10;

  const std::string ascii_map = R"(
               
        A---B---C---D---E
                    |   |
        J---I---H---G---F
    )";

  const gurka::ways ways = {
      {"AB", {{"highway", "unclassified"}}},
      {"BC", {{"highway", "unclassified"}}},
      {"CD", {{"highway", "unclassified"}}},
      {"DE", {{"highway", "unclassified"}}},
      {"EF", {{"highway", "unclassified"}}},
      {"FG", {{"highway", "unclassified"}}},
      {"DG",
       {{"highway", "footway"},
        {"bicycle", "yes"},
        {"oneway:bicycle", "yes"},
        {"oneway:foot", "yes"}}},
      {"GH", {{"highway", "unclassified"}}},
      {"HI", {{"highway", "unclassified"}}},
      {"IJ", {{"highway", "unclassified"}}},

  };

  const auto layout =
      gurka::detail::map_to_coordinates(ascii_map, gridsize_metres, {5.1079374, 52.0887174});
  auto map = gurka::buildtiles(layout, ways, {}, {}, "test/data/gurka_oneway_footway", build_config);
  for (auto& c : costing) {
    auto result = gurka::do_action(valhalla::Options::route, map, {"A", "J"}, c);
    if (c == "pedestrian" || c == "bicycle")
      gurka::assert::raw::expect_path(result, {"AB", "BC", "CD", "DG", "GH", "HI", "IJ"});
    else
      gurka::assert::raw::expect_path(result, {"AB", "BC", "CD", "DE", "EF", "FG", "GH", "HI", "IJ"});
  }

  for (auto const& c : costing) {
    auto result = gurka::do_action(valhalla::Options::route, map, {"J", "A"}, c);
    gurka::assert::raw::expect_path(result, {"IJ", "HI", "GH", "FG", "EF", "DE", "CD", "BC", "AB"});
  }
}

TEST(Standalone, OnewayFootwayAllowBike) {
  constexpr double gridsize_metres = 10;

  const std::string ascii_map = R"(
               
        A---B---C---D---E
                    |   |
        J---I---H---G---F
    )";

  const gurka::ways ways = {
      {"AB", {{"highway", "unclassified"}}},
      {"BC", {{"highway", "unclassified"}}},
      {"CD", {{"highway", "unclassified"}}},
      {"DE", {{"highway", "unclassified"}}},
      {"EF",
       {{"highway", "unclassified"},
        {"oneway", "yes"},
        {"bicycle", "no"}}}, // tend not to favor footways, so make the costing use it
      {"FG", {{"highway", "unclassified"}}},
      {"DG", {{"highway", "footway"}, {"bicycle", "yes"}, {"oneway", "yes"}}},
      {"GH", {{"highway", "unclassified"}}},
      {"HI", {{"highway", "unclassified"}}},
      {"IJ", {{"highway", "unclassified"}}},

  };

  const auto layout =
      gurka::detail::map_to_coordinates(ascii_map, gridsize_metres, {5.1079374, 52.0887174});
  auto map =
      gurka::buildtiles(layout, ways, {}, {}, "test/data/gurka_oneway_footway_bike", build_config);
  for (auto& c : costing) {
    auto result = gurka::do_action(valhalla::Options::route, map, {"A", "J"}, c);

    if (c == "pedestrian" || c == "bicycle")
      gurka::assert::raw::expect_path(result, {"AB", "BC", "CD", "DG", "GH", "HI", "IJ"});
    else
      gurka::assert::raw::expect_path(result, {"AB", "BC", "CD", "DE", "EF", "FG", "GH", "HI", "IJ"});
  }
}

TEST(Standalone, OnewayFlipped) {
  constexpr double gridsize_metres = 10;

  const std::string ascii_map = R"(
               
        A---B---C---D---E
                    |   |
        J---I---H---G---F
    )";

  const gurka::ways ways = {
      {"AB", {{"highway", "unclassified"}}},
      {"BC", {{"highway", "unclassified"}}},
      {"CD", {{"highway", "unclassified"}}},
      {"DE", {{"highway", "unclassified"}}},
      {"EF", {{"highway", "unclassified"}}},
      {"FG", {{"highway", "unclassified"}}},
      {"DG",
       {{"highway", "footway"}, {"bicycle", "yes"}, {"oneway:bicycle", "-1"}, {"oneway:foot", "-1"}}},
      {"GH", {{"highway", "unclassified"}}},
      {"HI", {{"highway", "unclassified"}}},
      {"IJ", {{"highway", "unclassified"}}},

  };

  const auto layout =
      gurka::detail::map_to_coordinates(ascii_map, gridsize_metres, {5.1079374, 52.0887174});
  auto map = gurka::buildtiles(layout, ways, {}, {}, "test/data/gurka_oneway_flipped", build_config);
  for (auto& c : costing) {
    auto result = gurka::do_action(valhalla::Options::route, map, {"A", "J"}, c);

    gurka::assert::raw::expect_path(result, {"AB", "BC", "CD", "DE", "EF", "FG", "GH", "HI", "IJ"});
  }
}

namespace {
struct PedestrianOnewayCase {
  std::string name;
  std::string highway;
  std::string oneway;
  std::map<std::string, std::string> foot_tags;
  bool foot_forward;
  bool foot_backward;
};

class PedestrianOnewayTest : public ::testing::TestWithParam<PedestrianOnewayCase> {};

TEST_P(PedestrianOnewayTest, GeometryDirections) {
  const auto& test = GetParam();
  const std::string ascii_map = R"(
    X---A---B---Y
        |   |
        C---D
  )";
  gurka::ways ways = {{"XA", {{"highway", "residential"}}},
                      {"AB", {{"highway", test.highway}, {"foot", "yes"}, {"bicycle", "yes"}}},
                      {"BY", {{"highway", "residential"}}},
                      {"AC", {{"highway", "residential"}}},
                      {"CD", {{"highway", "residential"}}},
                      {"DB", {{"highway", "residential"}}}};
  if (!test.oneway.empty()) {
    ways["AB"]["oneway"] = test.oneway;
  }
  for (const auto& tag : test.foot_tags) {
    ways["AB"][tag.first] = tag.second;
  }
  const auto layout = gurka::detail::map_to_coordinates(ascii_map, 50);
  const auto map =
      gurka::buildtiles(layout, ways, {}, {}, "test/data/gurka_pedestrian_oneway_" + test.name);
  baldr::GraphReader reader(map.config.get_child("mjolnir"));
  const auto* ab = std::get<1>(gurka::findEdgeByNodes(reader, layout, "A", "B"));
  const auto* ba = std::get<1>(gurka::findEdgeByNodes(reader, layout, "B", "A"));
  const auto check_access = [&](uint32_t mask, bool forward, bool backward) {
    EXPECT_EQ((ab->forwardaccess() & mask) != 0, forward);
    EXPECT_EQ((ab->reverseaccess() & mask) != 0, backward);
    EXPECT_EQ((ba->forwardaccess() & mask) != 0, backward);
    EXPECT_EQ((ba->reverseaccess() & mask) != 0, forward);
  };
  check_access(baldr::kPedestrianAccess, test.foot_forward, test.foot_backward);
  const bool vehicle_forward = test.oneway != "-1";
  const bool vehicle_backward = test.oneway != "yes";
  check_access(baldr::kBicycleAccess, vehicle_forward, vehicle_backward);
  check_access(baldr::kAutoAccess, test.highway == "residential" && vehicle_forward,
               test.highway == "residential" && vehicle_backward);

  for (const std::string profile : {"pedestrian", "bicycle", "auto"}) {
    if (profile == "auto" && test.highway != "residential") {
      continue;
    }
    SCOPED_TRACE(profile);
    const bool forward = profile == "pedestrian" ? test.foot_forward : vehicle_forward;
    const bool backward = profile == "pedestrian" ? test.foot_backward : vehicle_backward;
    const std::unordered_map<std::string, std::string> options =
        {{"/costing_options/" + profile + "/shortest", "1"},
         {"/costing_options/pedestrian/step_penalty", "0"}};
    auto result = gurka::do_action(Options::route, map, {"X", "Y"}, profile, options);
    gurka::assert::raw::expect_path(result,
                                    forward ? std::vector<std::string>{"XA", "AB", "BY"}
                                            : std::vector<std::string>{"XA", "AC", "CD", "DB", "BY"});
    result = gurka::do_action(Options::route, map, {"Y", "X"}, profile, options);
    gurka::assert::raw::expect_path(result, backward ? std::vector<std::string>{"BY", "AB", "XA"}
                                                     : std::vector<std::string>{"BY", "DB", "CD",
                                                                                "AC", "XA"});
  }
}

INSTANTIATE_TEST_SUITE_P(
    PedestrianDirections,
    PedestrianOnewayTest,
    ::testing::Values(
        PedestrianOnewayCase{"Footway", "footway", "yes", {}, true, true},
        PedestrianOnewayCase{"FootwayReverse", "footway", "-1", {}, true, true},
        PedestrianOnewayCase{"Path", "path", "yes", {}, true, true},
        PedestrianOnewayCase{"PathReverse", "path", "-1", {}, true, true},
        PedestrianOnewayCase{"Pedestrian", "pedestrian", "yes", {}, true, true},
        PedestrianOnewayCase{"PedestrianReverse", "pedestrian", "-1", {}, true, true},
        PedestrianOnewayCase{"Steps", "steps", "yes", {}, true, true},
        PedestrianOnewayCase{"StepsReverse", "steps", "-1", {}, true, true},
        PedestrianOnewayCase{"Residential", "residential", "yes", {}, true, true},
        PedestrianOnewayCase{"ResidentialReverse", "residential", "-1", {}, true, true},
        PedestrianOnewayCase{"ReverseFootYes",
                             "footway",
                             "-1",
                             {{"oneway:foot", "yes"}},
                             true,
                             false},
        PedestrianOnewayCase{"ReverseFootReverse",
                             "footway",
                             "-1",
                             {{"oneway:foot", "-1"}},
                             false,
                             true},
        PedestrianOnewayCase{"ReverseFootNo", "footway", "-1", {{"oneway:foot", "no"}}, true, true},
        PedestrianOnewayCase{"ReverseFootForwardNo",
                             "footway",
                             "-1",
                             {{"foot:forward", "no"}},
                             false,
                             true},
        PedestrianOnewayCase{"ReverseFootBackwardNo",
                             "footway",
                             "-1",
                             {{"foot:backward", "no"}},
                             true,
                             false},
        PedestrianOnewayCase{"ReverseFootDenied", "footway", "-1", {{"foot", "no"}}, false, false},
        PedestrianOnewayCase{"FootYes", "footway", "", {{"oneway:foot", "yes"}}, true, false},
        PedestrianOnewayCase{"FootReverse", "footway", "", {{"oneway:foot", "-1"}}, false, true}),
    [](const ::testing::TestParamInfo<PedestrianOnewayCase>& info) { return info.param.name; });
} // namespace
