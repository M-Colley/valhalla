#include "mjolnir/uniquenames.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <utility>

using namespace std;
using namespace valhalla::mjolnir;

namespace {

TEST(UniqueNames, Size) {
  UniqueNames names;
  names.index("Main Street");
  names.index("I-95");
  names.index("MD-32");
  names.index("I-95 S");
  EXPECT_EQ(names.Size(), 4);

  // Add a duplicate name and a unique name. Make sure Size
  // reflects only one name added
  names.index("MD-32");
  names.index("First Avenue");
  EXPECT_EQ(names.Size(), 5);
}

TEST(UniqueNames, TestAddAndIndex) {
  UniqueNames names;
  uint32_t index1 = names.index("I-95");
  uint32_t index2 = names.index("I-95 S");
  uint32_t index3 = names.index("I-95 N");
  uint32_t index4 = names.index("Interstate 95");
  uint32_t index5 = names.index("I-95");
  uint32_t index6 = names.index("I-95 N");

  EXPECT_EQ(index3, index6) << "UniqueNames: indexes for common name are not equal";
  EXPECT_EQ(index1, index5) << "UniqueNames: indexes for common name are not equal";

  // Test getting the name given the index
  EXPECT_EQ(names.name(index1), "I-95");
  EXPECT_EQ(names.name(index2), "I-95 S");
  EXPECT_EQ(names.name(index3), "I-95 N");
  EXPECT_EQ(names.name(index4), "Interstate 95");
  EXPECT_EQ(names.name(index5), "I-95");
  EXPECT_EQ(names.name(index6), "I-95 N");
}

TEST(UniqueNames, LookupSurvivesGrowth) {
  UniqueNames names;
  for (uint32_t i = 1; i <= 4096; ++i) {
    EXPECT_EQ(names.index("Street " + std::to_string(i)), i);
  }
  for (uint32_t i = 1; i <= 4096; ++i) {
    const auto expected = "Street " + std::to_string(i);
    EXPECT_EQ(names.name(i), expected);
    EXPECT_EQ(names.index(expected), i);
  }
  EXPECT_EQ(names.Size(), 4096);
}

TEST(UniqueNames, ClearPreservesReservedIndex) {
  UniqueNames names;
  names.index("Old street");
  names.Clear();
  ASSERT_EQ(names.Size(), 0);
  EXPECT_TRUE(names.name(0).empty());
  EXPECT_TRUE(names.name(100).empty());
  EXPECT_EQ(names.index("New street"), 1);
  EXPECT_EQ(names.index(""), 0);
  EXPECT_EQ(names.name(1), "New street");
  EXPECT_EQ(names.Size(), 1);
}

TEST(UniqueNames, CopiesOwnTheirNames) {
  UniqueNames assigned;
  assigned.index("Replaced street");
  {
    UniqueNames original;
    const auto index = original.index("Copied street");
    UniqueNames copied(original);
    assigned = copied;
    original.Clear();
    EXPECT_EQ(copied.name(index), "Copied street");
    copied.Clear();
  }
  EXPECT_EQ(assigned.name(1), "Copied street");
  EXPECT_EQ(assigned.index("Another street"), 2);
}

TEST(UniqueNames, MovesPreserveNamesAndAllowReuse) {
  UniqueNames original;
  original.index("Moved street");
  UniqueNames moved(std::move(original));
  EXPECT_EQ(moved.name(1), "Moved street");
  EXPECT_EQ(original.Size(), 0);
  EXPECT_TRUE(original.name(0).empty());
  EXPECT_EQ(original.index("Reused street"), 1);
  UniqueNames assigned;
  assigned = std::move(moved);
  EXPECT_EQ(assigned.name(1), "Moved street");
  EXPECT_EQ(moved.index("Reused again"), 1);
}

} // namespace

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
