#include "mjolnir/graph_lua_proc.h"
#include "mjolnir/luatagtransform.h"
#include "mjolnir/osmdata.h"

#include <gtest/gtest.h>
#include <osmium/builder/osm_object_builder.hpp>

#include <map>
#include <string>

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

auto transform_way(mjolnir::LuaTagTransform& lua, const std::map<std::string, std::string>& values) {
  TagsBuilder tags;
  for (const auto& tag : values) {
    tags.insert(tag);
  }
  return lua.Transform(mjolnir::OSMType::kWay, 1, tags.get());
}

TEST(Lua, PedestrianIgnoresVehicleOneway) {
  mjolnir::LuaTagTransform lua(std::string(lua_graph_lua, lua_graph_lua + lua_graph_lua_len));
  for (const std::string highway :
       {"footway", "pedestrian", "path", "steps", "cycleway", "residential"}) {
    for (const std::string oneway : {"yes", "true", "1", "-1", "no"}) {
      SCOPED_TRACE(highway + " oneway=" + oneway);
      auto results = transform_way(lua, {{"highway", highway},
                                         {"oneway", oneway},
                                         {"foot", "yes"},
                                         {"bicycle", "yes"}});
      EXPECT_EQ(results.at("pedestrian_forward"), "true");
      EXPECT_EQ(results.at("pedestrian_backward"), "true");
      EXPECT_EQ(results.at("bike_forward"), oneway == "-1" ? "false" : "true");
      EXPECT_EQ(results.at("bike_backward"), oneway == "no" || oneway == "-1" ? "true" : "false");
      if (highway == "residential") {
        EXPECT_EQ(results.at("auto_forward"), oneway == "-1" ? "false" : "true");
        EXPECT_EQ(results.at("auto_backward"), oneway == "no" || oneway == "-1" ? "true" : "false");
      }
    }
  }
}

TEST(Lua, PedestrianOnewayUsesFootDirection) {
  mjolnir::LuaTagTransform lua(std::string(lua_graph_lua, lua_graph_lua + lua_graph_lua_len));
  for (const std::string highway :
       {"footway", "pedestrian", "path", "steps", "cycleway", "residential"}) {
    for (const std::string oneway : {"", "yes", "-1", "no"}) {
      for (const std::string foot_oneway : {"yes", "true", "1", "-1", "no", "false"}) {
        SCOPED_TRACE(highway + " oneway=" + oneway + " oneway:foot=" + foot_oneway);
        std::map<std::string, std::string> tags = {{"highway", highway},
                                                   {"foot", "yes"},
                                                   {"oneway:foot", foot_oneway}};
        if (!oneway.empty()) {
          tags.emplace("oneway", oneway);
        }
        auto results = transform_way(lua, tags);
        EXPECT_EQ(results.at("pedestrian_forward"), foot_oneway == "-1" ? "false" : "true");
        EXPECT_EQ(results.at("pedestrian_backward"),
                  foot_oneway == "no" || foot_oneway == "false" || foot_oneway == "-1" ? "true"
                                                                                       : "false");
      }
    }
  }
}

TEST(Lua, PedestrianDirectionalAccessOverridesOneway) {
  mjolnir::LuaTagTransform lua(std::string(lua_graph_lua, lua_graph_lua + lua_graph_lua_len));
  for (const std::string oneway : {"yes", "-1", "no"}) {
    for (const std::string foot_oneway : {"yes", "-1", "no"}) {
      for (const std::string forward : {"yes", "no"}) {
        for (const std::string backward : {"yes", "no"}) {
          SCOPED_TRACE("oneway=" + oneway + " oneway:foot=" + foot_oneway +
                       " foot:forward=" + forward + " foot:backward=" + backward);
          auto results = transform_way(lua, {{"highway", "path"},
                                             {"oneway", oneway},
                                             {"oneway:foot", foot_oneway},
                                             {"foot:forward", forward},
                                             {"foot:backward", backward},
                                             {"bicycle", "yes"}});
          EXPECT_EQ(results.at("pedestrian_forward"), forward == "yes" ? "true" : "false");
          EXPECT_EQ(results.at("pedestrian_backward"), backward == "yes" ? "true" : "false");
        }
      }
    }
  }
}

TEST(Lua, PedestrianOnewayPreservesDeniedAccess) {
  mjolnir::LuaTagTransform lua(std::string(lua_graph_lua, lua_graph_lua + lua_graph_lua_len));
  for (const std::string oneway : {"yes", "-1", "no"}) {
    for (const std::string foot_oneway : {"yes", "-1", "no"}) {
      SCOPED_TRACE("oneway=" + oneway + " oneway:foot=" + foot_oneway);
      auto results = transform_way(lua, {{"highway", "residential"},
                                         {"oneway", oneway},
                                         {"oneway:foot", foot_oneway},
                                         {"foot", "no"}});
      EXPECT_EQ(results.at("pedestrian_forward"), "false");
      EXPECT_EQ(results.at("pedestrian_backward"), "false");
    }
  }
}

TEST(Lua, PedestrianDirectionalAccessWithoutFootOneway) {
  mjolnir::LuaTagTransform lua(std::string(lua_graph_lua, lua_graph_lua + lua_graph_lua_len));
  for (const std::string oneway : {"yes", "-1", "no"}) {
    SCOPED_TRACE("oneway=" + oneway);
    auto results =
        transform_way(lua, {{"highway", "path"}, {"oneway", oneway}, {"foot:backward", "no"}});
    EXPECT_EQ(results.at("pedestrian_forward"), "true");
    EXPECT_EQ(results.at("pedestrian_backward"), "false");
    results = transform_way(lua, {{"highway", "path"}, {"oneway", oneway}, {"foot:forward", "no"}});
    EXPECT_EQ(results.at("pedestrian_forward"), "false");
    EXPECT_EQ(results.at("pedestrian_backward"), "true");
    results = transform_way(lua, {{"highway", "path"},
                                  {"oneway", oneway},
                                  {"foot", "no"},
                                  {"foot:backward", "yes"}});
    EXPECT_EQ(results.at("pedestrian_forward"), "false");
    EXPECT_EQ(results.at("pedestrian_backward"), "true");
  }
}

TEST(Lua, PedestrianRoundaboutDoesNotInheritOneway) {
  mjolnir::LuaTagTransform lua(std::string(lua_graph_lua, lua_graph_lua + lua_graph_lua_len));
  auto results = transform_way(lua, {{"highway", "pedestrian"},
                                     {"junction", "roundabout"},
                                     {"bicycle", "yes"},
                                     {"motor_vehicle", "yes"}});
  EXPECT_EQ(results.at("pedestrian_forward"), "true");
  EXPECT_EQ(results.at("pedestrian_backward"), "true");
  EXPECT_EQ(results.at("bike_forward"), "true");
  EXPECT_EQ(results.at("bike_backward"), "false");
  EXPECT_EQ(results.at("auto_forward"), "true");
  EXPECT_EQ(results.at("auto_backward"), "false");
}

TEST(Lua, PedestrianConveyorPreservesTaggedOneway) {
  mjolnir::LuaTagTransform lua(std::string(lua_graph_lua, lua_graph_lua + lua_graph_lua_len));
  for (const std::string highway : {"steps", "footway"}) {
    for (const std::string conveying : {"yes", "forward", "backward", "reversible", "no"}) {
      for (const std::string oneway : {"yes", "-1", "no"}) {
        SCOPED_TRACE(highway + " conveying=" + conveying + " oneway=" + oneway);
        std::map<std::string, std::string> tags = {{"highway", highway},
                                                   {"conveying", conveying},
                                                   {"oneway", oneway},
                                                   {"bicycle", "no"}};
        auto results = transform_way(lua, tags);
        EXPECT_EQ(results.at("pedestrian_forward"),
                  conveying != "no" && oneway == "-1" ? "false" : "true");
        EXPECT_EQ(results.at("pedestrian_backward"),
                  conveying == "no" || oneway == "no" || oneway == "-1" ? "true" : "false");
        tags["oneway:foot"] = "no";
        results = transform_way(lua, tags);
        EXPECT_EQ(results.at("pedestrian_forward"), "true");
        EXPECT_EQ(results.at("pedestrian_backward"), "true");
        if (highway == "steps") {
          EXPECT_EQ(results.at("use"), "34");
        }
        tags.erase("oneway");
        tags.erase("oneway:foot");
        results = transform_way(lua, tags);
        EXPECT_EQ(results.at("pedestrian_forward"), "true");
        EXPECT_EQ(results.at("pedestrian_backward"), "true");
      }
    }
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
} // namespace

// TODO: sweet jesus add more tests of this class!

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
