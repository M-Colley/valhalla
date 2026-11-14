#include "test.h"
#include "tyr/actor.h"

#include <boost/property_tree/ptree.hpp>

using namespace valhalla;
using namespace valhalla::midgard;
using namespace std::string_literals;

namespace {

// fake config
const auto conf = test::make_config("test/data/utrecht_tiles");

std::string admin_filter_request(const std::string& filters) {
  return R"({"costing":"auto","shape_match":"map_snap","shape":[
      {"lat":40.546115,"lon":-76.385076},{"lat":40.544232,"lon":-76.385752}],
      "filters":)" +
         filters + "}";
}

TEST(ThorWorker, IncludeAdminFieldsWithoutNodeIndex) {
  tyr::actor_t actor(test::make_config(VALHALLA_SOURCE_DIR "test/traffic_matcher_tiles"), true);
  const auto baseline = test::json_to_pt(actor.trace_attributes(admin_filter_request("{}")));
  const auto baseline_admins = baseline.get_child_optional("admins");
  ASSERT_TRUE(baseline_admins);
  ASSERT_FALSE(baseline_admins->empty());
  EXPECT_FALSE(baseline_admins->front().second.get<std::string>("country_code").empty());

  for (const auto* field : {"country_code", "country_text", "state_code", "state_text"}) {
    SCOPED_TRACE(field);
    const auto result = test::json_to_pt(actor.trace_attributes(
        admin_filter_request(R"({"action":"include","attributes":["admin.)"s + field + R"("]})")));
    const auto admins = result.get_child_optional("admins");
    ASSERT_TRUE(admins);
    ASSERT_EQ(admins->size(), baseline_admins->size());
    auto expected = baseline_admins->begin();
    for (const auto& admin : *admins) {
      const auto value = expected++->second.get_optional<std::string>(field);
      EXPECT_EQ(admin.second.size(), value ? 1 : 0);
      if (value) {
        EXPECT_EQ(admin.second.get<std::string>(field), *value);
      }
    }
    for (const auto& edge : result.get_child("edges")) {
      EXPECT_TRUE(edge.second.empty());
    }
  }
}

TEST(ThorWorker, IncludeAdminIndexLinksToRequestedAdmins) {
  tyr::actor_t actor(test::make_config(VALHALLA_SOURCE_DIR "test/traffic_matcher_tiles"), true);
  Api api;
  const auto result = test::json_to_pt(actor.trace_attributes(
      admin_filter_request(
          R"({"action":"include","attributes":["admin.country_code","node.admin_index"]})"),
      nullptr, &api));
  const auto admins = result.get_child_optional("admins");
  ASSERT_TRUE(admins);
  ASSERT_FALSE(admins->empty());
  ASSERT_GT(api.trip().routes_size(), 0);
  for (const auto& route : api.trip().routes()) {
    for (const auto& leg : route.legs()) {
      ASSERT_GT(leg.admin_size(), 0);
      for (const auto& node : leg.node()) {
        EXPECT_LT(node.admin_index(), leg.admin_size());
      }
    }
  }
  for (const auto& edge : result.get_child("edges")) {
    EXPECT_LT(edge.second.get<unsigned>("end_node.admin_index"), admins->size());
  }
}

TEST(ThorWorker, ExcludedAdminFieldsStayAbsent) {
  tyr::actor_t actor(test::make_config(VALHALLA_SOURCE_DIR "test/traffic_matcher_tiles"), true);
  for (
      const auto* filters :
      {R"({"action":"include","attributes":["node.admin_index"]})",
       R"({"action":"exclude","attributes":["admin.country_code","admin.country_text","admin.state_code","admin.state_text"]})"}) {
    SCOPED_TRACE(filters);
    const auto result = test::json_to_pt(actor.trace_attributes(admin_filter_request(filters)));
    EXPECT_FALSE(result.get_child_optional("admins"));
    for (const auto& edge : result.get_child("edges")) {
      EXPECT_TRUE(edge.second.get_optional<unsigned>("end_node.admin_index"));
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
