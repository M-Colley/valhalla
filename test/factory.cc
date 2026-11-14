#include "baldr/rapidjson_utils.h"
#include "proto/api.pb.h"
#include "proto/options.pb.h"
#include "sif/costfactory.h"

#include <gtest/gtest.h>

using namespace std;
using namespace valhalla;
using namespace valhalla::sif;

namespace {

TEST(Factory, Register) {
  Api api;
  Options& options = *api.mutable_options();
  const rapidjson::Document doc;
  CostFactory factory;
  options.set_costing_type(Costing::auto_);
  sif::ParseCosting(doc, "/costing_options", options, *api.mutable_info()->mutable_warnings());
  auto car = factory.Create(options);
  options.set_costing_type(Costing::bicycle);
  sif::ParseCosting(doc, "/costing_options", options, *api.mutable_info()->mutable_warnings());
  auto bike = factory.Create(options);
  auto truck = factory.Create(Costing::truck);
}

TEST(Factory, TypeOnlyUsesParsedDefaults) {
  CostFactory factory;
  for (const auto type : {Costing::auto_, Costing::bicycle, Costing::pedestrian, Costing::truck}) {
    SCOPED_TRACE(Costing_Enum_Name(type));
    Api api;
    auto& options = *api.mutable_options();
    options.set_costing_type(type);
    const rapidjson::Document doc;
    ParseCosting(doc, "/costing_options", options, *api.mutable_info()->mutable_warnings());
    const auto expected = factory.Create(options);
    const auto actual = factory.Create(type);
    EXPECT_FLOAT_EQ(actual->AStarCostFactor(), expected->AStarCostFactor());
    EXPECT_EQ(actual->travel_mode(), expected->travel_mode());
    EXPECT_EQ(actual->travel_type(), expected->travel_type());
  }
}

TEST(Factory, CustomTypeRegistration) {
  CostFactory factory;
  const auto type = static_cast<Costing::Type>(Costing::Type_ARRAYSIZE);
  bool called = false;
  factory.Register(type, [&](const Costing& costing) {
    called = true;
    EXPECT_EQ(costing.type(), type);
    return cost_ptr_t{};
  });
  EXPECT_EQ(factory.Create(type), nullptr);
  EXPECT_TRUE(called);
}

} // namespace

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
