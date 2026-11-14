#include "baldr/graphconstants.h"
#include "mjolnir/graph_lua_proc.h"
#include "mjolnir/luatagtransform.h"
#include "mjolnir/osmdata.h"

#include <gtest/gtest.h>
#include <osmium/builder/osm_object_builder.hpp>

#include <array>
#include <initializer_list>
#include <string>
#include <utility>

using namespace valhalla;

namespace {
struct TagsBuilder {
  osmium::memory::Buffer buffer;
  osmium::builder::WayBuilder way_builder;
  osmium::builder::TagListBuilder tags_builder;

  TagsBuilder()
      : buffer(1024, osmium::memory::Buffer::auto_grow::yes), way_builder(buffer),
        tags_builder(way_builder) {
  }

  void insert(const std::pair<std::string, std::string>& tag) {
    tags_builder.add_tag(tag);
  }

  const osmium::TagList& get() {
    return way_builder.object().tags();
  }
};

mjolnir::Tags TransformAccessTags(std::initializer_list<std::pair<std::string, std::string>> values) {
  mjolnir::LuaTagTransform lua(std::string(lua_graph_lua, lua_graph_lua + lua_graph_lua_len));
  TagsBuilder tags;
  for (const auto& value : values)
    tags.insert(value);
  return lua.Transform(mjolnir::OSMType::kWay, 1, tags.get());
}
TEST(Lua, LaneCountsPreserveTwoWayAccess) {
  for (const auto& mode : {std::pair{"lanes:bus", "bus"}, std::pair{"lanes:psv", "taxi"}})
    for (const auto* count : {"1", "2"}) {
      SCOPED_TRACE(std::string(mode.first) + "=" + count);
      const auto result = TransformAccessTags({{"highway", "residential"}, {mode.first, count}});
      EXPECT_EQ(result.at(std::string(mode.second) + "_forward"), "true");
      EXPECT_EQ(result.at(std::string(mode.second) + "_backward"), "true");
    }
}
TEST(Lua, LaneCountsRespectAccessRestrictions) {
  for (const auto& mode : {std::pair{"lanes:bus", "bus"}, std::pair{"lanes:psv", "taxi"}})
    for (const auto* count : {"1", "2"}) {
      SCOPED_TRACE(std::string(mode.first) + "=" + count);
      const auto denied =
          TransformAccessTags({{"highway", "residential"}, {mode.first, count}, {mode.second, "no"}});
      EXPECT_EQ(denied.at(std::string(mode.second) + "_forward"), "false");
      EXPECT_EQ(denied.at(std::string(mode.second) + "_backward"), "false");
      const auto directional = TransformAccessTags({{"highway", "residential"},
                                                    {mode.first, count},
                                                    {std::string(mode.second) + ":forward", "no"}});
      EXPECT_EQ(directional.at(std::string(mode.second) + "_forward"), "false");
      EXPECT_EQ(directional.at(std::string(mode.second) + "_backward"), "true");
    }
}
TEST(Lua, LaneCountsRespectOneWayAndContraflow) {
  for (const auto& mode : {std::pair{"lanes:bus", "bus"}, std::pair{"lanes:psv", "taxi"}})
    for (const auto* count : {"1", "2"}) {
      SCOPED_TRACE(std::string(mode.first) + "=" + count);
      for (const auto* direction : {"yes", "-1"}) {
        const auto result = TransformAccessTags(
            {{"highway", "residential"}, {mode.first, count}, {"oneway", direction}});
        const bool forward = std::string(direction) == "yes";
        EXPECT_EQ(result.at(std::string(mode.second) + "_forward"), forward ? "true" : "false");
        EXPECT_EQ(result.at(std::string(mode.second) + "_backward"), forward ? "false" : "true");
      }
      const auto contra = TransformAccessTags({{"highway", "residential"},
                                               {mode.first, count},
                                               {"oneway", "yes"},
                                               {std::string("oneway:") + mode.second, "no"}});
      EXPECT_EQ(contra.at(std::string(mode.second) + "_forward"), "true");
      EXPECT_EQ(contra.at(std::string(mode.second) + "_backward"), "true");
    }
}

TEST(Lua, ZeroMantissa) {
  mjolnir::LuaTagTransform lua(std::string(lua_graph_lua, lua_graph_lua + lua_graph_lua_len));

  TagsBuilder tags;
  // Check that decimals are properly parsed
  tags.insert({"highway", "primary"});
  tags.insert({"maxheight", "2.0"});
  auto results = lua.Transform(mjolnir::OSMType::kWay, 1, tags.get());
  ASSERT_FLOAT_EQ(2.0f, std::stof(results["maxheight"]));
}

void assert_height_parses(const std::string& maxheight, float expected) {
  mjolnir::LuaTagTransform lua(std::string(lua_graph_lua, lua_graph_lua + lua_graph_lua_len));

  TagsBuilder tags;
  tags.insert({"highway", "tertiary"});
  tags.insert({"maxheight", maxheight});
  auto results = lua.Transform(mjolnir::OSMType::kWay, 1, tags.get());
  ASSERT_TRUE(results.count("maxheight") == 1);
  ASSERT_FLOAT_EQ(expected, std::stof(results["maxheight"]));
}

TEST(Lua, DefaultMeters) {
  // default for a number without units should be meters
  assert_height_parses("1.0", 1.0f);
  assert_height_parses("1", 1.0f);
  assert_height_parses("1.1", 1.1f);
}

TEST(Lua, ExplicitMeters) {
  // check all the common ways of writing meters
  assert_height_parses("1m", 1.0f);

  // there could be spaces, or not.
  assert_height_parses("1.1 m", 1.1f);
  assert_height_parses("1.1m", 1.1f);
  assert_height_parses("1 m", 1.0f);

  // but the units can change if they're plural
  assert_height_parses("1 meter", 1.0f);
  assert_height_parses("2meters", 2.0f);
}

TEST(Lua, UnitsCaseInsensitive) {
  // case doesn't matter for units
  assert_height_parses("1 METERS", 1.0f);
}

TEST(Lua, Centimeters) {
  // yes, this exists in the data...
  assert_height_parses("100cm", 1.0f);
}

TEST(Lua, EuropeanDecimal) {
  // it's common to see numbers written using the European convention of a
  // comma as the decimal separator.
  assert_height_parses("1,1", 1.1f);
}

TEST(Lua, FeetAndInches) {
  // there are a bunch of different commonly-used ways of writing heights in
  // feet and inches in OSM. here's a few, cropped from taginfo and generalised
  // to their generic formats.
  assert_height_parses("1ft1in", 0.33f);
  assert_height_parses("2 feet 1 inch", 0.64f);
  assert_height_parses("1 foot 6 inches", 0.46f);
  assert_height_parses("1.1 ft", 0.34f);
  assert_height_parses("1 ft", 0.3f);

  // feet and inches can also be written with single and double quotes (' and ")
  // and even with two single quotes (' and '').
  assert_height_parses("1'", 0.3f);
  assert_height_parses("1.1\"", 0.03f);
  assert_height_parses("1' 1\"", 0.33f);
  assert_height_parses("1'1\"", 0.33f);
  assert_height_parses("1'1''", 0.33f);
}

TEST(Lua, NumberDoublePeriod) {
  // Way 25494427 version 14 has a "maxheight" tag value of 3..35 with the two
  // dots. This probably shouldn't be parsed?
  mjolnir::LuaTagTransform lua(std::string(lua_graph_lua, lua_graph_lua + lua_graph_lua_len));

  TagsBuilder tags;
  tags.insert({"highway", "tertiary"});
  tags.insert({"maxheight", "3..35"});
  auto results = lua.Transform(mjolnir::OSMType::kWay, 1, tags.get());

  // check that the maxheight isn't present...
  ASSERT_TRUE(results.count("maxheight") == 0);

  // ... but that the results aren't completely empty
  ASSERT_TRUE(results.size() > 0);
}

TEST(Lua, TestForwardBackward) {
  // Way 25494427 version 14 has a "maxheight" tag value of 3..35 with the two
  // dots. This probably shouldn't be parsed?
  mjolnir::LuaTagTransform lua(std::string(lua_graph_lua, lua_graph_lua + lua_graph_lua_len));

  TagsBuilder tags;
  tags.insert({"highway", "tertiary"});
  tags.insert({"maxheight:forward", "1"});
  tags.insert({"maxheight:backward", "1"});
  tags.insert({"maxlength:forward", "1"});
  tags.insert({"maxlength:backward", "1"});
  tags.insert({"maxwidth:forward", "1"});
  tags.insert({"maxwidth:backward", "1"});
  tags.insert({"maxweight:forward", "1"});
  tags.insert({"maxweight:backward", "1"});
  auto results = lua.Transform(mjolnir::OSMType::kWay, 1, tags.get());

  // check that the maxheight is present...
  ASSERT_TRUE(results.count("maxheight_forward") == 1);
  ASSERT_TRUE(results.count("maxheight_backward") == 1);
  ASSERT_TRUE(results.count("maxlength_forward") == 1);
  ASSERT_TRUE(results.count("maxlength_backward") == 1);
  ASSERT_TRUE(results.count("maxwidth_forward") == 1);
  ASSERT_TRUE(results.count("maxwidth_backward") == 1);
  ASSERT_TRUE(results.count("maxweight_forward") == 1);
  ASSERT_TRUE(results.count("maxweight_backward") == 1);
}

TEST(Lua, RoadsWithRailsKeepRoadClassification) {
  mjolnir::LuaTagTransform lua(std::string(lua_graph_lua, lua_graph_lua + lua_graph_lua_len));
  for (const auto& highway : {std::pair{"residential", "6"}, std::pair{"primary", "2"}}) {
    TagsBuilder tags;
    tags.insert({"highway", highway.first});
    tags.insert({"railway", "rail"});
    const auto result = lua.Transform(mjolnir::OSMType::kWay, 1, tags.get());
    EXPECT_EQ(result.at("auto_forward"), "true");
    EXPECT_EQ(result.at("rail"), "false");
    EXPECT_EQ(result.at("road_class"), highway.second);
  }
}
TEST(Lua, ShuttleTrainClassificationRequiresAutoAccess) {
  mjolnir::LuaTagTransform lua(std::string(lua_graph_lua, lua_graph_lua + lua_graph_lua_len));
  for (const auto& value :
       {std::array{"shuttle_train", "yes", "true"}, std::array{"shuttle_train", "no", "false"},
        std::array{"ferry", "yes", "false"}}) {
    TagsBuilder tags;
    tags.insert({"route", value[0]});
    tags.insert({"railway", "rail"});
    tags.insert({"motorcar", value[1]});
    EXPECT_EQ(lua.Transform(mjolnir::OSMType::kWay, 1, tags.get()).at("rail"), value[2]);
  }
  TagsBuilder shuttle;
  shuttle.insert({"route", "shuttle_train"});
  EXPECT_EQ(lua.Transform(mjolnir::OSMType::kWay, 1, shuttle.get()).at("rail"), "true");
  TagsBuilder road;
  road.insert({"highway", "residential"});
  EXPECT_EQ(lua.Transform(mjolnir::OSMType::kWay, 1, road.get()).at("rail"), "false");
}

TEST(Lua, NodeMotorVehicleAccessOverridesGeneralRestrictions) {
  mjolnir::LuaTagTransform lua(std::string(lua_graph_lua, lua_graph_lua + lua_graph_lua_len));
  constexpr auto motorized = baldr::kAutoAccess | baldr::kMopedAccess | baldr::kMotorcycleAccess;
  for (const std::string restriction : {"access", "vehicle"}) {
    for (const std::string general_access : {"yes", "no"}) {
      for (const std::string vehicle_access : {"yes", "no"}) {
        SCOPED_TRACE(restriction + "=" + general_access + " motor_vehicle=" + vehicle_access);
        TagsBuilder tags;
        tags.insert({"barrier", "toll_booth"});
        tags.insert({restriction, general_access});
        tags.insert({"motor_vehicle", vehicle_access});
        const auto result = lua.Transform(mjolnir::OSMType::kNode, 1, tags.get());
        const auto mask = std::stoul(result.at("access_mask"));
        EXPECT_EQ(mask & motorized, vehicle_access == "yes" ? motorized : 0);
        EXPECT_EQ(mask & baldr::kBicycleAccess, general_access == "yes" ? baldr::kBicycleAccess : 0);
      }
    }
  }
}

TEST(Lua, ExplicitNodeModeAccessOverridesMotorVehicleAccess) {
  mjolnir::LuaTagTransform lua(std::string(lua_graph_lua, lua_graph_lua + lua_graph_lua_len));
  for (const auto& mode :
       {std::pair{"motorcycle", baldr::kMotorcycleAccess}, std::pair{"moped", baldr::kMopedAccess},
        std::pair{"mofa", baldr::kMopedAccess}}) {
    for (const std::string vehicle_access : {"yes", "no"}) {
      for (const std::string mode_access : {"yes", "no"}) {
        SCOPED_TRACE(std::string(mode.first) + "=" + mode_access +
                     " motor_vehicle=" + vehicle_access);
        TagsBuilder tags;
        tags.insert({"barrier", "toll_booth"});
        tags.insert({"access", "no"});
        tags.insert({"motor_vehicle", vehicle_access});
        tags.insert({mode.first, mode_access});
        tags.insert({"bicycle", "yes"});
        const auto result = lua.Transform(mjolnir::OSMType::kNode, 1, tags.get());
        const auto mask = std::stoul(result.at("access_mask"));
        EXPECT_EQ((mask & mode.second) != 0, mode_access == "yes");
        EXPECT_NE(mask & baldr::kBicycleAccess, 0);
      }
    }
  }
}
} // namespace

// TODO: sweet jesus add more tests of this class!

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
