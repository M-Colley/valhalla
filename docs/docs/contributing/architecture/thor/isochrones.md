What are Isochrone Maps?
------------------------

Recently valhalla has gained the ability to return these amazing structures called isochrones. What's an isochrone? The word is a combination of two greek roots `iso` meaning equal and `chrono` meaning time. So indeed, an isochrone is a structure representing equal time. In our case it's a line that represents constant travel time about a given location. One can think of isochrone maps as somewhat similar to the familiar topographic maps except that instead of lines of constant height, lines are of constant travel time are depicted. For this reason other terms common in topography apply such as contours or isolines.

![Isochrone Map](images/isochrone.png "Melbourne Driving Isochrone Map")
In this image the green, yellow, orange and red contour lines represent 15, 30, 45 and 60 minutes of driving time respectively.

How are Isochrone Maps Useful?
------------------------------

Isochrone maps can be used to make informed decisions about travel at both an individual level and en masse. You can get quantitative answers to questions like:

 * What are our lunch options within 5 minutes from here?
 * How much of the city lives within walking range of public transit?
 * What would adding/removing this road/bus stop/bridge do to travel times?
 * Where can I find housing that still has a reasonable commute to the office?

In other words planning departments of DOTs all the way down to consumer applications will have use-cases that call for such an isochrone map service.

Technical Details
------------------

Isochrones are formed in Valhalla by first creating a 2-D grid in latitiude,longitude about the location. This 2-D grid or array is used to define the time or cost it takes to get from the target location to each other grid location. This grid is populated by doing a breadth-first, least-cost first search (basically Dijkstra) from the origin location. At each iteration, the grid cells that are touched by a road segment or graph edge are marked with the time and cost from the origin, if less than the currently marked time. Once the expansion of the graph exceeds the maximum isochrone contour time the grid-marking process terminates. This leaves a 2-D grid or array that has the time or cost to reach each grid location.

For each requested time or distance interval, [`GriddedData::GenerateContours`](https://github.com/valhalla/valhalla/blob/master/valhalla/midgard/gridded_data.h) derives contour segments using [CONREC](https://paulbourke.net/papers/conrec/). It divides each grid cell into triangles and interpolates where the contour crosses their sides. The implementation joins directed segments at matching endpoints, extending or merging chains and closing rings when the ends meet. Open chains can remain; polygon requests discard them.

For each interval, the resulting contours are ordered by absolute area. The `denoise` option removes contours below a fraction of the largest contour's area. `generalize` controls Douglas–Peucker simplification in meters; when omitted, the service chooses a tolerance based on grid size. The implementation then shifts coordinates by half a grid cell to account for where samples were taken and removes lines with fewer than four points. See the [isochrone API reference](../../../api/isochrone.md) for these options and their limitations.

The [isochrone serializer](https://github.com/valhalla/valhalla/blob/master/src/tyr/isochrone_serializer.cc) emits GeoJSON line features by default. For polygon requests it groups exterior rings with their inner rings and emits a `Polygon` or `MultiPolygon`. Protobuf output also uses the generated contours. When GeoTIFF is requested and supported by the build, the serializer returns the underlying grid as a raster instead of generating contour geometries.

This 2-D grid can be useful for other purposes as well. It provides a very fast way to query a single location to see how long it takes to get there from the test location. Ultimately this could be a way to do very large one-to-many matrices.

Where is it?
------------

You can find the [API documentation here](../../../api/isochrone.md) and you see a live demo of it right [here](https://valhalla.github.io/demos/isochrone/).
