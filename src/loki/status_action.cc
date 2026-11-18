#include "baldr/tilehierarchy.h"
#include "config.h"
#include "filesystem_utils.h"
#include "loki/worker.h"
#include "proto/status.pb.h"

using namespace valhalla::baldr;

namespace {

time_t get_tileset_last_modified(const std::shared_ptr<GraphReader>& reader) {
  auto path = reader->GetTileSetLocation();
  try {
    return valhalla::filesystem_utils::last_write_time_t(path);
  } catch (...) {}
  return 0;
}

} // namespace

namespace valhalla {
namespace loki {
void loki_worker_t::status(Api& request) const {
#ifdef ENABLE_SERVICES
  // if we are in the process of shutting down we signal that here
  // should react by draining traffic (though they are likely doing this as they are usually the ones
  // who sent us the request to shutdown)
  if (prime_server::draining() || prime_server::shutting_down()) {
    throw valhalla_exception_t{102};
  }
#endif

  // info that's always returned
  auto* status = request.mutable_status();
  status->set_version(VALHALLA_PRINT_VERSION);
  status->set_tileset_last_modified(get_tileset_last_modified(reader));
  for (size_t i = 0; i < actions.size(); ++i)
    if (actions[i])
      *status->mutable_available_actions()->Add() =
          Options_Action_Enum_Name(static_cast<Options::Action>(i));

  // only return more info if explicitly asked for (can be very expensive)
  if (!request.options().verbose() || !allow_status_verbose)
    return;

  // Keep the first sample for this worker without retaining a tile reference.
  if (!tileset_status_) {
    TilesetStatus sampled;
    const auto transit_level = TileHierarchy::GetTransitLevel().level;
    graph_tile_ptr sample_tile;
    bool found_road_tile = false;
    for (const auto& tile_id : reader->GetTileSet()) {
      if (tile_id.level() == transit_level)
        sampled.has_transit_tiles = true;
      if (!found_road_tile) {
        if (auto tile = reader->GetGraphTile(tile_id)) {
          found_road_tile = tile_id.level() < transit_level && tile->header()->nodecount() > 0;
          sample_tile = std::move(tile);
        }
      }
      if (found_road_tile && (connectivity_map || sampled.has_transit_tiles))
        break;
    }
    if (sample_tile) {
      sampled.has_tiles = true;
      sampled.has_admins = sample_tile->header()->admincount() > 0;
      sampled.has_timezones =
          sample_tile->header()->nodecount() > 0 && sample_tile->node(0)->timezone() > 0;
      sampled.osm_changeset = sample_tile->header()->dataset_id();
    }
    tileset_status_ = sampled;
  }

  if (connectivity_map) {
    status->set_bbox(connectivity_map->to_geojson(2));
    const bool has_transit_tiles =
        connectivity_map->level_color_exists(TileHierarchy::GetTransitLevel().level);
    status->set_has_transit_tiles(has_transit_tiles);
  } else {
    status->set_has_transit_tiles(tileset_status_->has_transit_tiles);
  }

  status->set_has_tiles(tileset_status_->has_tiles);
  status->set_has_admins(tileset_status_->has_admins);
  status->set_has_timezones(tileset_status_->has_timezones);
  status->set_has_live_traffic(reader->HasLiveTraffic());
  status->set_osm_changeset(tileset_status_->osm_changeset);
}
} // namespace loki
} // namespace valhalla
