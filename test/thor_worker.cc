#include "test.h"
#include "thor/worker.h"
#include "tyr/actor.h"

#include <boost/property_tree/ptree.hpp>

#include <unordered_set>

using namespace valhalla;
using namespace valhalla::midgard;
using namespace std::string_literals;

namespace {

// fake config
const auto conf = test::make_config("test/data/utrecht_tiles");

class TraceOptionsWorker : public thor::thor_worker_t {
public:
  using thor_worker_t::parse_measurements;
  using thor_worker_t::thor_worker_t;

  const meili::Config& matcher_config() const {
    return matcher->config();
  }
};

Api trace_request(const std::string& trace_options) {
  Api request;
  ParseApi(R"({"costing":"auto","shape_match":"map_snap","shape":[
          {"lat":40.546115,"lon":-76.385076},{"lat":40.544232,"lon":-76.385752}],
          "trace_options":)" +
               trace_options + "}",
           Options::trace_attributes, request);
  return request;
}

TEST(ThorWorker, NoncustomizableBreakageDistanceWarns) {
  TraceOptionsWorker worker(test::make_config("/data/valhalla"));
  auto request = trace_request(R"({"breakage_distance":8000})");
  add_warning(request, 103);
  worker.parse_measurements(request);
  EXPECT_FLOAT_EQ(worker.matcher_config().transition_cost.breakage_distance_meters, 2000.f);
  EXPECT_FLOAT_EQ(request.options().breakage_distance(), 8000.f);
  ASSERT_EQ(request.info().warnings_size(), 2);
  EXPECT_EQ(request.info().warnings(0).code(), 103);
  EXPECT_EQ(request.info().warnings(1).code(), 217);
  EXPECT_NE(request.info().warnings(1).description().find("trace_options.breakage_distance"),
            std::string::npos);
}

TEST(ThorWorker, NoncustomizableTraceOptionsWarnIndividually) {
  auto config = test::make_config("/data/valhalla");
  config.put_child("meili.customizable", boost::property_tree::ptree{});
  config.put("meili.default.turn_penalty_factor", 7);
  config.put("meili.auto.turn_penalty_factor", 7);
  TraceOptionsWorker worker(config);
  auto request = trace_request(R"({"search_radius":90,"gps_accuracy":9,
      "turn_penalty_factor":70,"breakage_distance":8000,"interpolation_distance":30})");
  worker.parse_measurements(request);
  const auto& effective = worker.matcher_config();
  EXPECT_FLOAT_EQ(effective.candidate_search.search_radius_meters, 50.f);
  EXPECT_FLOAT_EQ(effective.emission_cost.gps_accuracy_meters, 5.f);
  EXPECT_FLOAT_EQ(effective.transition_cost.turn_penalty_factor, 7.f);
  EXPECT_FLOAT_EQ(effective.transition_cost.breakage_distance_meters, 2000.f);
  EXPECT_FLOAT_EQ(effective.routing.interpolation_distance_meters, 10.f);
  ASSERT_EQ(request.info().warnings_size(), 5);
  std::unordered_set<std::string> descriptions;
  for (const auto& warning : request.info().warnings()) {
    EXPECT_EQ(warning.code(), 217);
    descriptions.insert(warning.description());
  }
  for (const auto* option : {"search_radius", "gps_accuracy", "turn_penalty_factor",
                             "breakage_distance", "interpolation_distance"}) {
    EXPECT_EQ(descriptions.count(
                  "Trace option is not customizable on this server, ignoring trace_options."s +
                  option),
              1);
  }
}

TEST(ThorWorker, CustomizableTraceOptionsApplyWithoutWarnings) {
  auto config = test::make_config("/data/valhalla");
  boost::property_tree::ptree customizable;
  for (const auto* name : {"search_radius", "gps_accuracy", "turn_penalty_factor",
                           "breakage_distance", "interpolation_distance"}) {
    boost::property_tree::ptree entry;
    entry.put_value(name);
    customizable.push_back({"", entry});
  }
  config.put_child("meili.customizable", customizable);
  TraceOptionsWorker worker(config);
  auto request = trace_request(R"({"search_radius":90,"gps_accuracy":9,
      "turn_penalty_factor":70,"breakage_distance":8000,"interpolation_distance":30})");
  worker.parse_measurements(request);
  const auto& effective = worker.matcher_config();
  EXPECT_FLOAT_EQ(effective.candidate_search.search_radius_meters, 90.f);
  EXPECT_FLOAT_EQ(effective.emission_cost.gps_accuracy_meters, 9.f);
  EXPECT_FLOAT_EQ(effective.transition_cost.turn_penalty_factor, 70.f);
  EXPECT_FLOAT_EQ(effective.transition_cost.breakage_distance_meters, 8000.f);
  EXPECT_FLOAT_EQ(effective.routing.interpolation_distance_meters, 30.f);
  EXPECT_EQ(request.info().warnings_size(), 0);
}

TEST(ThorWorker, AbsentTraceOptionsDoNotWarn) {
  TraceOptionsWorker worker(test::make_config("/data/valhalla"));
  auto request = trace_request("{}");
  worker.parse_measurements(request);
  EXPECT_EQ(request.info().warnings_size(), 0);
}

TEST(ThorWorker, ConstMeasurementParsingRemainsReadOnly) {
  TraceOptionsWorker worker(test::make_config("/data/valhalla"));
  auto request = trace_request(R"({"breakage_distance":8000})");
  worker.parse_measurements(static_cast<const Api&>(request));
  EXPECT_FLOAT_EQ(worker.matcher_config().transition_cost.breakage_distance_meters, 2000.f);
  EXPECT_EQ(request.info().warnings_size(), 0);
}

TEST(ThorWorker, TraceResponsesIncludeIgnoredOptionWarnings) {
  const auto config = test::make_config(VALHALLA_SOURCE_DIR "test/traffic_matcher_tiles");
  tyr::actor_t actor(config, true);
  for (const auto* shape_match : {"map_snap", "walk_or_snap"}) {
    SCOPED_TRACE(shape_match);
    const std::string request = R"({"costing":"auto","shape_match":")"s + shape_match +
                                R"(","shape":[{"lat":40.546115,"lon":-76.385076},
        {"lat":40.5451735,"lon":-76.38500},{"lat":40.544232,"lon":-76.385752}],
        "best_paths":1,"trace_options":{"breakage_distance":8000}})";
    for (const auto action : {Options::trace_route, Options::trace_attributes}) {
      SCOPED_TRACE(Options_Action_Enum_Name(action));
      Api api;
      const auto result = test::json_to_pt(action == Options::trace_route
                                               ? actor.trace_route(request, nullptr, &api)
                                               : actor.trace_attributes(request, nullptr, &api));
      ASSERT_GT(api.trip().routes_size(), 0);
      for (const auto& route : api.trip().routes()) {
        for (const auto& leg : route.legs()) {
          ASSERT_EQ(leg.algorithms_size(), 1);
          EXPECT_EQ(leg.algorithms(0), "map_snap");
        }
      }
      const auto warnings =
          result.get_child_optional(action == Options::trace_route ? "trip.warnings" : "warnings");
      ASSERT_TRUE(warnings);
      ASSERT_EQ(warnings->size(), 2);
      std::unordered_set<unsigned> codes;
      for (const auto& warning : *warnings) {
        const auto code = warning.second.get<unsigned>("code");
        codes.insert(code);
        if (code == 217) {
          EXPECT_NE(warning.second.get<std::string>("text").find("trace_options.breakage_distance"),
                    std::string::npos);
        }
      }
      EXPECT_EQ(codes, (std::unordered_set<unsigned>{103, 217}));
    }
  }
}

TEST(ThorWorker, test_parse_filter_attributes_defaults) {
  tyr::actor_t actor(conf, true);

  auto result = test::json_to_pt(actor.trace_attributes(
      R"({"costing":"auto","shape_match":"map_snap","shape":[
          {"lat":52.09110,"lon":5.09806},
          {"lat":52.09098,"lon":5.09679}]})"));

  EXPECT_FALSE(result.get_child_optional("shape_attributes")) << "Expected excluded shape_attributes";

  EXPECT_TRUE(result.get_child_optional("edges")) << "Expected included edges";

  EXPECT_TRUE(result.get_child_optional("shape")) << "Expected included shape";
  EXPECT_FALSE(result.get_child_optional("edge.show_incidents"))
      << "Expected excluded edge.show_incidents";
}

TEST(ThorWorker, test_parse_filter_attributes_excludes) {
  tyr::actor_t actor(conf, true);

  std::vector<std::string> test_cases = {
      actor.trace_attributes(
          R"({"costing":"auto","shape_match":"map_snap","shape":[
          {"lat":52.09110,"lon":5.09806},
          {"lat":52.09098,"lon":5.09679}],
          "filters":{"attributes":["shape"], "action":"exclude"}})"),
      // TODO currently EnhancedTripPath sets any excluded fields to their default value
      // so this test can't check for excluded keys. Come back and add tests for
      // shape_attributes once we have them since those will be untouched by ETP.
      // actor.trace_route(
      //     R"({"costing":"auto","shape_match":"map_snap","shape":[
      //     {"lat":52.09110,"lon":5.09806},
      //     {"lat":52.09098,"lon":5.09679}],
      //     "filters":{"attributes":["shape"], "action":"exclude"}})"),
      // actor.route(
      //     R"({"costing":"auto","locations":[
      //     {"lat":52.09110,"lon":5.09806},
      //     {"lat":52.09098,"lon":5.09679}],
      //     "filters":{"attributes":["shape"], "action":"exclude"}})"),
  };
  std::vector<std::string> excluded_keys = {"shape", "trip.legs..shape", "trip.legs..shape"};

  for (size_t i = 0; i < test_cases.size(); ++i) {
    auto result = test::json_to_pt(test_cases[i]);
    EXPECT_FALSE(result.get_child_optional(excluded_keys[i]))
        << "Expected excluded shape | found " + excluded_keys[i] + "=" +
               result.get<std::string>(excluded_keys[i]);
  }
}

TEST(ThorWorker, test_parse_filter_attributes_includes) {
  tyr::actor_t actor(conf, true);

  std::vector<std::string> test_cases = {
      actor.trace_attributes(
          R"({"costing":"auto","shape_match":"map_snap","shape":[
          {"lat":52.09110,"lon":5.09806},
          {"lat":52.09098,"lon":5.09679}],"filters":{"attributes":["shape"], "action":"include"}})"),
      actor.trace_route(
          R"({"costing":"auto","shape_match":"map_snap","shape":[
          {"lat":52.09110,"lon":5.09806},
          {"lat":52.09098,"lon":5.09679}],
          "filters":{"attributes":["shape"], "action":"include"}})"),
      actor.route(
          R"({"costing":"auto","locations":[
          {"lat":52.09110,"lon":5.09806},
          {"lat":52.09098,"lon":5.09679}],"filters":{"attributes":["shape"],
          "action":"include"}})"),
  };
  std::vector<std::string> included_keys = {"shape", "trip.legs..shape", "trip.legs..shape"};

  for (size_t i = 0; i < test_cases.size(); ++i) {
    auto result = test::json_to_pt(test_cases[i]);
    EXPECT_TRUE(result.get_child_optional(included_keys[i]))
        << "Expected " + included_keys[i] + " to be present";
  }
}

TEST(ThorWorker, test_linear_references) {
  std::vector<std::string> requests = {
      R"({"costing":"auto","linear_references":true,"locations":[
          {"lat":52.09110,"lon":5.09806},
          {"lat":52.09098,"lon":5.09679}]})",
  };
  const std::vector<std::string>& expected = {
      "CwOgEyUK5SKXAP/H//wiBw==",
      "CwOf+CUK4iKXAP/k//8iBw==",
      "CwOf6yUK4SKXAP/Y//0iBw==",
  };
  tyr::actor_t actor(conf, true);
  for (const auto& request : requests) {
    auto result = test::json_to_pt(actor.route(request));
    std::vector<std::string> references;
    for (const auto& reference : result.get_child("trip.linear_references"))
      references.push_back(reference.second.get_value<std::string>());
    EXPECT_EQ(references.size(), 3);
    EXPECT_EQ(expected, references);
  }
}

} // namespace

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
