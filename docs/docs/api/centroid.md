# Centroid service API reference

**Beta.** Use `/centroid` to find a shared meeting point on the routing network and get directions from each origin.

Valhalla searches forward from each origin using the requested costing model. It uses the midpoint of the first road segment with a settled path from every origin, counting both directions of the segment together. Each route has its own travel time and distance.

## Request

Send a JSON object to `POST /centroid` with the following fields.

| Field | Description |
| --- | --- |
| `locations` | An array of at least two origins. Each origin includes `lat` and `lon` in decimal degrees. |
| `costing` | The costing model used for the search, such as `pedestrian`. See the [routing costing models](./route/api-reference.md#costing-models). |

The server defaults to at most five origins and a maximum straight-line distance of 200 km between any pair of origins. Operators can tune these limits with `service_limits.centroid.max_locations` and `service_limits.centroid.max_distance` (meters).

Save this example as `centroid.json`. The server needs routing tiles covering Pine Grove, Pennsylvania.

```json
{
  "locations": [
    {"lat": 40.546115, "lon": -76.385076},
    {"lat": 40.544232, "lon": -76.385752}
  ],
  "costing": "pedestrian"
}
```

```bash
curl -H 'Content-Type: application/json' --data-binary @centroid.json http://localhost:8002/centroid
```

## Response

The default [Valhalla JSON route response](./route/api-reference.md#outputs-of-a-route) contains one trip per origin, in request order. Read the first trip from `trip` and the remaining trips from `alternates[].trip`. In each trip, `locations[0]` is that trip's origin and `locations[1]` is the shared destination. Each trip includes a summary and one leg with directions.

The endpoint also supports [Protocol Buffers](./protocol-buffers.md).
