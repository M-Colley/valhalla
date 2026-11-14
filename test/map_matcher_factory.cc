// -*- mode: c++ -*-
#include "meili/map_matcher_factory.h"
#include "baldr/rapidjson_utils.h"
#include "proto/api.pb.h"
#include "sif/costconstants.h"
#include "test.h"

#include <boost/property_tree/ptree.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#if !defined(VALHALLA_SOURCE_DIR)
#define VALHALLA_SOURCE_DIR
#endif

namespace {

using namespace valhalla;

using ptree = boost::property_tree::ptree;

void create_costing_options(Costing::Type costing,
                            Options& options,
                            google::protobuf::RepeatedPtrField<CodedDescription>& warnings) {
  const rapidjson::Document doc;
  options.set_costing_type(costing);
  sif::ParseCosting(doc, "/costing_options", options, warnings);
}

TEST(MapMatcherFactory, ConfiguredCostingValues) {
  const auto root = test::make_config("/data/valhalla");
  meili::MapMatcherFactory factory(root);
  Options options;
  for (const auto& [costing, expected] :
       std::vector<std::pair<Costing::Type, float>>{{Costing::auto_, 200.f},
                                                    {Costing::pedestrian, 100.f},
                                                    {Costing::bicycle, 140.f},
                                                    {Costing::multimodal, 70.f},
                                                    {Costing::truck, 0.f}}) {
    options.set_costing_type(costing);
    EXPECT_FLOAT_EQ(factory.MergeConfig(options).transition_cost.turn_penalty_factor, expected)
        << Costing_Enum_Name(costing);
  }
}

TEST(MapMatcherFactory, CostingConfigPrecedenceAndIsolation) {
  auto root = test::make_config("/data/valhalla");
  root.put("meili.default.search_radius", 11);
  root.put("meili.default.beta", 5);
  root.put("meili.auto.search_radius", 22);
  root.put("meili.auto.breakage_distance", 4321);
  root.put("meili.pedestrian.search_radius", 33);
  meili::MapMatcherFactory factory(root);
  Options options;
  options.set_costing_type(Costing::auto_);
  auto config = factory.MergeConfig(options);
  EXPECT_FLOAT_EQ(config.candidate_search.search_radius_meters, 22.f);
  EXPECT_FLOAT_EQ(config.transition_cost.beta, 5.f);
  EXPECT_FLOAT_EQ(config.transition_cost.breakage_distance_meters, 4321.f);

  options.set_search_radius(44);
  options.set_turn_penalty_factor(99);
  options.set_breakage_distance(123);
  config = factory.MergeConfig(options);
  EXPECT_FLOAT_EQ(config.candidate_search.search_radius_meters, 44.f);
  EXPECT_FLOAT_EQ(config.transition_cost.turn_penalty_factor, 99.f);
  EXPECT_FLOAT_EQ(config.transition_cost.breakage_distance_meters, 4321.f)
      << "configured values apply even when request overrides are disabled";

  options.Clear();
  options.set_costing_type(Costing::pedestrian);
  EXPECT_FLOAT_EQ(factory.MergeConfig(options).candidate_search.search_radius_meters, 33.f);
  options.set_costing_type(Costing::truck);
  EXPECT_FLOAT_EQ(factory.MergeConfig(options).candidate_search.search_radius_meters, 11.f);
  options.set_costing_type(Costing::auto_);
  EXPECT_FLOAT_EQ(factory.MergeConfig(options).candidate_search.search_radius_meters, 22.f)
      << "request and other costing values must not mutate stored configurations";
}

TEST(MapMatcherFactory, CostingConfigPreservesRequestAllowlist) {
  auto root = test::make_config("/data/valhalla");
  root.put_child("meili.customizable", ptree{});
  root.put("meili.auto.search_radius", 12);
  root.put("meili.auto.gps_accuracy", 3);
  root.put("meili.auto.interpolation_distance", 8);
  meili::MapMatcherFactory factory(root);
  Options options;
  options.set_costing_type(Costing::auto_);
  options.set_search_radius(90);
  options.set_turn_penalty_factor(0);
  options.set_gps_accuracy(90);
  options.set_interpolation_distance(90);
  const auto config = factory.MergeConfig(options);
  EXPECT_FLOAT_EQ(config.candidate_search.search_radius_meters, 12.f);
  EXPECT_FLOAT_EQ(config.transition_cost.turn_penalty_factor, 200.f);
  EXPECT_FLOAT_EQ(config.emission_cost.gps_accuracy_meters, 3.f);
  EXPECT_FLOAT_EQ(config.routing.interpolation_distance_meters, 8.f);
}

TEST(MapMatcherFactory, CostingConfigIsValidated) {
  auto root = test::make_config("/data/valhalla");
  root.put("meili.bicycle.beta", 0);
  EXPECT_THROW(meili::MapMatcherFactory factory(root), std::invalid_argument);
}

TEST(MapMatcherFactory, MatcherReceivesCostingConfig) {
  auto root = test::make_config("/data/valhalla");
  root.put("meili.auto.search_radius", 12);
  meili::MapMatcherFactory factory(root);
  Api api;
  auto& options = *api.mutable_options();
  create_costing_options(Costing::auto_, options, *api.mutable_info()->mutable_warnings());
  std::unique_ptr<meili::MapMatcher> matcher(factory.Create(options));
  EXPECT_FLOAT_EQ(matcher->config().candidate_search.search_radius_meters, 12.f);
  EXPECT_FLOAT_EQ(matcher->config().transition_cost.turn_penalty_factor, 200.f);
}

TEST(MapMatcherFactory, TestMapMatcherFactory) {
  const auto root = test::make_config("/data/valhala");

  // Do it thousand times to check memory leak
  for (size_t i = 0; i < 3000; i++) {

    // Test configuration priority
    {
      // Copy it so we can change it
      Api api;
      Options& options = *api.mutable_options();
      create_costing_options(Costing::auto_, options, *api.mutable_info()->mutable_warnings());
      auto config = root;
      config.put<std::string>("meili.auto.hello", "world");
      config.put<std::string>("meili.default.hello", "default world");
      meili::MapMatcherFactory factory(config);
      auto matcher = factory.Create(options);
      EXPECT_EQ(matcher->travelmode(), sif::TravelMode::kDrive);

      // NOT SURE WHAT THIS IS SUPPOSED TO DO?
      //      EXPECT_EQ(matcher->config().get<std::string>("hello"), "world")
      //                       << "config for auto should override default");
      delete matcher;
    }

    // Test configuration priority
    {
      Api api;
      Options& options = *api.mutable_options();
      create_costing_options(Costing::bicycle, options, *api.mutable_info()->mutable_warnings());
      auto config = root;
      config.put<std::string>("meili.default.hello", "default world");
      meili::MapMatcherFactory factory(config);
      auto matcher = factory.Create(options);
      EXPECT_EQ(matcher->travelmode(), sif::TravelMode::kBicycle);

      // NOT SURE WHAT THIS IS SUPPOSED TO DO?
      //      EXPECT_EQ(matcher->config().get<std::string>("hello"), "default world")
      //                        << "config for bicycle should use default");
      delete matcher;
    }

    // Test configuration priority
    {
      Api api;
      Options& options = *api.mutable_options();
      create_costing_options(Costing::pedestrian, options, *api.mutable_info()->mutable_warnings());
      auto config = root;
      meili::MapMatcherFactory factory(config);
      float preferred_search_radius = 3;
      float incorrect_search_radius = 2;
      float default_search_radius = 1;
      options.set_search_radius(preferred_search_radius);
      config.put<int>("meili.auto.search_radius", incorrect_search_radius);
      config.put<int>("meili.default.search_radius", default_search_radius);
      auto matcher = factory.Create(options);
      EXPECT_EQ(matcher->travelmode(), sif::TravelMode::kPedestrian);
      EXPECT_EQ(matcher->config().candidate_search.search_radius_meters, preferred_search_radius)
          << "preference for pedestrian should override pedestrian config";

      delete matcher;
    }

    // Test configuration priority
    {
      Api api;
      Options& options = *api.mutable_options();
      create_costing_options(Costing::pedestrian, options, *api.mutable_info()->mutable_warnings());
      meili::MapMatcherFactory factory(root);
      float preferred_search_radius = 3;
      options.set_search_radius(preferred_search_radius);
      auto matcher = factory.Create(options);
      EXPECT_EQ(matcher->travelmode(), sif::TravelMode::kPedestrian);
      EXPECT_EQ(matcher->config().candidate_search.search_radius_meters, preferred_search_radius)
          << "preference for universal should override config";
      delete matcher;
    }

    // Test default mode
    {
      Api api;
      Options& options = *api.mutable_options();
      create_costing_options(Costing::auto_, options, *api.mutable_info()->mutable_warnings());
      meili::MapMatcherFactory factory(root);
      auto matcher = factory.Create(options);
      EXPECT_EQ(matcher->travelmode(), sif::TravelMode::kDrive)
          << "should read default mode in the meili.mode correctly";

      delete matcher;
    }

    // Test preferred mode
    {
      Api api;
      Options& options = *api.mutable_options();
      create_costing_options(Costing::pedestrian, options, *api.mutable_info()->mutable_warnings());
      meili::MapMatcherFactory factory(root);
      auto matcher = factory.Create(options);
      EXPECT_EQ(matcher->travelmode(), sif::TravelMode::kPedestrian)
          << "should read costing in options correctly";

      delete matcher;

      create_costing_options(Costing::bicycle, options, *api.mutable_info()->mutable_warnings());
      matcher = factory.Create(options);
      EXPECT_EQ(matcher->travelmode(), sif::TravelMode::kBicycle)
          << "should read costing in options correctly again";

      delete matcher;
    }

    // Test custom costing
    {
      Api api;
      Options& options = *api.mutable_options();
      create_costing_options(Costing::pedestrian, options, *api.mutable_info()->mutable_warnings());
      meili::MapMatcherFactory factory(root);
      options.set_costing_type(Costing::pedestrian);
      auto matcher = factory.Create(options);
      EXPECT_NE(matcher->costing()->travel_type(), (int)sif::PedestrianType::kWheelchair)
          << "should not have custom costing options when not set in preferences";

      delete matcher;

      options.mutable_costings()
          ->find(Costing::pedestrian)
          ->second.mutable_options()
          ->set_transport_type("wheelchair");
      matcher = factory.Create(options);

      EXPECT_EQ(matcher->costing()->travel_type(), (int)sif::PedestrianType::kWheelchair)
          << "should read custom costing options in preferences correctly";

      delete matcher;
    }
  }
}

TEST(MapMatcherFactory, TypeOnlyUsesDefaultCosting) {
  meili::MapMatcherFactory factory(test::make_config("/data/valhalla"));
  for (const auto type : {Costing::auto_, Costing::bicycle, Costing::pedestrian}) {
    SCOPED_TRACE(Costing_Enum_Name(type));
    Api api;
    create_costing_options(type, *api.mutable_options(), *api.mutable_info()->mutable_warnings());
    const std::unique_ptr<meili::MapMatcher> expected(factory.Create(api.options()));
    const std::unique_ptr<meili::MapMatcher> actual(factory.Create(type));
    EXPECT_EQ(actual->travelmode(), expected->travelmode());
    EXPECT_FLOAT_EQ(actual->costing()->AStarCostFactor(), expected->costing()->AStarCostFactor());
  }
}

TEST(MapMatcherFactory, TestMapMatcher) {
  const auto root = test::make_config("/data/valhalla");

  // Nothing special to test for the moment

  meili::MapMatcherFactory factory(root);
  Api api;
  Options& options = *api.mutable_options();
  create_costing_options(Costing::auto_, options, *api.mutable_info()->mutable_warnings());
  auto auto_matcher = factory.Create(options);
  create_costing_options(Costing::pedestrian, options, *api.mutable_info()->mutable_warnings());
  auto pedestrian_matcher = factory.Create(options);

  // Share the same pool

  EXPECT_EQ(&auto_matcher->graphreader(), &pedestrian_matcher->graphreader())
      << "graph reader should be shared among matchers";
  EXPECT_EQ(&auto_matcher->candidatequery(), &pedestrian_matcher->candidatequery())
      << "range query should be shared among matchers";

  delete auto_matcher;
  delete pedestrian_matcher;
}

} // namespace

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
