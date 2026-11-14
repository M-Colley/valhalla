#include <gtest/gtest.h>

#ifdef minor
#undef minor
#endif
#define minor(device) ((device)&0xff)

#include "mjolnir/osmnode.h"

static_assert(minor(0x1234) == 0x34, "The minor macro must remain available");

TEST(OSMNode, MinorFlagWithSystemMacro) {
  valhalla::mjolnir::OSMNode node;
  EXPECT_FALSE((node.minor)());
  node.set_minor(true);
  EXPECT_TRUE((node.minor)());
  node.set_minor(false);
  EXPECT_FALSE((node.minor)());
}

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
