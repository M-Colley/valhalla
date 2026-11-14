#include "gurka.h"

#include <gtest/gtest.h>
#include <rapidjson/document.h>

#include <iostream>
#include <string>

using namespace valhalla;

TEST(TraceOnewayObservation, FactorZeroAndTwoHundred) {
  const std::string ascii_map = R"(
    D
    |
    0
    |
    A--1--2-----------3--4--B
  )";
  const gurka::ways ways = {
      {"AB", {{"highway", "residential"}, {"oneway", "yes"}}},
      {"DA", {{"highway", "residential"}}},
  };
  const auto layout = gurka::detail::map_to_coordinates(ascii_map, 10);
  for (const auto* factor : {"0", "200"}) {
    SCOPED_TRACE(factor);
    const std::string directory = std::string("test/data/trace_oneway_probe_") + factor;
    auto map = gurka::buildtiles(layout, ways, {}, {}, directory,
                                 {{"meili.auto.turn_penalty_factor", factor}});
    std::string json;
    gurka::do_action(Options::trace_attributes, map, {"0", "3", "4", "1", "2"}, "auto",
                     {{"/elevation_interval", "30"}}, {}, &json);
    rapidjson::Document result;
    result.Parse(json.c_str());
    ASSERT_FALSE(result.HasParseError());
    ASSERT_TRUE(result.HasMember("edges"));
    ASSERT_TRUE(result["edges"].IsArray());
    std::cout << "OBSERVATION factor=" << factor << " edges=";
    for (const auto& edge : result["edges"].GetArray()) {
      if (edge.HasMember("names") && edge["names"].IsArray() && !edge["names"].Empty())
        std::cout << edge["names"][0].GetString() << ",";
      else
        std::cout << "<unnamed>,";
    }
    std::cout << " matched_edge_indices=";
    if (result.HasMember("matched_points") && result["matched_points"].IsArray()) {
      for (const auto& point : result["matched_points"].GetArray()) {
        if (point.HasMember("edge_index") && point["edge_index"].IsUint())
          std::cout << point["edge_index"].GetUint() << ",";
        else
          std::cout << "<none>,";
      }
    }
    std::cout << "\n";
  }
}
