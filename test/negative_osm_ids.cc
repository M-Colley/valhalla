#include "midgard/sequence.h"
#include "mjolnir/osmnode.h"
#include "mjolnir/osmway.h"
#include "mjolnir/pbfgraphparser.h"

#include <boost/property_tree/ptree.hpp>
#include <gtest/gtest.h>
#include <osmium/builder/attr.hpp>
#include <osmium/io/pbf_output.hpp>
#include <osmium/io/writer.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

using namespace valhalla::mjolnir;
using namespace valhalla::midgard;

namespace {
struct ParserFiles {
  std::filesystem::path dir;
  std::string pbf;
  boost::property_tree::ptree config;

  ParserFiles() {
    dir = std::filesystem::path(VALHALLA_BUILD_DIR "test/data/negative_osm_ids") /
          ::testing::UnitTest::GetInstance()->current_test_info()->name();
    std::filesystem::create_directories(dir);
    pbf = (dir / "input.osm.pbf").string();
    config.put("tile_dir", dir.string());
    config.put("id_table_size", 1000);
    config.put("concurrency", 4);
  }

  std::string path(const char* name) const {
    return (dir / name).string();
  }

  void write(osmium::memory::Buffer&& buffer) const {
    osmium::io::Writer writer(pbf, osmium::io::overwrite::allow);
    writer(std::move(buffer));
    writer.close();
  }

  OSMData ways() const {
    return PBFGraphParser::ParseWays(config, {pbf}, path("ways.bin"), path("way_nodes.bin"),
                                     path("access.bin"));
  }
};

void expect_negative(const std::function<void()>& parse, const std::string& detail) {
  try {
    parse();
    FAIL() << "Expected rejection of " << detail;
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find(detail), std::string::npos);
    EXPECT_NE(std::string(error.what()).find("renumber"), std::string::npos);
  }
}

TEST(NegativeOSMIds, RejectWayIdAndNodeReferenceFromWorkers) {
  for (bool bad_way : {true, false}) {
    SCOPED_TRACE(bad_way);
    ParserFiles files;
    osmium::memory::Buffer buffer(1024, osmium::memory::Buffer::auto_grow::yes);
    const std::vector<osmium::object_id_type> refs = {1, bad_way ? 2 : -2};
    osmium::builder::add_way(buffer, osmium::builder::attr::_id(bad_way ? -1 : 1),
                             osmium::builder::attr::_version(1), osmium::builder::attr::_nodes(refs),
                             osmium::builder::attr::_tags({{"highway", "residential"}}));
    files.write(std::move(buffer));
    expect_negative([&] { files.ways(); }, bad_way ? "way ID -1" : "way node reference ID -2");
  }
}

TEST(NegativeOSMIds, RejectReferenceInOtherwiseIgnoredWay) {
  ParserFiles files;
  osmium::memory::Buffer buffer(1024, osmium::memory::Buffer::auto_grow::yes);
  const std::vector<osmium::object_id_type> refs = {-2};
  osmium::builder::add_way(buffer, osmium::builder::attr::_id(1), osmium::builder::attr::_version(1),
                           osmium::builder::attr::_nodes(refs));
  files.write(std::move(buffer));
  expect_negative([&] { files.ways(); }, "way node reference ID -2");
}

TEST(NegativeOSMIds, DrainWorkerQueuesAfterInvalidReference) {
  ParserFiles files;
  osmium::io::Writer writer(files.pbf, osmium::io::overwrite::allow);
  for (int chunk = 0; chunk < 64; ++chunk) {
    osmium::memory::Buffer buffer(1024, osmium::memory::Buffer::auto_grow::yes);
    for (int i = 0; i < 100; ++i) {
      const int id = chunk * 100 + i + 1;
      const std::vector<osmium::object_id_type> refs = {1, id == 1601 ? -2 : 2};
      osmium::builder::add_way(buffer, osmium::builder::attr::_id(id),
                               osmium::builder::attr::_version(1),
                               osmium::builder::attr::_nodes(refs),
                               osmium::builder::attr::_tags({{"highway", "residential"}}));
    }
    writer(std::move(buffer));
  }
  writer.close();
  expect_negative([&] { files.ways(); }, "way node reference ID -2");
}

TEST(NegativeOSMIds, PositiveWideWayAndReferences) {
  ParserFiles files;
  osmium::memory::Buffer buffer(1024, osmium::memory::Buffer::auto_grow::yes);
  constexpr osmium::object_id_type wide = osmium::object_id_type{1} << 33;
  const std::vector<osmium::object_id_type> refs = {wide + 1, wide + 2};
  osmium::builder::add_way(buffer, osmium::builder::attr::_id(wide),
                           osmium::builder::attr::_version(1), osmium::builder::attr::_nodes(refs),
                           osmium::builder::attr::_tags({{"highway", "residential"}}));
  files.write(std::move(buffer));
  const auto data = files.ways();
  EXPECT_EQ(data.osm_way_count, 1);
  sequence<OSMWay> ways(files.path("ways.bin"), false);
  ASSERT_EQ(ways.size(), 1);
  EXPECT_EQ((*ways[0]).way_id(), static_cast<uint64_t>(wide));
  sequence<OSMWayNode> nodes(files.path("way_nodes.bin"), false);
  ASSERT_EQ(nodes.size(), 2);
  EXPECT_EQ((*nodes[0]).node.osmid_, static_cast<uint64_t>(wide + 1));
  EXPECT_EQ((*nodes[1]).node.osmid_, static_cast<uint64_t>(wide + 2));
}

TEST(NegativeOSMIds, RejectAreaWayIdAndNodeReference) {
  for (int scenario : {0, 1, 2}) {
    SCOPED_TRACE(scenario);
    const bool bad_way = scenario == 0;
    ParserFiles files;
    osmium::memory::Buffer buffer(1024, osmium::memory::Buffer::auto_grow::yes);
    const std::vector<osmium::object_id_type> refs = {1, bad_way ? 2 : -2};
    osmium::builder::add_way(buffer, osmium::builder::attr::_id(bad_way ? -1 : 1),
                             osmium::builder::attr::_version(1), osmium::builder::attr::_nodes(refs));
    files.write(std::move(buffer));
    { sequence<OSMWay> ways(files.path("ways.bin"), true); }
    { sequence<OSMWayNode> nodes(files.path("way_nodes.bin"), true); }
    OSMData data{};
    data.initialized = true;
    data.area_relations.emplace(1, OSMAreaMember{scenario == 2 ? 2u : 1u, true});
    expect_negative(
        [&] {
          PBFGraphParser::ParseAreaWays(files.config, {files.pbf}, files.path("ways.bin"),
                                        files.path("way_nodes.bin"), data);
        },
        bad_way ? "way ID -1" : "way node reference ID -2");
  }
}

TEST(NegativeOSMIds, PositiveWideAreaWayAndReferences) {
  ParserFiles files;
  constexpr osmium::object_id_type wide = osmium::object_id_type{1} << 33;
  osmium::memory::Buffer buffer(1024, osmium::memory::Buffer::auto_grow::yes);
  const std::vector<osmium::object_id_type> refs = {wide + 1, wide + 2};
  osmium::builder::add_way(buffer, osmium::builder::attr::_id(wide),
                           osmium::builder::attr::_version(1), osmium::builder::attr::_nodes(refs));
  files.write(std::move(buffer));
  { sequence<OSMWay> ways(files.path("ways.bin"), true); }
  { sequence<OSMWayNode> nodes(files.path("way_nodes.bin"), true); }
  OSMData data{};
  data.initialized = true;
  data.area_relations.emplace(1, OSMAreaMember{static_cast<uint64_t>(wide), true});
  PBFGraphParser::ParseAreaWays(files.config, {files.pbf}, files.path("ways.bin"),
                                files.path("way_nodes.bin"), data);
  sequence<OSMWay> ways(files.path("ways.bin"), false);
  sequence<OSMWayNode> nodes(files.path("way_nodes.bin"), false);
  ASSERT_EQ(ways.size(), 1);
  ASSERT_EQ(nodes.size(), 2);
  EXPECT_EQ((*ways[0]).way_id(), static_cast<uint64_t>(wide));
  EXPECT_EQ((*nodes[0]).node.osmid_, static_cast<uint64_t>(wide + 1));
  EXPECT_EQ((*nodes[1]).node.osmid_, static_cast<uint64_t>(wide + 2));
}

TEST(NegativeOSMIds, RejectRelationIdAndMember) {
  for (bool bad_relation : {true, false}) {
    SCOPED_TRACE(bad_relation);
    ParserFiles files;
    osmium::memory::Buffer buffer(1024, osmium::memory::Buffer::auto_grow::yes);
    const std::vector<osmium::builder::attr::member_type> members = {
        {osmium::item_type::way, bad_relation ? 1 : -2, "from"}};
    osmium::builder::add_relation(buffer, osmium::builder::attr::_id(bad_relation ? -1 : 1),
                                  osmium::builder::attr::_version(1),
                                  osmium::builder::attr::_members(members));
    files.write(std::move(buffer));
    OSMData data{};
    data.initialized = true;
    expect_negative(
        [&] {
          PBFGraphParser::ParseRelations(files.config, {files.pbf}, files.path("from.bin"),
                                         files.path("to.bin"), data);
        },
        bad_relation ? "relation ID -1" : "relation member ID -2");
  }
}

TEST(NegativeOSMIds, RejectNodeIdInBothNodePasses) {
  for (bool bss : {false, true}) {
    SCOPED_TRACE(bss);
    ParserFiles files;
    osmium::memory::Buffer buffer(1024, osmium::memory::Buffer::auto_grow::yes);
    osmium::builder::add_node(buffer, osmium::builder::attr::_id(-1),
                              osmium::builder::attr::_version(1),
                              osmium::builder::attr::_location(osmium::Location{0., 0.}));
    files.write(std::move(buffer));
    files.config.put("import_bike_share_stations", bss);
    { sequence<OSMWayNode> nodes(files.path("way_nodes.bin"), true); }
    OSMData data{};
    data.initialized = true;
    expect_negative(
        [&] {
          PBFGraphParser::ParseNodes(files.config, {files.pbf}, files.path("way_nodes.bin"),
                                     files.path("bss.bin"), files.path("linguistics.bin"), data);
        },
        "node ID -1");
  }
}

// Generate fresh, small PBFs for the real build-tiles CLI integration test. This mode only writes
// files; the Python test runs them against the patched CLI in separate bounded subprocesses.
void generate_cli_fixtures(const std::filesystem::path& dir) {
  std::filesystem::create_directories(dir);
  for (const std::string& name : {"positive", "negative_way", "negative_ref", "negative_node",
                                  "negative_relation", "negative_member", "negative_many"}) {
    osmium::io::Writer writer((dir / (name + ".osm.pbf")).string(), osmium::io::overwrite::allow);
    osmium::memory::Buffer buffer(1024, osmium::memory::Buffer::auto_grow::yes);
    if (name == "negative_node") {
      osmium::builder::add_node(buffer, osmium::builder::attr::_id(-1),
                                osmium::builder::attr::_version(1),
                                osmium::builder::attr::_location(osmium::Location{0., 0.}));
    }
    for (int id : {1, 2}) {
      osmium::builder::add_node(buffer, osmium::builder::attr::_id(id),
                                osmium::builder::attr::_version(1),
                                osmium::builder::attr::_location(osmium::Location{id * 0.001, 0.}));
    }
    const std::vector<osmium::object_id_type> refs = {1, name == "negative_ref" ? -2 : 2};
    osmium::builder::add_way(buffer, osmium::builder::attr::_id(name == "negative_way" ? -1 : 1),
                             osmium::builder::attr::_version(1), osmium::builder::attr::_nodes(refs),
                             osmium::builder::attr::_tags({{"highway", "residential"}}));
    if (name == "negative_relation" || name == "negative_member") {
      const std::vector<osmium::builder::attr::member_type> members = {
          {osmium::item_type::way, name == "negative_member" ? -2 : 1, "from"}};
      osmium::builder::add_relation(buffer,
                                    osmium::builder::attr::_id(name == "negative_relation" ? -1 : 1),
                                    osmium::builder::attr::_version(1),
                                    osmium::builder::attr::_members(members));
    }
    writer(std::move(buffer));
    if (name == "negative_many") {
      for (int chunk = 0; chunk < 64; ++chunk) {
        osmium::memory::Buffer next(1024, osmium::memory::Buffer::auto_grow::yes);
        for (int i = 0; i < 100; ++i) {
          const int id = chunk * 100 + i + 2;
          const std::vector<osmium::object_id_type> nodes = {1, id == 102 ? -2 : 2};
          osmium::builder::add_way(next, osmium::builder::attr::_id(id),
                                   osmium::builder::attr::_version(1),
                                   osmium::builder::attr::_nodes(nodes),
                                   osmium::builder::attr::_tags({{"highway", "residential"}}));
        }
        writer(std::move(next));
      }
    }
    writer.close();
  }
}

TEST(NegativeOSMIds, FreshCliFixturesReachExpectedParserStage) {
  ParserFiles files;
  const auto fixtures = files.dir / "fixtures";
  generate_cli_fixtures(fixtures);
  for (const std::string& name : {"positive", "negative_way", "negative_ref", "negative_node",
                                  "negative_relation", "negative_member", "negative_many"}) {
    SCOPED_TRACE(name);
    std::filesystem::copy_file(fixtures / (name + ".osm.pbf"), files.pbf,
                               std::filesystem::copy_options::overwrite_existing);
    if (name == "negative_way" || name == "negative_ref" || name == "negative_many") {
      expect_negative([&] { files.ways(); },
                      name == "negative_way" ? "way ID -1" : "way node reference ID -2");
      continue;
    }
    auto data = files.ways();
    EXPECT_EQ(data.osm_way_count, 1);
    auto relations = [&] {
      PBFGraphParser::ParseRelations(files.config, {files.pbf}, files.path("from.bin"),
                                     files.path("to.bin"), data);
    };
    if (name == "negative_relation" || name == "negative_member") {
      expect_negative(relations,
                      name == "negative_relation" ? "relation ID -1" : "relation member ID -2");
      continue;
    }
    relations();
    auto nodes = [&] {
      PBFGraphParser::ParseNodes(files.config, {files.pbf}, files.path("way_nodes.bin"),
                                 files.path("bss.bin"), files.path("linguistics.bin"), data);
    };
    if (name == "negative_node")
      expect_negative(nodes, "node ID -1");
    else
      nodes();
  }
}
} // namespace

int main(int argc, char* argv[]) {
  if (argc == 3 && std::string(argv[1]) == "--generate-cli-fixtures") {
    generate_cli_fixtures(argv[2]);
    return 0;
  }
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
