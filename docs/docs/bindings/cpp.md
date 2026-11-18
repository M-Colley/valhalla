# C++ Actor

`valhalla::tyr::actor_t` runs Valhalla actions in your process. Link your application to
`libvalhalla` and include `<valhalla/tyr/actor.h>`. Use the same [configuration](../start/building.md)
and routing tiles you would use for the HTTP service.

## Run a request

Action methods such as `route`, `isochrone`, and `matrix` accept the JSON request body
for the corresponding [HTTP API](../api/index.md). Set the coordinates to locations
covered by your routing tiles.

```cpp
#include <valhalla/tyr/actor.h>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <iostream>

int main() {
  boost::property_tree::ptree config;
  boost::property_tree::read_json("valhalla.json", config);
  valhalla::tyr::actor_t actor(config, /*auto_cleanup=*/true);

  std::cout << actor.route(R"({
    "locations": [
      {"lat": 40.546115, "lon": -76.385076},
      {"lat": 40.544232, "lon": -76.385752}
    ],
    "costing": "auto"
  })") << '\n';
}
```

The returned `std::string` contains the serialized response. Most actions default to
JSON. Other formats depend on the action and build options, including GPX for routes,
[Protocol Buffers](../api/protocol-buffers.md) for supported actions, and
[Mapbox Vector Tiles](../api/tile.md) from `tile`. Use the string's length when handling
binary responses, which can contain zero bytes. Errors such as an invalid request or
a route with no path are reported as C++ exceptions.

Pass a fresh `valhalla::Api` as the third argument to an action method to inspect the
objects populated during a JSON request. For a protobuf request, fill in the `Api`'s
`options` and pass an empty request string, or set `options.action` and call
`actor.act(api)`. Action methods still serialize their return value when given an `Api`.

## Cancellation and reuse

The optional `interrupt` argument points to a `std::function<void()>` that must stay
alive until the call returns. Algorithms that support interruption call it at checkpoints.
Return normally to continue or throw an exception to interrupt the work. The checkpoints
are action-dependent, so cancellation may take time.

Keep an actor for successive requests. With `auto_cleanup=true`, the actor cleans up its
workers when an action exits, including when it throws. With the default
`auto_cleanup=false`, call `cleanup()` after each action, including on error paths, before
reusing the actor.

An actor has mutable workers. For concurrent requests, use an independently constructed
actor with its own graph reader per thread. Copying an actor shares the original workers.
The constructor taking a `baldr::GraphReader&` borrows the reader. Keep that reader alive
for the lifetime of every actor that uses it.
